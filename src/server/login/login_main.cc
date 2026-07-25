#include <memory>

#include "login_service.h"
#include "service_base.h"
#include "sylar/core/log.h"
#include "sylar/http/http.h"
#include "sylar/http/http_server.h"
#include "sylar/rpc/rpc_provider.h"
#include "sylar/scheduler/iomanager.h"
#include "sylar/util/json_util.h"

static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

int main(int argc, char** argv) {
    ddt::ServiceRunner runner("login");
    if(!runner.init(argc, argv)) return 1;
    const auto& cfg = runner.config();
    if(cfg.port == 0) const_cast<ddt::ServiceConfig&>(cfg).port = 8200;

    sylar::IOManager iom(4, true, "login");

    iom.schedule([&]() {
        // RPC 连接池: login→data 调用走多路复用长连接, 替代原短连接。
        // 消除栈溢出根因(原每次 RPC 新建 EtcdClient 重对象 + 短连接)。
        auto rpcPool = std::make_shared<sylar::rpc::RpcChannelPool>(cfg.etcd_endpoint, 8);
        auto impl = std::make_shared<ddt::LoginServiceImpl>(cfg.etcd_endpoint);
        impl->setRpcPool(rpcPool);
        // 1) HTTP /login /register (供客户端明文登录)
        sylar::http::HttpServer::ptr http(new sylar::http::HttpServer(false, &iom, &iom));
        http->getServletDispatch()->addServlet("/login",
            [impl](sylar::http::HttpRequest::ptr req, sylar::http::HttpResponse::ptr rsp,
                   sylar::http::HttpSession::ptr) -> int32_t {
                Json::Value body;
                sylar::JsonUtil::FromString(body, req->getBody());
                std::string name = body.get("name", "").asString();
                std::string pwd  = body.get("password", "").asString();
                std::string out = impl->handleHttpLogin(name, pwd);
                rsp->setBody(out);
                rsp->setHeader("content-type", "application/json");
                rsp->setStatus(sylar::http::HttpStatus::OK);
                return 0;
            });
        http->getServletDispatch()->addServlet("/register",
            [impl](sylar::http::HttpRequest::ptr req, sylar::http::HttpResponse::ptr rsp,
                   sylar::http::HttpSession::ptr) -> int32_t {
                Json::Value body;
                sylar::JsonUtil::FromString(body, req->getBody());
                std::string name = body.get("name", "").asString();
                std::string pwd  = body.get("password", "").asString();
                std::string out = impl->handleHttpRegister(name, pwd);
                rsp->setBody(out);
                rsp->setHeader("content-type", "application/json");
                rsp->setStatus(sylar::http::HttpStatus::OK);
                return 0;
            });
        auto addr = sylar::IPAddress::Create(cfg.host.c_str(), cfg.port);
        if(!http->bind(addr)) {
            SYLAR_LOG_FATAL(g_logger) << "login http bind fail " << cfg.host << ":" << cfg.port;
            return;
        }
        http->start();
        SYLAR_LOG_INFO(g_logger) << "login http on " << cfg.host << ":" << cfg.port;

        // 2) RPC LoginService (供 gate 调用 ValidateToken/Register/Login)
        // 用 +1 端口做 RPC, 与 HTTP 分开
        auto provider = std::make_shared<sylar::rpc::RpcProvider>();
        provider->setEtcd(cfg.etcd_endpoint, cfg.etcd_ttl);
        provider->setListen((uint16_t)(cfg.port + 1));
        // advertise 地址: 用配置推导, 端口取 RPC 端口(port+1)
        {
            std::string adv = cfg.advertiseAddr();
            auto pos = adv.rfind(':');
            std::string host = (pos == std::string::npos) ? "127.0.0.1" : adv.substr(0, pos);
            provider->setAdvertise(host + ":" + std::to_string(cfg.port + 1));
        }
        provider->notifyService(impl.get());
        provider->run();

        static std::shared_ptr<ddt::LoginServiceImpl> g_keepalive = impl;
        static std::shared_ptr<sylar::rpc::RpcChannelPool> g_rpcPool = rpcPool;
    });

    runner.installSignal();
    return 0;
}
