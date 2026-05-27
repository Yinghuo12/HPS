#include <csignal>
#include "sylar/log.h"
#include "sylar/iomanager.h"
#include "sylar/http/ws_server.h"
#include "sylar/env.h"
#include "ddt_servlet.h"
#include "ddt_database.h"
#include "ddt_chat_manager.h"
#include "ddt_config.h"

static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

static ddt::DDTWSServlet::ptr s_ddt_servlet;

static void gracefulShutdown(int signum) {
    SYLAR_LOG_INFO(g_logger) << "Received signal " << signum << ", shutting down gracefully";
    if (s_ddt_servlet) {
        s_ddt_servlet->broadcastShutdown();
    }
    // Give clients a moment to receive the message, then exit
    usleep(500000);  // 500ms
    _exit(0);
}

int main(int argc, char** argv) {
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, gracefulShutdown);
    signal(SIGTERM, gracefulShutdown);
    sylar::EnvMgr::GetInstance()->init(argc, argv);

    // Load game config
    auto& config = ddt::GameConfig::Instance();
    std::string confPath = "conf/game.yml";
    if (argc > 1) confPath = argv[1];
    if (!config.load(confPath)) {
        SYLAR_LOG_WARN(g_logger) << "Using default config values";
    }

    // Initialize database
    auto& db = ddt::DDTDatabase::Instance();
    if (!db.init(config.db_host, config.db_port, config.db_user, config.db_pass,
                 config.db_name, config.redis_host, config.redis_port,
                 config.db_pool_size)) {
        SYLAR_LOG_ERROR(g_logger) << "Database init failed, continuing without DB";
    }

    sylar::IOManager iom(4, true, "ddt");

    // WebSocket server
    sylar::http::WSServer::ptr ws_server(new sylar::http::WSServer(&iom, &iom));
    auto ws_addr = sylar::IPAddress::Create(config.host.c_str(), config.port);
    if (!ws_server->bind(ws_addr)) {
        SYLAR_LOG_ERROR(g_logger) << "ws bind fail";
        return 1;
    }

    s_ddt_servlet.reset(new ddt::DDTWSServlet);
    auto ws_dispatch = ws_server->getWSServletDispatch();
    ws_dispatch->addServlet("/ddt/game",
        std::bind(&ddt::DDTWSServlet::handle, s_ddt_servlet.get(),
                  std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
        std::bind(&ddt::DDTWSServlet::onConnect, s_ddt_servlet.get(),
                  std::placeholders::_1, std::placeholders::_2),
        std::bind(&ddt::DDTWSServlet::onClose, s_ddt_servlet.get(),
                  std::placeholders::_1, std::placeholders::_2));

    ws_server->start();
    SYLAR_LOG_INFO(g_logger) << "DDT game server started on "
        << config.host << ":" << config.port;
    return 0;
}
