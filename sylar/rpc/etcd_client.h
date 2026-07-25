#ifndef __SYLAR_RPC_ETCD_CLIENT_H__
#define __SYLAR_RPC_ETCD_CLIENT_H__

#include <stdint.h>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <etcd/SyncClient.hpp>
#include <etcd/KeepAlive.hpp>
#include <etcd/Watcher.hpp>

#include "sylar/core/thread.h"

namespace sylar {
namespace rpc {

// etcd v3 客户端（基于 etcd-cpp-apiv3）
class EtcdClient {
public:
    typedef std::shared_ptr<EtcdClient> ptr;

    struct KV {
        std::string key;
        std::string value;
        int64_t lease = 0;
    };

    explicit EtcdClient(
        const std::string& endpoint = "http://127.0.0.1:2379",
        uint64_t timeout_ms = 3000);
    ~EtcdClient();

    bool put(const std::string& key, const std::string& value, int64_t lease_id = 0);
    bool get(const std::string& key, KV& out);
    bool getPrefix(const std::string& prefix, std::vector<KV>& out);
    bool del(const std::string& key);
    bool delPrefix(const std::string& prefix);

    int64_t leaseGrant(int ttl);
    int64_t leaseKeepalive(int64_t lease_id);
    bool leaseRevoke(int64_t lease_id);

    const std::string& getEndpoint() const { return m_endpoint; }
    bool ready() const { return (bool)m_client; }

    EtcdClient(const EtcdClient&) = delete;
    EtcdClient& operator=(const EtcdClient&) = delete;

private:
    std::shared_ptr<etcd::SyncClient> m_client;
    std::string m_endpoint;
};

// 服务注册信息
struct EtcdRegisterInfo {
    std::string key;
    std::string value;
    int ttl = 30;
};

// 服务注册器：每个服务绑定 lease + KeepAlive 自动续租（RAII）
class EtcdRegistrar {
public:
    typedef std::shared_ptr<EtcdRegistrar> ptr;

    EtcdRegistrar(
        const std::string& endpoint = "http://127.0.0.1:2379",
        uint64_t timeout_ms = 3000);
    ~EtcdRegistrar();

    bool registerService(const EtcdRegisterInfo& info);
    bool unregisterService(const std::string& key);
    bool updateService(const EtcdRegisterInfo& info);
    bool clear();
    std::vector<EtcdRegisterInfo> haveServices() const;

    const std::string& getEndpoint() const { return m_endpoint; }

    EtcdRegistrar(const EtcdRegistrar&) = delete;
    EtcdRegistrar& operator=(const EtcdRegistrar&) = delete;

private:
    struct Entry {
        EtcdRegisterInfo info;
        std::shared_ptr<etcd::KeepAlive> keepalive;
        int64_t leaseId = 0;
    };

    bool doRegisterLocked(const EtcdRegisterInfo& info, Entry& out);

    std::shared_ptr<etcd::SyncClient> m_client;
    std::string m_endpoint;
    mutable RWMutex m_mutex;
    std::unordered_map<std::string, Entry> m_entries;
};

// 监视事件类型
enum EtcdWatchEvent {
    ETCD_PUT = 0,
    ETCD_DELETE = 1
};

typedef std::function<void(EtcdWatchEvent, const std::string&, const std::string&)> EtcdWatchCallback;

struct EtcdWatchInfo {
    std::string prefix;
    EtcdWatchCallback cb;
};

// 服务发现监视器（基于 etcd::Watcher 前缀监听）
class EtcdWatcher {
public:
    typedef std::shared_ptr<EtcdWatcher> ptr;

    EtcdWatcher(
        const std::string& endpoint = "http://127.0.0.1:2379",
        uint64_t timeout_ms = 3000);
    ~EtcdWatcher();

    bool watch(const EtcdWatchInfo& info);
    bool cancel(const std::string& prefix);
    void cancelAll();

    const std::string& getEndpoint() const { return m_endpoint; }

    EtcdWatcher(const EtcdWatcher&) = delete;
    EtcdWatcher& operator=(const EtcdWatcher&) = delete;

private:
    struct WatchCtx {
        std::string prefix;
        std::unique_ptr<etcd::Watcher> watcher;
    };

    std::shared_ptr<etcd::SyncClient> m_client;
    std::string m_endpoint;
    mutable RWMutex m_mutex;
    std::unordered_map<std::string, WatchCtx> m_watches;
};

}  // namespace rpc
}  // namespace sylar

#endif
