#include "log.h"
#include <map>
#include <iostream>
#include <functional>
#include <time.h>
#include <string.h>

namespace sylar {

// 将日志级别转换为字符串，方便打印日志时显示日志级别
const char* LogLevel::toString(LogLevel::Level level) {
    switch(level){

// 带参宏定义， # + 宏参数 = 直接把参数变成双引号包裹的字符串  name = DEBUG， #name = "DEBUG", 
#define XX(name) case LogLevel::name: return #name; break;  
        XX(DEBUG);
        XX(INFO);
        XX(WARN);
        XX(ERROR);
        XX(FATAL);
#undef XX
    default:
        return "UNKNOW";
    }
    return "UNKNOW";
}


// RAII封装的日志事件包装器，构造时传入LogEvent对象，析构时自动将日志事件内容输出   
LogEventWrap::LogEventWrap(LogEvent::ptr e)
    :m_event(e) {
}

LogEventWrap::~LogEventWrap() {
    m_event->getLogger()->log(m_event->getLevel(), m_event);
}

std::stringstream& LogEventWrap::getSS() {
    return m_event->getSS();
}
// RAII end


/*  格式化输出 例如 format("姓名=%s 年龄=%d", "张三", 20) 会把日志内容格式化为 "姓名=张三 年龄=20" 需要stdarg.h头文件支持可变参数  */
void LogEvent::format(const char* fmt, ...) {
    va_list al;
    va_start(al, fmt);
    format(fmt, al);  // 调用的是va_list版本的format函数 而不是递归调用自己
    va_end(al);
}

// 格式化输出，va_list版本
void LogEvent::format(const char* fmt, va_list al) {
    char* buf = nullptr;
    int len = vasprintf(&buf, fmt, al);   // 分配了内存，需要调用free(buf)释放内存
    if(len != -1) {
        m_ss << std::string(buf, len);
        free(buf);   // 释放内存
    }
}
/* 格式化输出 end */

class MessageFormatItem : public LogFormatter::FormatItem {
public:
    MessageFormatItem(const std::string& str = ""){}
    void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) override{
        os << event->getContent();
    }
};

class LevelFormatItem : public LogFormatter::FormatItem {
public:
    LevelFormatItem(const std::string& str = ""){}
    void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) override{
        os << LogLevel::toString(level);
    }
};

class ElapseFormatItem : public LogFormatter::FormatItem {
public:
    ElapseFormatItem(const std::string& str = ""){}
    void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) override{
        os << event->getElapse();
    }
};

class NameFormatItem : public LogFormatter::FormatItem {
public:
    NameFormatItem(const std::string& str = ""){}
    void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) override{
        os << logger->getName();
    }
};

class ThreadIdFormatItem : public LogFormatter::FormatItem {
public:
    ThreadIdFormatItem(const std::string& str = ""){}
    void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) override{
        os << event->getThreadId();
    }
};

class FiberIdFormatItem : public LogFormatter::FormatItem {
public:
    FiberIdFormatItem(const std::string& str = ""){}
    void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) override{
        os << event->getFiberId();
    }
};

class DateTimeFormatItem : public LogFormatter::FormatItem {
public:
    DateTimeFormatItem(const std::string& format = "%Y-%m-%d %H:%M:%S")
        :m_format(format) {
            if(m_format.empty()) {
                m_format = "%Y-%m-%d %H:%M:%S";
            }
        }
    void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) override{
        struct tm tm;   // 时间结构体 tm_year从1900开始，tm_mon从0开始，tm_mday从1开始
        time_t time = event->getTime();   // 获取时间戳 这个数字是：从 1970-01-01 到现在的秒数
        localtime_r(&time, &tm);          // 将时间戳转换为tm结构体，localtime_r是线程安全的版本，localtime是非线程安全的版本
        char buf[64];
        strftime(buf, sizeof(buf), m_format.c_str(), &tm);  // 将tm结构体格式化为字符串，格式由m_format指定，例如"%Y-%m-%d %H:%M:%S", 这些%Y、%m、%d是系统内部早就写好的固定对应关系。
        os << buf;
    }
private:
    std::string m_format;
};

class FilenameFormatItem : public LogFormatter::FormatItem {
public:
    FilenameFormatItem(const std::string& str = ""){}
    void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) override{
        os << event->getFile();
    }
};

