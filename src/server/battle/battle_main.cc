#include <memory>

#include "battle_service.h"
#include "service_base.h"
#include "sylar/core/log.h"
#include "sylar/rpc/rpc_channel.h"
#include "sylar/rpc/rpc_channel_pool.h"
#include "sylar/rpc/rpc_controller.h"
#include "sylar/rpc/rpc_provider.h"
#include "sylar/scheduler/iomanager.h"
#include "sylar/scheduler/timewheel.h"

static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

int main(int argc, char** argv) {
    ddt::ServiceRunner runner("battle");
    if (!runner.init(argc, argv)) {
        return 1;
    }
    const auto& cfg = runner.config();
    if (cfg.port == 0) {
        const_cast<ddt::ServiceConfig&>(cfg).port = 8400;
    }

    sylar::IOManager iom(4, true, "battle");

    iom.schedule([&]() {
        // 共享时间轮: 所有房间的回合超时定时器注册其上
        // steps=100ms(秒级超时精度足够), maxMin=10(最长 10 分钟)
        auto tw = std::make_shared<sylar::TimeWheel>();
        tw->init(100, 10);
        tw->start(&iom);

        // RPC 连接池: battle→gate 推送高频(每回合广播), 用池复用 TCP + etcd 缓存
        auto rpcPool = std::make_shared<sylar::rpc::RpcChannelPool>(cfg.etcd_endpoint, 8);

        // 推送闭包: battle -> gate 的 PushService.NotifyClient
        ddt::PushFn push = [&cfg, rpcPool](const ddt::RoutingHandle& h, uint16_t msg_id, const std::string& payload) {
            try {
                sylar::rpc::RpcChannel chan(cfg.etcd_endpoint, rpcPool.get());
                ddt::PushService::Stub stub(&chan);
                sylar::rpc::RpcController ctrl;
                ddt::NotifyReq req;
                req.set_account_id(h.accountId);
                req.set_msg_id(msg_id);
                req.set_payload(payload);
                ddt::ResultResp resp;
                stub.NotifyClient(&ctrl, &req, &resp, nullptr);
                if (ctrl.Failed()) {
                    SYLAR_LOG_WARN(g_logger) << "battle push fail account=" << h.accountId
                        << " msg_id=" << msg_id << " err=" << ctrl.ErrorText();
                }
            } catch (const std::exception& e) {
                SYLAR_LOG_WARN(g_logger) << "battle push exception: " << e.what();
            } catch (...) {
                SYLAR_LOG_WARN(g_logger) << "battle push unknown exception";
            }
        };

        // 批量推送闭包: battle -> gate 的 PushService.NotifyClients
        // 1 次 RPC 把同一条消息推给房间所有玩家, 替代逐人单推(N 次 RPC = N 个 fiber)
        ddt::BroadcastPushFn bpush = [&cfg, rpcPool](const std::vector<uint64_t>& account_ids, uint16_t msg_id, const std::string& payload) {
            try {
                sylar::rpc::RpcChannel chan(cfg.etcd_endpoint, rpcPool.get());
                ddt::PushService::Stub stub(&chan);
                sylar::rpc::RpcController ctrl;
                ddt::NotifyManyReq req;
                for (auto aid : account_ids) {
                    req.add_account_ids(aid);
                }
                req.set_msg_id(msg_id);
                req.set_payload(payload);
                ddt::ResultResp resp;
                stub.NotifyClients(&ctrl, &req, &resp, nullptr);
                if (ctrl.Failed()) {
                    SYLAR_LOG_WARN(g_logger) << "battle broadcast push fail n=" << account_ids.size()
                        << " msg_id=" << msg_id << " err=" << ctrl.ErrorText();
                }
            } catch (const std::exception& e) {
                SYLAR_LOG_WARN(g_logger) << "battle broadcast push exception: " << e.what();
            } catch (...) {
                SYLAR_LOG_WARN(g_logger) << "battle broadcast push unknown exception";
            }
        };

        // 注入 data 服 channel: 统一走 pool(与 push 共用连接池)
        auto impl = std::make_shared<ddt::BattleServiceImpl>(cfg, push, tw, bpush,
            std::make_shared<sylar::rpc::RpcChannel>(cfg.etcd_endpoint, rpcPool.get()));
        auto provider = std::make_shared<sylar::rpc::RpcProvider>();
        provider->setEtcd(cfg.etcd_endpoint, cfg.etcd_ttl);
        provider->setListen((uint16_t)cfg.port);
        provider->setAdvertise(cfg.advertiseAddr());
        provider->notifyService(impl.get());
        provider->run();

        static std::shared_ptr<ddt::BattleServiceImpl> g_keepalive = impl;
        static std::shared_ptr<sylar::TimeWheel> g_tw = tw;   // 保活时间轮
    });

    runner.installSignal();
    return 0;
}
