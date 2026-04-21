#ifndef __SYLAR_ASYNC_LOG_H__
#define __SYLAR_ASYNC_LOG_H__

#include "log.h"
#include "thread.h"
#include "noncopyable.h"
#include <vector>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <string.h>

namespace sylar {

// 固定大小的内存缓冲区，用于极速暂存日志数据
const int kSmallBuffer = 4000;         // 4KB
const int kLargeBuffer = 4000 * 1000;  // 4MB

template <int SIZE>
class FixedBuffer : Noncopyable {
public:
    FixedBuffer() : m_cur(m_data) {}
    
    // 向缓冲区追加数据
    void append(const char* buf, size_t len) {
        if (static_cast<size_t>(avail()) > len) {
            memcpy(m_cur, buf, len);
            m_cur += len;
        }
    }
    
    const char* data() const { return m_data; }
    int length() const { return static_cast<int>(m_cur - m_data); }
    char* current() { return m_cur; }
    int avail() const { return static_cast<int>(end() - m_cur); }
    void add(size_t len) { m_cur += len; }
    void reset() { m_cur = m_data; }
    void bzero() { ::bzero(m_data, sizeof(m_data)); }
    std::string toString() const { return std::string(m_data, length()); }

private:
    const char* end() const { return m_data + sizeof(m_data); }
    char m_data[SIZE];
    char* m_cur;
};

// 异步日志输出地 (基于 Muduo 的双缓冲核心思想)
class AsyncLogAppender : public LogAppender {
public:
    typedef std::shared_ptr<AsyncLogAppender> ptr;
    using Buffer = FixedBuffer<kLargeBuffer>;
    using BufferPtr = std::unique_ptr<Buffer>;
    using BufferVector = std::vector<BufferPtr>;

    // 构造函数：自动启动后端落盘线程
    AsyncLogAppender(const std::string& basename, off_t rollSize = 50 * 1024 * 1024, int flushInterval = 3);
    
    // 析构函数：自动唤醒并回收后端线程，保证日志不丢失
    ~AsyncLogAppender();

    // 覆盖父类的 log 方法（前端业务线程调用）
    void log(Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) override;
    
    std::string toYamlString() override;

private:
    void start();
    void stop();
    // 后端专属线程函数：负责将缓存刷入磁盘
    void threadFunc();
    // 简单的日志滚动逻辑
    void rollFile();

private:
    const int m_flushInterval;      // 刷新间隔
    std::atomic<bool> m_running;    // 是否运行
    const std::string m_basename;   // 文件名前缀
    const off_t m_rollSize;         // 滚动大小
    
    Thread::ptr m_thread;           // 后端落盘线程
    std::mutex m_mutex;             // 标准互斥锁(配合条件变量保护内存池)
    std::condition_variable m_cond; // 条件变量(前端唤醒后端)

    BufferPtr m_currentBuffer;      // 当前缓冲区 (前端正在写)
    BufferPtr m_nextBuffer;         // 预备缓冲区 (防止当前块写满时频繁 new)
    BufferVector m_buffers;         // 待写入磁盘的满缓冲区队列

    FILE* m_fp = nullptr;           // 底层C文件指针(无锁写入性能极高)
    off_t m_writtenBytes = 0;       // 当前文件已写入字节数
};

}

#endif // __SYLAR_ASYNC_LOG_H__