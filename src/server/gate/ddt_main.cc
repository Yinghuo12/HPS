#include <csignal>
#include <atomic>
#include <unistd.h>
#include "sylar/core/log.h"
#include "sylar/scheduler/iomanager.h"
#include "sylar/http/ws_server.h"
#include "sylar/core/env.h"
#include "ddt_servlet.h"
#include "ddt_database.h"
#include "ddt_chat_manager.h"
#include "ddt_config.h"

static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();
static ddt::DDTWSServlet::ptr s_ddt_servlet;

// 1. 使用原子布尔变量作为退出标记。
// 信号处理函数必须保持绝对的简洁，不能有任何锁、日志和 hook 的系统调用。
static std::atomic<bool> g_quit{false};
static std::atomic<int> g_quitCount{0};

static void gracefulShutdown(int signum) {
    g_quitCount++;
    if (g_quitCount >= 2) {
        // 第二次 Ctrl+C：强制退出
        _exit(0);
    }
    // 第一次：仅仅置位，交给定时器做优雅退出
    g_quit = true;
}

int main(int argc, char** argv) {
    // 忽略向已关闭的 socket 写数据导致的 SIGPIPE 信号
    signal(SIGPIPE, SIG_IGN);
    // 注册终止信号
    signal(SIGINT, gracefulShutdown);
    signal(SIGTERM, gracefulShutdown);
    
    sylar::EnvMgr::GetInstance()->init(argc, argv);

    // === File logging ===
    {
        auto logger = SYLAR_LOG_ROOT();
        sylar::FileLogAppender::ptr file_appender(new sylar::FileLogAppender("logs/ddt_server.log"));
        sylar::LogFormatter::ptr fmt(new sylar::LogFormatter(
            "%d{%Y-%m-%d %H:%M:%S}%T%t%T%F%T%p%T%c%T%f:%l%T%m%n"));
        file_appender->setFormatter(fmt);
        file_appender->setLevel(sylar::LogLevel::DEBUG);
        logger->addAppender(file_appender);
    }

    SYLAR_LOG_INFO(g_logger) << "========================================";
    SYLAR_LOG_INFO(g_logger) << "DDT Server starting...";

    // Load game config
    auto& config = ddt::GameConfig::Instance();
    std::string confPath = "conf/game.yml";
    if (argc > 1) {
        confPath = argv[1];
    } else {
        // 尝试多个路径查找配置文件（支持从不同目录启动）
        const char* candidates[] = {
            "conf/game.yml",
            "../conf/game.yml",
            "../../conf/game.yml",
            nullptr
        };
        for (int i = 0; candidates[i]; ++i) {
            if (access(candidates[i], F_OK) == 0) {
                confPath = candidates[i];
                break;
            }
        }
    }
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

    // 初始化 IOManager（主事件循环）
    sylar::IOManager iom(4, true, "ddt");

    // 2. 将关闭逻辑转移到正常的协程定时器中处理（这是解决死锁和退不出的核心）
    // 添加一个循环定时器（每 100 毫秒检查一次 g_quit）
    iom.addTimer(100, []() {
        if (g_quit) {
            SYLAR_LOG_INFO(g_logger) << "Detect quit signal, shutting down gracefully...";
            if (s_ddt_servlet) {
                // 此时执行在合法的协程上下文中，读写锁和发送 socket 数据都是绝对安全的
                s_ddt_servlet->broadcastShutdown();
            }
            
            // 延迟 500ms，让断开连接的 WS Frame 有时间发送出去，然后退出
            sylar::IOManager::GetThis()->addTimer(500, []() {
                SYLAR_LOG_INFO(g_logger) << "Server exit.";
                _exit(0);
            }, false); // false = 单次定时器
            
            // 将 quit 置回 false，防止这段逻辑在这 500ms 内被触发多次
            g_quit = false; 
        }
    }, true); // true = 循环定时器

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
        
    return 0; // iom 将会阻塞并接管主线程，直到所有协程结束。
}