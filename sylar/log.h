#ifndef __SYLAR_LOG_H__
#define __SYLAR_LOG_H__

#include <list>
#include <memory>
#include <stdint.h>
#include <string>
#include <sstream>
#include <fstream>
#include <vector>
#include <stdarg.h>
#include <map>
#include "singleton.h"
#include "util.h"
#include "thread.h"

// 日志宏定义
#define SYLAR_LOG_LEVEL(logger, level) \
  if(logger->getLevel() <= level) \
    sylar::LogEventWrap(sylar::LogEvent::ptr(new sylar::LogEvent(logger, level, __FILE__, __LINE__, 0, sylar::GetThreadId(), sylar::GetFiberId(), time(0)))).getSS()

#define SYLAR_LOG_DEBUG(logger) SYLAR_LOG_LEVEL(logger, sylar::LogLevel::DEBUG)
#define SYLAR_LOG_INFO(logger)  SYLAR_LOG_LEVEL(logger, sylar::LogLevel::INFO)
#define SYLAR_LOG_WARN(logger)  SYLAR_LOG_LEVEL(logger, sylar::LogLevel::WARN)
#define SYLAR_LOG_ERROR(logger)  SYLAR_LOG_LEVEL(logger, sylar::LogLevel::ERROR)
#define SYLAR_LOG_FATAL(logger) SYLAR_LOG_LEVEL(logger, sylar::LogLevel::FATAL)

// 格式化日志内容的宏，使用可变参数，支持类似printf的格式化输出
#define SYLAR_LOG_FMT_LEVEL(logger, level, fmt, ...) \
    if(logger->getLevel() <= level) sylar::LogEventWrap(sylar::LogEvent::ptr(new sylar::LogEvent(logger, level, __FILE__, __LINE__, 0, sylar::GetThreadId(), sylar::GetFiberId(), time(0)))).getEvent()->format(fmt, __VA_ARGS__)

#define SYLAR_LOG_FMT_DEBUG(logger, fmt, ...) SYLAR_LOG_FMT_LEVEL(logger, sylar::LogLevel::DEBUG, fmt, __VA_ARGS__)
#define SYLAR_LOG_FMT_INFO(logger, fmt, ...)  SYLAR_LOG_FMT_LEVEL(logger, sylar::LogLevel::INFO, fmt, __VA_ARGS__)
#define SYLAR_LOG_FMT_WARN(logger, fmt, ...)  SYLAR_LOG_FMT_LEVEL(logger, sylar::LogLevel::WARN, fmt, __VA_ARGS__)
#define SYLAR_LOG_FMT_ERROR(logger, fmt, ...) SYLAR_LOG_FMT_LEVEL(logger, sylar::LogLevel::ERROR, fmt, __VA_ARGS__)
#define SYLAR_LOG_FMT_FATAL(logger, fmt, ...) SYLAR_LOG_FMT_LEVEL(logger, sylar::LogLevel::FATAL, fmt, __VA_ARGS__)

#define SYLAR_LOG_ROOT() sylar::LoggerMgr::GetInstance()->getRoot()  // 获取全局的root日志器
#define SYLAR_LOG_NAME(name) sylar::LoggerMgr::GetInstance()->getLogger(name)  // 获取指定名称的日志器

namespace sylar {

class Logger;
class LoggerManager;

// 日志级别
class LogLevel {
public:
  enum Level { 
    UNKNOW = 0, 
    DEBUG = 1, 
    INFO = 2, 
    WARN = 3, 
    ERROR = 4, 
    FATAL = 5 
  };
  
  // 日志级别 - 字符串转换
  static const char* toString(LogLevel::Level level);
  static LogLevel::Level fromString(const std::string& str);
};


// 日志事件
class LogEvent {
public:
  typedef std::shared_ptr<LogEvent> ptr;
  LogEvent(std::shared_ptr<Logger> logger, LogLevel::Level level, const char* file, int32_t line, uint32_t elapse, uint32_t thread_id, uint32_t fiber_id, uint64_t time);

  // 获取日志事件信息
  const char *getFile() const { return m_file; }
  int32_t getLine() const { return m_line; }
  uint32_t getElapse() const { return m_elapse; }
  uint32_t getThreadId() const { return m_threadId; }
  uint32_t getFiberId() const { return m_fiberId; }
  uint64_t getTime() const { return m_time; }
  std::string getContent() const { return m_ss.str(); }
  std::shared_ptr<Logger> getLogger() const { return m_logger; }
  LogLevel::Level getLevel() const { return m_level; }

  std::stringstream& getSS() { return m_ss; }
  void format(const char* fmt, ...);         // 格式化日志内容
  void format(const char* fmt, va_list al);  // 格式化日志内容，va_list版本
  
private:
  // 日志事件包含的信息
  const char *m_file = nullptr; // 文件名
  int32_t m_line = 0;           // 行号
  uint32_t m_elapse = 0;       // 程序运行时间
  uint32_t m_threadId = 0;      // 线程ID
  uint32_t m_fiberId = 0;       // 协程ID
  uint64_t m_time = 0;          // 时间戳
  std::stringstream m_ss;        // 日志内容流