class LineFormatItem : public LogFormatter::FormatItem {
public:
    LineFormatItem(const std::string& str = ""){}
    void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) override{
        os << event->getLine();
    }
};

class NewLineFormatItem : public LogFormatter::FormatItem {
public:
    NewLineFormatItem(const std::string& str = ""){}
    void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) override{
        os << std::endl;
    }
};

class StringFormatItem : public LogFormatter::FormatItem {
public:
    StringFormatItem(const std::string& str) : m_string(str) {}
    void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) override{
        os << m_string;
    }
private: 
    std::string m_string;
};


class TabFormatItem : public LogFormatter::FormatItem {
public:
    TabFormatItem(const std::string& str = "") {}
    void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) override {
        os << "\t";
    }
private:
    std::string m_string;
};

// const char *m_file = nullptr; // 文件名
// int32_t m_line = 0;           // 行号
// uint32_t m_elapsed = 0;       // 程序运行时间
// uint32_t m_threadId = 0;      // 线程ID
// uint32_t m_fiberId = 0;       // 协程ID
// uint64_t m_time = 0;          // 时间戳
// std::string m_content;        // 日志内容流

LogEvent::LogEvent(std::shared_ptr<Logger> logger, LogLevel::Level level, const char* file, int32_t line, uint32_t elapse, uint32_t thread_id, uint32_t fiber_id, uint64_t time)
    :m_file(file)
    ,m_line(line)
    ,m_elapse(elapse)
    ,m_threadId(thread_id)
    ,m_fiberId(fiber_id)
    ,m_time(time)
    ,m_logger(logger)
    ,m_level(level) {

}


Logger::Logger(const std::string &name) : m_name(name), m_level(LogLevel::DEBUG){ // 默认最低级别，可以打印所有日志 
    // 默认日志格式, %d:时间 (%Y-%m-%d %H:%M:%S) %T:Tab %t:线程id %F:协程id %N:线程名称 %p:日志级别 %c:日志名称 %f:文件名 %l:行号 %m:日志内容 %n:换行
    // 形式如下：2024-06-01 12:00:00    12345   67890   [DEBUG] [root] test.cpp:10    Hello World
    m_formatter.reset(new LogFormatter("%d{%Y-%m-%d %H:%M:%S}%T%t%T%F%T[%p]%T[%c]%T%f:%l%T%m%n"));
}

void Logger::addAppender(LogAppender::ptr appender) {
    // 如果appender没有设置格式器，则使用日志器的格式器
    if(!appender->getFormatter()) {
        appender->setFormatter(m_formatter);
    }
    m_appenders.push_back(appender);
}

void Logger::delAppender(LogAppender::ptr appender) {
    for(auto it = m_appenders.begin(); it != m_appenders.end(); ++it) {
        if(*it == appender) {
            m_appenders.erase(it);
            break;
        }
    }
}

void Logger::log(LogLevel::Level level, LogEvent::ptr event) {
    if(level >= m_level) {
        auto self = shared_from_this();  // 获取当前Logger的shared_ptr智能指针，方便传递给LogAppender使用
        for(auto &appender : m_appenders) {
            appender->log(self, level,event);
        }
    }
}

void Logger::debug(LogEvent::ptr event) { log(LogLevel::DEBUG, event); }
void Logger::info(LogEvent::ptr event) { log(LogLevel::INFO, event); }
void Logger::warn(LogEvent::ptr event) { log(LogLevel::WARN, event); }
void Logger::error(LogEvent::ptr event) { log(LogLevel::ERROR, event); }
void Logger::fatal(LogEvent::ptr event) { log(LogLevel::FATAL, event); }


// 输出文件Appender
FileLogAppender::FileLogAppender(const std::string &filename) : m_filename(filename) {
    reopen();   // 打开文件
}

void FileLogAppender::log(Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) {
    if(level >= m_level) {  // 满足日志级别的才会输出
        m_filestream << m_formatter->format(logger, level, event);
    }
}

bool FileLogAppender::reopen() {
    if(m_filestream) {
        m_filestream.close();
    }
    m_filestream.open(m_filename);
    return !!m_filestream;   // 非0值都转为1
}


