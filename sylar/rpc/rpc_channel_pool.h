#ifndef __SYLAR_RPC_RPC_CHANNEL_POOL_H__
#define __SYLAR_RPC_RPC_CHANNEL_POOL_H__

#include <condition_variable>
#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "sylar/net/socket.h"
#include "sylar/net/async_socket_stream.h"

namespace sylar {
namespace rpc {

class RpcStream;  // 前置声明

// RPC 客户端连接池 + 服务发现缓存：用 socket 池化消除每次 connect/close 开销，
// 用带 TTL 的发现缓存消除每次 etcd 服务发现的开销。
class RpcChannelPool : public std::enable_shared_from_this<RpcChannelPool> {
public:
    typedef std::shared_ptr<RpcChannelPool> ptr;

    // etcdEndpoint: 服务发现地址; maxSizePerHost: 每个目标 ip:port 的最大连接数。
    explicit RpcChannelPool(
        const std::string& etcdEndpoint = "http://127.0.0.1:2379",
        size_t maxSizePerHost = 8);
    ~RpcChannelPool();

    // 借一个到 ip:port 的 socket。无空闲且未达上限则新建并 connect;
    // 已达上限则阻塞等待（协程 yield）直到有空闲或容量。返回 nullptr 表示 connect 失败。
    sylar::Socket::ptr acquire(const std::string& ip, uint16_t port);

    // 归还 socket。健康（isConnected）则放回空闲池; 已断则丢弃（减少 outstanding 计数）。
    void release(const std::string& ip, uint16_t port, sylar::Socket::ptr sock);

    // ---- 多路复用(AsyncSocketStream) ----
    // 借一个 RpcStream(封装 Socket 为 AsyncSocketStream, 支持长连接多路复用)。
    // 首次借出时 start()(启动 doRead+doWrite); 归还时不 close, 下次复用。
    std::shared_ptr<RpcStream> acquireStream(const std::string& ip, uint16_t port);
    // 归还 RpcStream(不断开, 放回池供复用)。
    void releaseStream(const std::string& ip, uint16_t port, std::shared_ptr<RpcStream> stream);

    // ---- 服务发现缓存（带 TTL） ----
    // 查 method_path（如 "/BattleService/Shoot"）对应的 ip:port。
    // 缓存命中且未过期则直接返回 true 并填充 ip/port; 否则返回 false（由调用方查 etcd 后调 putDiscovery 更新）。
    bool getDiscovery(const std::string& method_path, std::string& ip, uint16_t& port);
    // 更新发现缓存（查 etcd 命中后调用）。
    void putDiscovery(const std::string& method_path, const std::string& ip, uint16_t port);

    // RAII 归还守卫：构造时 acquire（可选），析构时自动 release。避免业务侧忘记归还。
    class Guard {
    public:
        Guard(
            RpcChannelPool* pool,
            const std::string& ip,
            uint16_t port,
            sylar::Socket::ptr sock)
            : m_pool(pool),
              m_ip(ip),
              m_port(port),
              m_sock(sock) {}

        ~Guard() {
            if (m_pool && m_sock) {
                m_pool->release(m_ip, m_port, m_sock);
            }
        }

        sylar::Socket::ptr sock() const { return m_sock; }
        sylar::Socket::ptr operator->() const { return m_sock; }
        explicit operator bool() const { return (bool)m_sock; }
        // 转移所有权（不归还）：用于 sock 已坏、调用方要丢弃的场景
        void release() { m_pool = nullptr; }

        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;

    private:
        RpcChannelPool* m_pool;
        std::string m_ip;
        uint16_t m_port;
        sylar::Socket::ptr m_sock;
    };

    // 发现缓存项
    struct DiscoveryEntry {
        std::string ip;
        uint16_t port = 0;
        uint64_t expireMs = 0;  // 绝对过期时间（GetCurrentMS()）
    };

private:
    // 每个 ip:port 一个子池
    struct HostPool {
        std::list<sylar::Socket::ptr> idle;  // 空闲 socket（可复用）
        size_t outstanding = 0;              // 借出未还数
    };

    static std::string hostKey(const std::string& ip, uint16_t port) {
        return ip + ":" + std::to_string(port);
    }

    std::string m_etcdEndpoint;
    size_t m_maxSizePerHost;
    // 连接池(裸 Socket)
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::map<std::string, HostPool> m_hosts;
    // 连接池(RpcStream 多路复用)
    std::map<std::string, std::list<std::shared_ptr<RpcStream>>> m_streamPool;
    // 服务发现缓存
    std::map<std::string, DiscoveryEntry> m_discovery;
    uint64_t m_discoveryTtlMs;  // 发现缓存 TTL（默认 5000ms）
};

}  // namespace rpc
}  // namespace sylar

#endif