  std::shared_ptr<Logger> m_logger;  // 日志事件所属的日志器
  LogLevel::Level m_level;              // 日志级别
};

// RAII封装的日志事件包装器，构造时传入LogEvent对象，析构时自动将日志事件内容输出
class LogEventWrap {
public:
    LogEventWrap(LogEvent::ptr e);
    ~LogEventWrap();
    std::stringstream& getSS();
    LogEvent::ptr getEvent() { return m_event; }

private:
    LogEvent::ptr m_event;
};


// 日志格式器
class LogFormatter {
public:
  typedef std::shared_ptr<LogFormatter> ptr;
  LogFormatter(const std::string& pattern);
  void init();        // 解析日志格式模板，生成日志格式项
  bool isError() const { return m_error; }  //  解析日志格式模板是否出错
  const std::string getPattern() const { return m_pattern; }  // 获取日志格式模板

  std::string format(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event); // 负责把LogEvent中的信息格式化成字符串
public:
  // 日志格式项
  class FormatItem{
    public:
      typedef std::shared_ptr<FormatItem> ptr;
      virtual ~FormatItem() {}
      virtual void format(std::ostream& os, std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) = 0;
  };

private:
  std::string m_pattern;                 // 日志格式模板
  std::vector<FormatItem::ptr> m_items;  // 存储解析后的日志格式项
  bool m_error = false;                  // 解析日志格式模板是否出错
};

// 日志输出地
class LogAppender {
  friend class Logger;
public:
  typedef std::shared_ptr<LogAppender> ptr;
  typedef Spinlock MutexType;
  virtual ~LogAppender() {};

  virtual void log(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) = 0;  // 纯虚函数，子类必须实现
  virtual std::string toYamlString() = 0;

  // 设置获取格式器
  void setFormatter(LogFormatter::ptr formatter){
    MutexType::Lock lock(m_mutex);
    m_formatter = formatter;
    if(m_formatter){
        m_hasFormatter = true;
    } else {
        m_hasFormatter = false;
    }
  }
  
  LogFormatter::ptr getFormatter() { 
    MutexType::Lock lock(m_mutex);
    return m_formatter; 
  }

  LogLevel::Level getLevel() const { return m_level; }
  void setLevel(LogLevel::Level level) { m_level = level; }

protected:  // 子类可以访问
  LogLevel::Level m_level = LogLevel::DEBUG;  // 日志级别
  LogFormatter::ptr m_formatter;              // 格式器
  bool m_hasFormatter = false;               // 是否有格式器
  MutexType m_mutex;
};


// 日志器
class Logger : public std::enable_shared_from_this<Logger> {
  friend class LoggerManager;
public:
  typedef std::shared_ptr<Logger> ptr;
  typedef Spinlock MutexType;

  Logger(const std::string &name = "root");
  
  // 记录日志，根据日志级别判断是否输出
  void log(LogLevel::Level level, LogEvent::ptr event);
  
  // 日志级别
  void debug(LogEvent::ptr event);
  void info(LogEvent::ptr event);
  void warn(LogEvent::ptr event);
  void error(LogEvent::ptr event);
  void fatal(LogEvent::ptr event);
  
  // 添加删除清空日志输出地
  void addAppender(LogAppender::ptr appender);
  void delAppender(LogAppender::ptr appender);
  void clearAppenders();
  
  // 获取设置日志级别
  LogLevel::Level getLevel() const { return m_level; }
  void setLevel(LogLevel::Level level) { m_level = level; }

  // 获取日志名称
  const std::string& getName() const { return m_name; }

  // 获取设置日志格式器
  void setFormatter(LogFormatter::ptr val);
  void setFormatter(const std::string& val);
  LogFormatter::ptr getFormatter();
  std::string toYamlString();
  
private:
  std::string m_name;                      // 日志器名称
  LogLevel::Level m_level;                 // 只有满足日志级别的才会输出
  std::list<LogAppender::ptr> m_appenders; // 日志输出地集合
  LogFormatter::ptr m_formatter;           // 日志格式器
  Logger::ptr m_root;                      // 根日志器，默认的日志输出地
  MutexType m_mutex;
};


// 定义输出到控制台的Appender
class StdoutLogAppender : public LogAppender {
public:
  typedef std::shared_ptr<StdoutLogAppender> ptr;
  void log(Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) override;
  std::string toYamlString() override;
};


// 定义输出到文件的Appender
class FileLogAppender : public LogAppender {
public:
  typedef std::shared_ptr<FileLogAppender> ptr;
  FileLogAppender(const std::string &filename);  // 与控制台输出不同，文件输出需要指定文件名
  void log(Logger::ptr logger,LogLevel::Level level, LogEvent::ptr event) override;

  std::string toYamlString() override;
  // 重新打开文件
  bool reopen();
private:
  std::string m_filename; // 文件名
  std::ofstream m_filestream;
  uint64_t m_lastTime = 0;
};


// 日志管理器，负责管理多个日志器
class LoggerManager {
public:
  typedef Spinlock MutexType;
  LoggerManager();
  void init();

  Logger::ptr getLogger(const std::string& name);
  Logger::ptr getRoot() const { return m_root; }

  std::string toYamlString();
private:
  std::map<std::string, Logger::ptr> m_loggers;
  Logger::ptr m_root;
  MutexType m_mutex;
};
typedef sylar::Singleton<LoggerManager> LoggerMgr;  // 定义一个LoggerManager的单例，方便全局访问

} // namespace sylar

#endif // __SYLAR_LOG_H__