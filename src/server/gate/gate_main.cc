#include <memory>

#include "gate_server.h"
#include "service_base.h"
#include "sylar/core/log.h"
#include "sylar/rpc/rpc_provider.h"
#include "sylar/scheduler/iomanager.h"
#include "sylar/scheduler/timewheel.h"

static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

int main(int argc, char** argv) {
    ddt::ServiceRunner runner("gate");
    if(!runner.init(argc, argv)) return 1;
    const auto& cfg = runner.config();
    if(cfg.port == 0) const_cast<ddt::ServiceConfig&>(cfg).port = 8100;

    sylar::IOManager iom(4, true, "gate");

    iom.schedule([&]() {
        // 共享时间轮: 心跳检查周期定时器注册其上(O(1) 调度)。
        // steps=1000ms(心跳是秒级), maxMin=60(最长 1 小时, 足够)。
        auto tw = std::make_shared<sylar::TimeWheel>();
        tw->init(1000, 60);
        tw->start(&iom);

        // RPC 连接池: 4 个下游服务统一走多路复用长连接, 替代原短连接模式。
        auto rpcPool = std::make_shared<sylar::rpc::RpcChannelPool>(cfg.etcd_endpoint, 8);

        // 客户端 TCP 入口 (GateServer: 持 session, 处理 msg_id 分发)
        auto gate = std::make_shared<ddt::GateServer>(cfg, cfg.etcd_endpoint, tw);
        gate->setRpcPool(rpcPool);
        gate->setRedis(cfg.redis_host, cfg.redis_port);   // 用于订阅世界聊天
        auto addr = sylar::IPAddress::Create(cfg.host.c_str(), cfg.port);
        if(!gate->bind(addr)) {
            SYLAR_LOG_FATAL(g_logger) << "gate bind fail " << cfg.host << ":" << cfg.port;
            return;
        }
        gate->start();
        gate->startHeartbeatCheck();
        gate->startWorldChatSubscriber();   // 订阅 Redis chat:world, 收到消息广播给本地在线玩家
        SYLAR_LOG_INFO(g_logger) << "gate TCP on " << cfg.host << ":" << cfg.port;

        // PushService RPC (供 lobby/battle 回调推送) — 用 +1 端口
        //    GateServer 同时继承 PushService, 直接注册到 RpcProvider
        auto provider = std::make_shared<sylar::rpc::RpcProvider>();
        provider->setEtcd(cfg.etcd_endpoint, cfg.etcd_ttl);
        provider->setListen((uint16_t)(cfg.port + 1));
        {
            std::string adv = cfg.advertiseAddr();
            auto pos = adv.rfind(':');
            std::string host = (pos == std::string::npos) ? "127.0.0.1" : adv.substr(0, pos);
            provider->setAdvertise(host + ":" + std::to_string(cfg.port + 1));
        }
        provider->notifyService(gate.get());   // gate 实现 PushService
        provider->run();

        static std::shared_ptr<ddt::GateServer> g_keepalive = gate;
        static std::shared_ptr<sylar::TimeWheel> g_tw = tw;
        static std::shared_ptr<sylar::rpc::RpcChannelPool> g_rpcPool = rpcPool;
    });

    runner.installSignal();
    return 0;
}
