#include "sylar/rpc/rpc_channel_pool.h"

#include "sylar/core/log.h"
#include "sylar/core/sys_util.h"      // GetCurrentMS
#include "sylar/net/address.h"

namespace sylar {
namespace rpc {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("rpc");

RpcChannelPool::RpcChannelPool(const std::string& etcdEndpoint, size_t maxSizePerHost)
    : m_etcdEndpoint(etcdEndpoint)
    , m_maxSizePerHost(maxSizePerHost > 0 ? maxSizePerHost : 1)
    , m_discoveryTtlMs(5000) {   // 服务发现缓存 5s TTL
}

RpcChannelPool::~RpcChannelPool() {
    // 清理所有空闲 socket(断开)
    std::lock_guard<std::mutex> lk(m_mutex);
    for(auto& kv : m_hosts) {
        for(auto& sock : kv.second.idle) {
            if(sock) sock->close();
        }
    }
}

sylar::Socket::ptr RpcChannelPool::acquire(const std::string& ip, uint16_t port) {
    std::string key = hostKey(ip, port);
    // 1) 先尝试从空闲池取一个
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        auto& hp = m_hosts[key];
        if(!hp.idle.empty()) {
            auto sock = hp.idle.back();
            hp.idle.pop_back();
            hp.outstanding++;
            if(sock->isConnected()) return sock;
            // 已断的连接丢弃, 继续走新建路径(下面 outstanding 已+1, 容量已占)
            // 但要先把刚 +1 的 outstanding 还回去, 走正常新建逻辑
            hp.outstanding--;
        }
    }
    // 2) 决定能否新建: 在锁内判断容量, 锁外执行 connect
    {
        std::unique_lock<std::mutex> lk(m_mutex);
        auto& hp = m_hosts[key];
        m_cv.wait(lk, [&]() {
            return !hp.idle.empty() || (hp.idle.size() + hp.outstanding) < m_maxSizePerHost;
        });
        // 被唤醒后再次检查空闲(可能别人归还了)
        if(!hp.idle.empty()) {
            auto sock = hp.idle.back();
            hp.idle.pop_back();
            hp.outstanding++;
            if(sock->isConnected()) return sock;
            hp.outstanding--;
        }
        // 可以新建: 预占容量, 防止并发新建超限
        hp.outstanding++;
    }
    // 3) 锁外 connect(可能 yield, 不能持锁)
    IPAddress::ptr addr = IPAddress::Create(ip.c_str(), port);
    if(!addr) {
        SYLAR_LOG_WARN(g_logger) << "pool acquire: invalid address " << ip << ":" << port;
        std::lock_guard<std::mutex> lk(m_mutex);
        m_hosts[key].outstanding--;
        m_cv.notify_one();
        return nullptr;
    }
    sylar::Socket::ptr sock = Socket::CreateTCP(addr);
    if(!sock || !sock->connect(addr)) {
        SYLAR_LOG_DEBUG(g_logger) << "pool acquire: connect fail " << ip << ":" << port;
        std::lock_guard<std::mutex> lk(m_mutex);
        m_hosts[key].outstanding--;
        m_cv.notify_one();
        return nullptr;
    }
    return sock;
}

void RpcChannelPool::release(const std::string& ip, uint16_t port, sylar::Socket::ptr sock) {
    if(!sock) {
        // 空指针: 仍是借出的占用, 减计数
        std::lock_guard<std::mutex> lk(m_mutex);
        auto& hp = m_hosts[hostKey(ip, port)];
        if(hp.outstanding > 0) hp.outstanding--;
        m_cv.notify_one();
        return;
    }
    std::lock_guard<std::mutex> lk(m_mutex);
    auto& hp = m_hosts[hostKey(ip, port)];
    if(hp.outstanding > 0) hp.outstanding--;
    if(sock->isConnected()) {
        hp.idle.push_back(sock);   // 健康则放回空闲池
    } else {
        sock->close();             // 断开则丢弃
    }
    m_cv.notify_one();   // 唤醒可能在等待的 acquire
}

bool RpcChannelPool::getDiscovery(const std::string& method_path, std::string& ip, uint16_t& port) {
    std::lock_guard<std::mutex> lk(m_mutex);
    auto it = m_discovery.find(method_path);
    if(it == m_discovery.end()) return false;
    if(sylar::GetCurrentMS() >= it->second.expireMs) {
        m_discovery.erase(it);   // 过期, 删除
        return false;
    }
    ip = it->second.ip;
    port = it->second.port;
    return true;
}

void RpcChannelPool::putDiscovery(const std::string& method_path, const std::string& ip, uint16_t port) {
    std::lock_guard<std::mutex> lk(m_mutex);
    DiscoveryEntry e;
    e.ip = ip;
    e.port = port;
    e.expireMs = sylar::GetCurrentMS() + m_discoveryTtlMs;
    m_discovery[method_path] = e;
}

} // namespace rpc
} // namespace sylar