// 输出文控制台Appender
void StdoutLogAppender::log(Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) {
     if (level >= m_level) {
        std::cout << m_formatter->format(logger, level, event);
        // std::string str = m_formatter->format(logger, level, event);
        // std::cout << str << "*****" << std::endl;
     } 
}

LogFormatter::LogFormatter(const std::string& pattern) : m_pattern(pattern) {
        init();  // 解析格式模板，生成日志格式项
    }

std::string LogFormatter::format(Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) {
    std::stringstream ss;
    for(auto& i : m_items){
        i->format(ss, logger, level, event);
    }
    return ss.str();
}



// 解析 %x %x{xxx} %% 格式
// m_pattern "%d{%Y-%m-%d %H:%M:%S}%T%t%T%N%T%F%T[%p]%T[%c]%T%f:%l%T%m%n" 
void LogFormatter::init(){
    std::vector<std::tuple<std::string, std::string, int> > vec; // 数组存放 <str, format, type> 字符串内容, 格式化信息, 类型标记 
    std::string normal_str;   // 存放普通字符串

    for(size_t i = 0; i < m_pattern.size(); ++i) {
        // 如果不是%，则说明是普通字符串，直接添加到normal_str中，直到遇到%为止
        if(m_pattern[i] != '%') {
            normal_str.append(1, m_pattern[i]);
            continue;
        }
        // 如果遇到了%，则检查下一个字符是否也是%,处理%%转义情况, 如果下一个也是%，则第二个%被视为普通字符，添加到normal_str中，并跳过当前%继续处理后续字符 
        if((i + 1) < m_pattern.size()) {
            if(m_pattern[i + 1] == '%') {
                normal_str.append(1, '%');
                continue;
            }  
        }
        // 如果遇到%且下一个字符不是%，则说明这是一个占位符的开始，需要解析占位符和格式信息 
        // 解析%x{xxx}格式   即 %占位符{占位符参数}
        size_t n = i + 1;    // n是占位符的第一个字符位置，例如%d{xxx}，n指向d
        int fmt_status = 0;  // 0=解析占位符名，1=解析{}内的占位符参数 
        size_t fmt_begin = 0;

        std::string str;  // 占位符名（d/p/m等)
        std::string fmt;  // 占位符参数（YYYYMMDD）
        
        // 从当前占位符结束位置n开始循环解析占位符和{}里面的占位符参数
        while(n < m_pattern.size()) {
            // 解析占位符
            // 这里的日志库只支持单个字母占位符解析  %后面只能跟字母（d/p/m/t/c...）一旦遇到非字母，就代表这个占位符的名字结束了
                //%d{YYMMDD} → d后面是{ → 继续（读参数）
                //%d → d后面是空格 → 结束
                //%m%s → m后面是% → 结束
                //%d: → d后面是: → 结束
            if(!fmt_status && (!isalpha(m_pattern[n])) && m_pattern[n] != '{' && m_pattern[n] != '}') {  // 遇到非字母、非{、非} 字符，, 要break循环添加占位符到vec中。 !fmt_status表示大括号外面严格解析，大括号里面无脑读取
                // i是%字符位置，n是占位符结束位置，要截取%后面和{前面的字符串作为占位符名，例如%d{xxx} -> d
                str = m_pattern.substr(i + 1, n - (i + 1));  // 保存占位符名, 起始位置是i+1，长度是n-(i+1), 本日志库中占位符名只能是单个字母，所以长度一般是1
                break;
            }
        
            // 如果没有break掉, 则说明是字母，"{""  或者  "}" ，继续解析 
            // 如果占位符后面是{，则继续解析{}内的参数，直到遇到}结束
            if(fmt_status == 0) {
                if(m_pattern[n] == '{') {
                    str = m_pattern.substr(i + 1, n - (i + 1));  // d{xxx} -> d
                    // std::cout << "*" << str << std::endl;
                    fmt_status = 1;   // 解析{}参数状态，下次循环跳过这段if，直接解析{}内的参数
                    fmt_begin = n;    // 记录{位置
                    ++n;              // 从{后面开始，跳出循环，解析 { 后面 到 } 前面的参数
                    continue;
                }
            } else if(fmt_status == 1) { // 解析 } 结束参数
                // 如果不是}，则不进入if，继续解析{}内的参数，直到遇到}结束
                if(m_pattern[n] == '}'){
                    fmt = m_pattern.substr(fmt_begin + 1, n - (fmt_begin + 1));  // 提取{}内的参数，例如%d{YYYYMMDD} -> YYYYMMDD
                    // std::cout << "*" << str << std::endl;
                    fmt_status = 0;   // 解析完成，重置状态
                    ++n;              // 跳过}继续处理后续字符
                    break;            // 解析完成，跳出循环添加普通字符串和占位符到vec中
                }
            }
            ++n;  // 继续解析
            if(n == m_pattern.size()){  // 最后一个字符
                if(str.empty()){
                    str = m_pattern.substr(i+1);  // 如果str为空，说明占位符只有一个%，没有占位符名，直接截取%后面的字符串作为占位符名
                }
            }
        }


        // 解析完成后，根据解析结果将普通字符串和格式化字符串添加到vec中
        if(fmt_status == 0) {
            if(!normal_str.empty()){
                // 存入普通文本，标志类型0
                vec.push_back(std::make_tuple(normal_str, "", 0));  // 第一轮：( "", "", 0 )
                normal_str.clear();
            }
            // 存入占位符，标志类型1
            vec.push_back(std::make_tuple(str, fmt, 1));   // 类型1表示占位符  第一轮：( "d", "%Y-%m-%d %H:%M:%S", 1 )
            i = n - 1;  // 跳过已解析的字符，让i指向当前处理的字符，下个for循环会++i处理下个字符
        } else if(fmt_status == 1) {
            std::cout << "pattern parse error: " << m_pattern << " - " << m_pattern.substr(i) << std::endl;
            vec.push_back(std::make_tuple("<<pattern_error>>", fmt, 0));      
        }
    }
    if(!normal_str.empty()){
        vec.push_back(std::make_tuple(normal_str, "", 0));
    }

    // 占位符信息与FormatItem对象映射
    static std::map<std::string, std::function<FormatItem::ptr(const std::string& str)> > s_format_items = {
#define XX(str, C) \
    {#str, [](const std::string& fmt) { return FormatItem::ptr(new C(fmt));}}

    XX(m, MessageFormatItem),           //m:消息
    XX(p, LevelFormatItem),             //p:日志级别
    XX(r, ElapseFormatItem),            //r:累计毫秒数
    XX(c, NameFormatItem),              //c:日志名称
    XX(t, ThreadIdFormatItem),          //t:线程id
    XX(n, NewLineFormatItem),           //n:换行
    XX(d, DateTimeFormatItem),          //d:时间
    XX(f, FilenameFormatItem),          //f:文件名
    XX(l, LineFormatItem),              //l:行号
    XX(T, TabFormatItem),               //T:Tab
    XX(F, FiberIdFormatItem),           //F:协程id
    // XX(N, ThreadNameFormatItem),        //N:线程名称
#undef XX
    };

    for(auto& i : vec) {
        if(std::get<2>(i) == 0){  // 若type为0
            // 将解析出的FormatItem放到m_items中
            m_items.push_back(FormatItem::ptr(new StringFormatItem(std::get<0>(i))));
        } else {                  // type为1
            auto it = s_format_items.find(std::get<0>(i));   // 从map中找到相应的FormatItem
            if(it == s_format_items.end()) {                 // 若没有找到则用StringFormatItem显示错误信息 并设置错误标志位
                m_items.push_back(FormatItem::ptr(new StringFormatItem("<<error_format %" + std::get<0>(i) + ">> ")));
            } else {
                // 返回相应格式的FormatItem，其中std::get<1>(i)作为cb的参数
                m_items.push_back(it->second(std::get<1>(i)));
            }
        }
        // std::cout << "{" << std::get<0>(i) << ") - (" << std::get<1>(i) << ") - (" << std::get<2>(i) << "}" << std::endl;
    }
    // std::cout << m_items.size() << std::endl;
}


LoggerManager::LoggerManager() {
    m_root.reset(new Logger);
    m_root->addAppender(LogAppender::ptr(new StdoutLogAppender));
}

Logger::ptr LoggerManager::getLogger(const std::string& name) {
    auto it = m_loggers.find(name);
    return it == m_loggers.end() ? m_root : it->second;
}

} // namespace sylar

