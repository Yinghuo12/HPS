#include "asynclog.h"
#include <time.h>
#include <assert.h>

namespace sylar {

AsyncLogAppender::AsyncLogAppender(const std::string& basename, off_t rollSize, int flushInterval)
    : m_flushInterval(flushInterval)
    , m_running(false)
    , m_basename(basename)
    , m_rollSize(rollSize)
    , m_currentBuffer(new Buffer)
    , m_nextBuffer(new Buffer) 
{
    m_currentBuffer->bzero();
    m_nextBuffer->bzero();
    m_buffers.reserve(16);
    rollFile(); // 初始化打开文件
    
    // 构造即启动：对用户层完全屏蔽线程管理细节
    start();
}

AsyncLogAppender::~AsyncLogAppender() {
    // 析构即停止：安全落盘，完美收尾
    if (m_running) {
        stop();
    }
    if (m_fp) {
        ::fclose(m_fp);
    }
}

void AsyncLogAppender::start() {
    m_running = true;
    m_thread.reset(new Thread(std::bind(&AsyncLogAppender::threadFunc, this), "AsyncLog"));
}

void AsyncLogAppender::stop() {
    m_running = false;
    m_cond.notify_one();
    if(m_thread) {
        m_thread->join();
    }
}

std::string AsyncLogAppender::toYamlString() {
    return "[AsyncLogAppender basename=" + m_basename + "]";
}

void AsyncLogAppender::rollFile() {
    if (m_fp) {
        ::fclose(m_fp);
    }
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    char timebuf[32];
    strftime(timebuf, sizeof timebuf, "%Y%m%d-%H%M%S", &tm);
    std::string filename = m_basename + "_" + timebuf + ".log";
    
    m_fp = ::fopen(filename.c_str(), "ae");
    m_writtenBytes = 0;
}

// 前端业务线程：将日志推入内存缓冲区
void AsyncLogAppender::log(Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) {
    if (level < m_level) return;

    std::string str = m_formatter->format(logger, level, event);
    
    std::lock_guard<std::mutex> lock(m_mutex);
    // 1. 极速路径：当前内存块容量足够，直接 memcpy
    if (m_currentBuffer->avail() > (int)str.length()) {
        m_currentBuffer->append(str.c_str(), str.length());
    } 
    // 2. 满载路径：内存块已满，切入备用块并唤醒后台线程
    else {
        m_buffers.push_back(std::move(m_currentBuffer));
        if (m_nextBuffer) {
            m_currentBuffer = std::move(m_nextBuffer);
        } else {
            m_currentBuffer.reset(new Buffer); // 罕见情况：备用块也被耗尽
        }
        m_currentBuffer->append(str.c_str(), str.length());
        m_cond.notify_one(); // 唤醒后端线程去落盘
    }
}

// 后端落盘线程：批量无锁刷盘
void AsyncLogAppender::threadFunc() {
    BufferPtr newBuffer1(new Buffer);
    BufferPtr newBuffer2(new Buffer);
    newBuffer1->bzero();
    newBuffer2->bzero();

    BufferVector buffersToWrite;
    buffersToWrite.reserve(16);

    while (m_running) {
        {
            // 临界区：只用于交换指针，绝不涉及磁盘 IO
            std::unique_lock<std::mutex> lock(m_mutex);
            if (m_buffers.empty()) {
                m_cond.wait_for(lock, std::chrono::seconds(m_flushInterval));
            }
            
            // 将前端当前未写满的 buffer 也推入队列，保证日志实时性
            m_buffers.push_back(std::move(m_currentBuffer));
            m_currentBuffer = std::move(newBuffer1); // 归还空闲块给前端
            
            buffersToWrite.swap(m_buffers); // O(1) 窃取满块列表
            
            if (!m_nextBuffer) {
                m_nextBuffer = std::move(newBuffer2); // 归还备用块给前端
            }
        } // 离开临界区，前端可以继续欢快地写内存了

        // 防洪堤：如果后端落盘速度严重跟不上前端（例如磁盘卡死），强行丢弃积压的日志防止 OOM
        if (buffersToWrite.size() > 25) {
            buffersToWrite.erase(buffersToWrite.begin() + 2, buffersToWrite.end());
        }

        // 纯后台落盘：由于这是单线程私有操作，使用无锁版 fwrite_unlocked 获取极致性能
        for (const auto& buffer : buffersToWrite) {
            size_t n = ::fwrite_unlocked(buffer->data(), 1, buffer->length(), m_fp);
            m_writtenBytes += n;
        }

        if (m_writtenBytes > m_rollSize) {
            rollFile();
        }
        ::fflush(m_fp);

        // 垃圾回收：保留两个容量干净的块下次交换，多余的销毁
        if (buffersToWrite.size() > 2) {
            buffersToWrite.resize(2);
        }

        if (!newBuffer1) {
            newBuffer1 = std::move(buffersToWrite.back());
            buffersToWrite.pop_back();
            newBuffer1->reset();
        }
        if (!newBuffer2) {
            newBuffer2 = std::move(buffersToWrite.back());
            buffersToWrite.pop_back();
            newBuffer2->reset();
        }
        buffersToWrite.clear();
    }
    ::fflush(m_fp); // 退出前最后刷一次盘
}

}