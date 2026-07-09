#include <csignal>
#include "sylar/core/log.h"
#include "sylar/scheduler/iomanager.h"
#include "sylar/http/ws_server.h"
#include "sylar/http/http_server.h"
#include "sylar/core/env.h"

#include "resource_servlet.h"
#include "chat_servlet.h"

static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

static chat::ChatWSServlet::ptr s_chat_servlet;

int main(int argc, char** argv) {
    signal(SIGPIPE, SIG_IGN);

    sylar::EnvMgr::GetInstance()->init(argc, argv);

    sylar::IOManager iom(4, true, "chat");

    // HTTP server: port 8090, serves static html
    sylar::http::HttpServer::ptr http_server(new sylar::http::HttpServer(false, &iom, &iom));
    auto addr = sylar::IPAddress::Create("0.0.0.0", 8090);
    if(!http_server->bind(addr)) {
        SYLAR_LOG_ERROR(g_logger) << "http bind fail";
        return 1;
    }
    auto slt_dispatch = http_server->getServletDispatch();
    sylar::http::ResourceServlet::ptr slt(new sylar::http::ResourceServlet(sylar::EnvMgr::GetInstance()->getCwd()));
    slt_dispatch->addGlobServlet("/html/*", slt);
    SYLAR_LOG_INFO(g_logger) << "HTTP server bind on 0.0.0.0:8090";

    // WebSocket server: port 8072, handles chat
    sylar::http::WSServer::ptr ws_server(new sylar::http::WSServer(&iom, &iom));
    auto ws_addr = sylar::IPAddress::Create("0.0.0.0", 8072);
    if(!ws_server->bind(ws_addr)) {
        SYLAR_LOG_ERROR(g_logger) << "ws bind fail";
        return 1;
    }
    s_chat_servlet.reset(new chat::ChatWSServlet);
    auto ws_dispatch = ws_server->getWSServletDispatch();
    ws_dispatch->addServlet("/sylar/chat",
        std::bind(&chat::ChatWSServlet::handle, s_chat_servlet.get(), std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
        std::bind(&chat::ChatWSServlet::onConnect, s_chat_servlet.get(), std::placeholders::_1, std::placeholders::_2),
        std::bind(&chat::ChatWSServlet::onClose, s_chat_servlet.get(), std::placeholders::_1, std::placeholders::_2));
    SYLAR_LOG_INFO(g_logger) << "WS server bind on 0.0.0.0:8072";

    http_server->start();
    ws_server->start();

    SYLAR_LOG_INFO(g_logger) << "chat_room server started";
    return 0;
}
