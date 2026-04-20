#include "sylar/http/http_server.h"
#include "sylar/log.h"
#include "sylar/http/servlet.h"

sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

void run() {
    sylar::Address::ptr addr = sylar::Address::LookupAnyIPAddress("0.0.0.0:8020");

    // 正确构造函数
    sylar::http::HttpServer::ptr http_server(new sylar::http::HttpServer(true));

    // 正确注册路径（C++11 标准）
    auto sd = http_server->getServletDispatch();
    sd->addServlet("/", [](sylar::http::HttpRequest::ptr req, sylar::http::HttpResponse::ptr rsp, sylar::http::HttpSession::ptr session) {
        rsp->setBody("hello yinghuo!\n");
        return 200;
    });

    while (!http_server->bind(addr)) {
        sleep(1);
    }
    http_server->start();
}

int main() {
    sylar::IOManager iom(4);
    iom.schedule(run);
    return 0;
}
