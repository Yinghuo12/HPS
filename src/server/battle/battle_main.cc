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
    if(!runner.init(argc, argv)) return 1;
    const auto& cfg = runner.config();
    if(cfg.port == 0) const_cast<ddt::ServiceConfig&>(cfg).port = 8400;

    sylar::IOManager iom(4, true, "battle");

    iom.schedule([&]() {
        // 共享时间轮: 所有房间的回合超时定时器注册其上(O(1) 调度, 大量房间并发时优于最小堆)。
        // steps=100ms(回合超时是秒级, 100ms 步长精度足够), maxMin=10(最长 10 分钟)。
        auto tw = std::make_shared<sylar::TimeWheel>();
        tw->init(100, 10);
        tw->start(&iom);

        // RPC 连接池: battle→gate 推送高频(每回合广播), 用池复用 TCP + etcd 发现缓存,
        // 替代每次推送新建短连接(connect+close)。独占借出语义保证无 fd 竞态。
        // 注: gate 转发层暂不接入(曾因 fd 竞态回退), 仅推送闭包用池。
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
                if(ctrl.Failed()) {
                    SYLAR_LOG_WARN(g_logger) << "battle push fail account=" << h.accountId
                        << " msg_id=" << msg_id << " err=" << ctrl.ErrorText();
                }
            } catch(const std::exception& e) {
                SYLAR_LOG_WARN(g_logger) << "battle push exception: " << e.what();
            } catch(...) {
                SYLAR_LOG_WARN(g_logger) << "battle push unknown exception";
            }
        };

        // 批量推送闭包: battle -> gate 的 PushService.NotifyClients
        // 1次RPC把同一条消息推给房间所有玩家, 替代原来逐人单推(N次RPC=N个fiber)。
        // 这是降 RPC 频率的根本手段: 4人房间广播从4个fiber降为1个。
        ddt::BroadcastPushFn bpush = [&cfg, rpcPool](const std::vector<uint64_t>& account_ids, uint16_t msg_id, const std::string& payload) {
            try {
                sylar::rpc::RpcChannel chan(cfg.etcd_endpoint, rpcPool.get());
                ddt::PushService::Stub stub(&chan);
                sylar::rpc::RpcController ctrl;
                ddt::NotifyManyReq req;
                for(auto aid : account_ids) req.add_account_ids(aid);
                req.set_msg_id(msg_id);
                req.set_payload(payload);
                ddt::ResultResp resp;
                stub.NotifyClients(&ctrl, &req, &resp, nullptr);
                if(ctrl.Failed()) {
                    SYLAR_LOG_WARN(g_logger) << "battle broadcast push fail n=" << account_ids.size()
                        << " msg_id=" << msg_id << " err=" << ctrl.ErrorText();
                }
            } catch(const std::exception& e) {
                SYLAR_LOG_WARN(g_logger) << "battle broadcast push exception: " << e.what();
            } catch(...) {
                SYLAR_LOG_WARN(g_logger) << "battle broadcast push unknown exception";
            }
        };

        auto impl = std::make_shared<ddt::BattleServiceImpl>(cfg, push, tw, bpush);
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
