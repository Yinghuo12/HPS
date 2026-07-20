#include <memory>

#include "lobby_service.h"
#include "service_base.h"
#include "sylar/core/log.h"
#include "sylar/rpc/rpc_provider.h"
#include "sylar/rpc/rpc_channel.h"
#include "sylar/rpc/rpc_channel_pool.h"
#include "sylar/rpc/rpc_controller.h"
#include "sylar/scheduler/iomanager.h"

static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

int main(int argc, char** argv) {
    ddt::ServiceRunner runner("lobby");
    if (!runner.init(argc, argv)) {
        return 1;
    }
    const auto& cfg = runner.config();
    if (cfg.port == 0) {
        const_cast<ddt::ServiceConfig&>(cfg).port = 8300;
    }

    sylar::IOManager iom(4, true, "lobby");

    iom.schedule([&]() {
        auto impl = std::make_shared<ddt::LobbyServiceImpl>(cfg.etcd_endpoint);

        // RPC 连接池: lobby→gate 推送高频, 用池复用 TCP + etcd 发现缓存
        auto rpcPool = std::make_shared<sylar::rpc::RpcChannelPool>(cfg.etcd_endpoint, 8);

        // 注入推送闭包: lobby -> gate 的 PushService.NotifyClient
        // 异步投到独立协程执行, 避免撑爆 RPC 入口协程栈
        impl->setPushFn([&cfg, rpcPool](const ddt::RoutingHandle& h, uint16_t msg_id, const std::string& payload) {
            sylar::IOManager::GetThis()->schedule([&cfg, rpcPool, h, msg_id, payload]() {
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
                        SYLAR_LOG_WARN(g_logger) << "lobby push fail account=" << h.accountId
                            << " msg_id=" << msg_id << " err=" << ctrl.ErrorText();
                    }
                } catch (const std::exception& e) {
                    SYLAR_LOG_WARN(g_logger) << "lobby push exception: " << e.what();
                }
            });
        });

        // 注入"广播给所有在线玩家"闭包: lobby -> gate.PushService.NotifyAllOnline(世界聊天用)
        impl->setPushAllFn([&cfg, rpcPool](uint16_t msg_id, const std::string& payload) {
            sylar::IOManager::GetThis()->schedule([&cfg, rpcPool, msg_id, payload]() {
                try {
                    sylar::rpc::RpcChannel chan(cfg.etcd_endpoint, rpcPool.get());
                    ddt::PushService::Stub stub(&chan);
                    sylar::rpc::RpcController ctrl;
                    ddt::NotifyReq req;
                    req.set_msg_id(msg_id);
                    req.set_payload(payload);
                    ddt::ResultResp resp;
                    stub.NotifyAllOnline(&ctrl, &req, &resp, nullptr);
                    if (ctrl.Failed()) {
                        SYLAR_LOG_WARN(g_logger) << "lobby pushAll fail msg_id=" << msg_id
                            << " err=" << ctrl.ErrorText();
                    }
                } catch (const std::exception& e) {
                    SYLAR_LOG_WARN(g_logger) << "lobby pushAll exception: " << e.what();
                }
            });
        });

        auto provider = std::make_shared<sylar::rpc::RpcProvider>();
        provider->setEtcd(cfg.etcd_endpoint, cfg.etcd_ttl);
        provider->setListen((uint16_t)cfg.port);
        provider->setAdvertise(cfg.advertiseAddr());
        provider->notifyService(impl.get());
        provider->run();

        static std::shared_ptr<ddt::LobbyServiceImpl> g_keepalive = impl;
    });

    runner.installSignal();
    return 0;
}
