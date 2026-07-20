#ifndef __DDT_REDIS_POOL_H__
#define __DDT_REDIS_POOL_H__

#include <condition_variable>
#include <functional>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>

// 前向声明 hiredis 类型(避免头文件污染)
struct redisContext;

namespace ddt {

// Redis 连接池(仿 sylar::ConnectionPool 模式)
// 替代原 data_service 的单连接 + 全局 mutex 方案, token 操作并行化。
// 自愈: 借出方 markUnhealthy() 标记损坏的连接, 归还时池销毁它, 下次 get 自动新建。
class RedisPool {
public:
    RedisPool();
    ~RedisPool();

    // 创建池: 后续按需建连, 不预热(冷启动延迟低, 业务首次 get 时建立)。
    bool create(const std::string& host, int port, int maxSize);

    // 借出连接(阻塞, 池空且达上限则等)。失败(nullptr)表示 pool 已关闭。
    redisContext* get();

    // 归还连接。healthy=true 入空闲队列; false 销毁并 --m_created(下次自动重建)。
    void put(redisContext* c, bool healthy);

    // 关闭池: 释放所有空闲连接, 标记关闭, 后续 get 立即返回 nullptr。
    void close();

private:
    // 锁外实际建连, 失败返回 nullptr。
    redisContext* doConnect();

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::list<redisContext*> m_idle;
    int m_maxSize = 4;
    int m_created = 0;     // 空闲 + 借出 的总创建数
    std::string m_host;
    int m_port = 6379;
    bool m_closing = false;
};

// RAII 借还 guard(对称 sylar::Database)。析构时归还或丢弃(markUnhealthy 标记的)。
class RedisGuard {
public:
    explicit RedisGuard(RedisPool* pool);
    ~RedisGuard();
    RedisGuard(const RedisGuard&) = delete;
    RedisGuard& operator=(const RedisGuard&) = delete;

    redisContext* get() const { return m_ctx; }
    operator bool() const { return m_ctx != nullptr; }

    // 标记当前连接不健康(命令失败/连接错误), 析构时丢弃而非归还。
    void markUnhealthy() { m_healthy = false; }

private:
    RedisPool* m_pool;
    redisContext* m_ctx;
    bool m_healthy;
};

// 订阅者: 持有一个独立的 redisContext(不入连接池), 长期阻塞接收消息。
class Subscriber {
public:
    using MsgCallback = std::function<void(const std::string&)>;
    Subscriber(const std::string& host, int port);
    ~Subscriber();
    // 订阅一个 channel, 注册消息回调。可多次调用订阅多 channel。
    bool subscribe(const std::string& channel, MsgCallback cb);
    // 阻塞循环接收消息, 在独立线程/协程里调用。回调在调用线程上下文执行。
    // 返回 false 表示连接断开(调用方可重连)。
    bool loop();
    // 主动停止 loop(线程安全, 设标志)
    void stop();

private:
    std::string host_;
    int port_;
    redisContext* ctx_ = nullptr;
    bool running_ = false;
    std::mutex cbMutex_;
    // channel -> callback 映射(不同 channel 不同回调)
    std::unordered_map<std::string, MsgCallback> callbacks_;
};

// 发布消息到 channel(便捷工具, 用普通短连接即可)。
bool redisPublish(RedisPool& pool, const std::string& channel, const std::string& msg);
bool redisPublish(const std::string& host, int port, const std::string& channel, const std::string& msg);

} // namespace ddt

#endif
