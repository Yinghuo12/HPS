#include <iostream>
#include <thread>
#include "../sylar/log.h"
#include "../sylar/util.h"

int main(){
    sylar::Logger::ptr logger(new sylar::Logger);
    logger->addAppender(sylar::LogAppender::ptr(new sylar::StdoutLogAppender));

    sylar::FileLogAppender::ptr file_appender(new sylar::FileLogAppender("./log.txt"));
    sylar::LogFormatter::ptr fmt(new sylar::LogFormatter("%d%T%p%T%m%n"));
    file_appender->setFormatter(fmt);

    file_appender->setLevel(sylar::LogLevel::ERROR);  // 设置日志级别为ERROR，只有ERROR级别以上的日志才会输出到文件
    logger->addAppender(file_appender);
    
    // sylar::LogEvent::ptr event(new sylar::LogEvent(__FILE__, __LINE__, 0, sylar::GetThreadId(), sylar::GetFiberId(), time(0))); 
    // event->getSS() << "hello sylar log"; // 拿到日志事件的stringstream流，写入日志内容hello sylar log
    // logger->log(sylar::LogLevel::DEBUG, event);


    SYLAR_LOG_INFO(logger) << "test macro";
    SYLAR_LOG_ERROR(logger) << "test macro error";
    SYLAR_LOG_FMT_ERROR(logger, "test macro fmt error %s", "aa");


    auto logger_singleton = sylar::LoggerMgr::GetInstance()->getLogger("xx");
    SYLAR_LOG_INFO(logger_singleton) << "test xxx";
    return 0;
}