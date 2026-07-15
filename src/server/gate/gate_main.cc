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

        // 客户端 TCP 入口 (GateServer: 持 session, 处理 msg_id 分发)
        // 注: 下游 RPC channel 用短连接(无连接池)——连接池在 sylar hook 模型下
        //     存在 fd 复用竞态(addEvent assert), 已回退。
        auto gate = std::make_shared<ddt::GateServer>(cfg, cfg.etcd_endpoint, tw);
        auto addr = sylar::IPAddress::Create(cfg.host.c_str(), cfg.port);
        if(!gate->bind(addr)) {
            SYLAR_LOG_FATAL(g_logger) << "gate bind fail " << cfg.host << ":" << cfg.port;
            return;
        }
        gate->start();
        gate->startHeartbeatCheck();
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
        static std::shared_ptr<sylar::TimeWheel> g_tw = tw;   // 保活时间轮
    });

    runner.installSignal();
    return 0;
}
