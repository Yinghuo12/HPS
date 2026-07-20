#include "redis_pool.h"

#include <cstring>
#include <hiredis/hiredis.h>

namespace ddt {

RedisPool::RedisPool() = default;

RedisPool::~RedisPool() {
    close();
}

bool RedisPool::create(const std::string& host, int port, int maxSize) {
    if(maxSize <= 0) maxSize = 4;
    std::lock_guard<std::mutex> lk(m_mutex);
    m_host = host;
    m_port = port;
    m_maxSize = maxSize;
    m_closing = false;
    return true;
}

redisContext* RedisPool::doConnect() {
    // connect 在锁外调用(sylar hook 下可能 yield, 不能持锁)。
    // timeval 同时作 connect 和命令超时(hiredis 语义), 1.5s 与原实现一致。
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 500000;
    redisContext* rc = redisConnectWithTimeout(m_host.c_str(), m_port, tv);
    if(!rc || rc->err) {
        if(rc) redisFree(rc);
        return nullptr;
    }
    // 显式设命令超时, 避免个别慢命令把连接卡死
    redisSetTimeout(rc, tv);
    return rc;
}

redisContext* RedisPool::get() {
    while(true) {
        std::unique_lock<std::mutex> lk(m_mutex);
        m_cv.wait(lk, [this]() {
            return m_closing || !m_idle.empty() || m_created < m_maxSize;
        });
        if(m_closing) return nullptr;
        if(!m_idle.empty()) {
            // 有空闲: 直接 pop
            redisContext* c = m_idle.front();
            m_idle.pop_front();
            return c;
        }
        // 无空闲但未达上限: 预占一个创建配额, 锁外建连
        ++m_created;
        lk.unlock();
        redisContext* c = doConnect();
        if(c) return c;
        // 建连失败: 释放配额, notify 让其他等待者重试
        lk.lock();
        --m_created;
        m_cv.notify_one();
        // 短暂重试间隔防止 redis 宕机时打爆 CPU(简单轮询)
        // 注: 不在锁内 sleep, 释放锁后再 wait 重试
    }
}

void RedisPool::put(redisContext* c, bool healthy) {
    if(!c) return;
    std::lock_guard<std::mutex> lk(m_mutex);
    if(m_closing || !healthy) {
        redisFree(c);
        if(m_created > 0) --m_created;
        m_cv.notify_one();
        return;
    }
    m_idle.push_back(c);
    m_cv.notify_one();
}

void RedisPool::close() {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_closing = true;
    for(auto* c : m_idle) redisFree(c);
    m_idle.clear();
    m_cv.notify_all();
}

// ---- RedisGuard ----

RedisGuard::RedisGuard(RedisPool* pool)
    : m_pool(pool)
    , m_ctx(pool ? pool->get() : nullptr)
    , m_healthy(true) {
}

RedisGuard::~RedisGuard() {
    if(m_pool && m_ctx) m_pool->put(m_ctx, m_healthy);
}

// ---- Subscriber ----

Subscriber::Subscriber(const std::string& host, int port)
    : host_(host), port_(port) {
}

Subscriber::~Subscriber() {
    stop();
    if(ctx_) redisFree(ctx_);
}

bool Subscriber::subscribe(const std::string& channel, MsgCallback cb) {
    if(!ctx_) {
        // 连接阶段用超时(避免 Redis 不可达时永久阻塞)
        struct timeval tv = {1, 500000};
        ctx_ = redisConnectWithTimeout(host_.c_str(), port_, tv);
        if(!ctx_ || ctx_->err) {
            if(ctx_) {
                redisFree(ctx_);
                ctx_ = nullptr;
            }
            return false;
        }
        // 关键: redisConnectWithTimeout 会把 tv 同时设为 command_timeout,
        // 导致后续 redisGetReply 也受 1.5s 超时限制 → loop() 每次都超时退出 → 订阅断开。
        // 用 {0,0} 清除 command_timeout(hiredis 语义: 0 = NULL = 无超时),
        // 让 redisGetReply 永久阻塞等消息。
        struct timeval zeroTv = {0, 0};
        redisSetTimeout(ctx_, zeroTv);
    }
    redisReply* r = (redisReply*)redisCommand(ctx_, "SUBSCRIBE %s", channel.c_str());
    if(!r) return false;
    bool ok = (r->type == REDIS_REPLY_ARRAY);
    freeReplyObject(r);
    if(ok) {
        std::lock_guard<std::mutex> lk(cbMutex_);
        callbacks_[channel] = cb;
    }
    return ok;
}

bool Subscriber::loop() {
    if(!ctx_) return false;
    running_ = true;
    while(running_) {
        void* reply = nullptr;
        // redisGetReply 阻塞等待消息。线程/协程在此挂起。
        int rc = redisGetReply(ctx_, &reply);
        if(rc != REDIS_OK || !reply) break;
        redisReply* r = (redisReply*)reply;
        // SUBSCRIBE 消息格式: ["message", channel, payload]
        if(r->type == REDIS_REPLY_ARRAY && r->elements >= 3
           && r->element[0]->type == REDIS_REPLY_STRING
           && strcmp(r->element[0]->str, "message") == 0) {
            std::string channel(r->element[1]->str, r->element[1]->len);
            std::string payload(r->element[2]->str, r->element[2]->len);
            MsgCallback cb;
            {
                std::lock_guard<std::mutex> lk(cbMutex_);
                auto it = callbacks_.find(channel);
                if(it != callbacks_.end()) cb = it->second;
            }
            if(cb) cb(payload);
        }
        freeReplyObject(r);
    }
    running_ = false;
    return false;   // 连接断开(调用方可重连)
}

void Subscriber::stop() {
    running_ = false;
    // 发送 unsubscribe 让阻塞的 redisGetReply 返回
    if(ctx_) {
        redisReply* r = (redisReply*)redisCommand(ctx_, "UNSUBSCRIBE");
        if(r) freeReplyObject(r);
    }
}

// ---- publish ----

bool redisPublish(RedisPool& pool, const std::string& channel, const std::string& msg) {
    RedisGuard g(&pool);
    if(!g) return false;
    redisReply* r = (redisReply*)redisCommand(g.get(), "PUBLISH %s %b",
        channel.c_str(), msg.data(), msg.size());
    if(!r) {
        g.markUnhealthy();
        return false;
    }
    bool ok = (r->type == REDIS_REPLY_INTEGER && r->integer >= 0);
    if(g.get()->err) g.markUnhealthy();
    freeReplyObject(r);
    return ok;
}

bool redisPublish(const std::string& host, int port, const std::string& channel, const std::string& msg) {
    struct timeval tv = {1, 500000};
    redisContext* rc = redisConnectWithTimeout(host.c_str(), port, tv);
    if(!rc || rc->err) {
        if(rc) redisFree(rc);
        return false;
    }
    redisSetTimeout(rc, tv);
    redisReply* r = (redisReply*)redisCommand(rc, "PUBLISH %s %b",
        channel.c_str(), msg.data(), msg.size());
    bool ok = false;
    if(r && r->type == REDIS_REPLY_INTEGER && r->integer >= 0) ok = true;
    if(r) freeReplyObject(r);
    redisFree(rc);
    return ok;
}

} // namespace ddt
