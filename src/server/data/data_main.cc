#include <memory>

#include "data_service.h"
#include "service_base.h"
#include "sylar/core/log.h"
#include "sylar/rpc/rpc_provider.h"
#include "sylar/scheduler/iomanager.h"

static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

int main(int argc, char** argv) {
    ddt::ServiceRunner runner("data");
    if(!runner.init(argc, argv)) return 1;
    const auto& cfg = runner.config();
    if(cfg.port == 0) {
        // 无配置则用默认
        const_cast<ddt::ServiceConfig&>(cfg).port = 8500;
    }

    sylar::IOManager iom(4, true, "data");

    iom.schedule([&]() {
        auto impl = std::make_shared<ddt::DataServiceImpl>();
        if(!impl->init(cfg.db_host, cfg.db_port, cfg.db_user, cfg.db_pass,
                       cfg.db_name, cfg.db_pool_size, cfg.redis_host, cfg.redis_port,
                       cfg.redis_pool_size)) {
            SYLAR_LOG_FATAL(g_logger) << "data: init backend fail";
            return;
        }
        auto provider = std::make_shared<sylar::rpc::RpcProvider>();
        provider->setEtcd(cfg.etcd_endpoint, cfg.etcd_ttl);
        provider->setListen((uint16_t)cfg.port);
        provider->setAdvertise(cfg.advertiseAddr());
        provider->notifyService(impl.get());
        provider->run();
        // impl 生命周期: provider 仅持有裸指针; 用一个静态 shared_ptr 保活
        static std::shared_ptr<ddt::DataServiceImpl> g_keepalive = impl;
    });

    runner.installSignal();
    return 0;
}
