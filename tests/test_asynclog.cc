#include "sylar/sylar.h"
#include "sylar/asynclog.h"
#include <iostream>
#include <vector>
#include <string>

const std::string LOG_DIR = "./logs/";

const int THREAD_COUNT = 4;
const int LOG_COUNT_PER_THREAD = 250000;

// 1. 同步日志压测
void test_sync_logger() {
    sylar::Logger::ptr logger(new sylar::Logger("sync_logger"));
    std::string sync_log_path = LOG_DIR + "sync_test.log";
    sylar::FileLogAppender::ptr file_appender(new sylar::FileLogAppender(sync_log_path));

    sylar::LogFormatter::ptr fmt(new sylar::LogFormatter("%d{%Y-%m-%d %H:%M:%S}%T%t%T[%p]%T[%c]%T%m%n"));
    file_appender->setFormatter(fmt);
    logger->addAppender(file_appender);

    uint64_t start_time = sylar::GetCurrentMS();

    std::vector<sylar::Thread::ptr> thrs;
    for(int i = 0; i < THREAD_COUNT; ++i) {
        thrs.push_back(sylar::Thread::ptr(new sylar::Thread([logger]() {
            for(int j = 0; j < LOG_COUNT_PER_THREAD; ++j) {
                SYLAR_LOG_INFO(logger) << "This is a synchronous log test message. Index: " << j;
            }
        }, "sync_thr")));
    }

    for(auto& i : thrs) {
        i->join();
    }

    uint64_t end_time = sylar::GetCurrentMS();
    double cost_sec = (end_time - start_time) / 1000.0;
    std::cout << "[Sync Logger] 耗时: " << (end_time - start_time) << " ms" << " QPS: " << (THREAD_COUNT * LOG_COUNT_PER_THREAD / cost_sec) << std::endl;
}

// 2. 异步日志压测 (对用户完全透明，无需手动干预线程)
void test_async_logger() {
    sylar::Logger::ptr logger(new sylar::Logger("async_logger"));
    std::string async_log_path = LOG_DIR + "async_test";
    
    // 构造即启动！
    sylar::AsyncLogAppender::ptr async_appender(new sylar::AsyncLogAppender(async_log_path));

    sylar::LogFormatter::ptr fmt(new sylar::LogFormatter("%d{%Y-%m-%d %H:%M:%S}%T%t%T[%p]%T[%c]%T%m%n"));
    async_appender->setFormatter(fmt);
    logger->addAppender(async_appender);

    uint64_t start_time = sylar::GetCurrentMS();

    std::vector<sylar::Thread::ptr> thrs;
    for(int i = 0; i < THREAD_COUNT; ++i) {
        thrs.push_back(sylar::Thread::ptr(new sylar::Thread([logger]() {
            for(int j = 0; j < LOG_COUNT_PER_THREAD; ++j) {
                SYLAR_LOG_INFO(logger) << "This is an asynchronous log test msg. Index: " << j;
            }
        }, "async_thr")));
    }

    for(auto& i : thrs) {
        i->join();
    }

    // 智能指针 async_appender 在函数结束时自动析构，触发 stop()，安全落盘所有数据！
    uint64_t end_time = sylar::GetCurrentMS();
    double cost_sec = (end_time - start_time) / 1000.0;
    std::cout << "[Async Logger] 耗时: " << (end_time - start_time) << " ms" << " QPS: " << (THREAD_COUNT * LOG_COUNT_PER_THREAD / cost_sec) << std::endl;
}

int main() {
    system(("mkdir -p " + LOG_DIR).c_str());
    std::cout << "Starting benchmark (Total 1,000,000 lines)..." << std::endl;
    
    test_sync_logger();
    test_async_logger();

    return 0;
}