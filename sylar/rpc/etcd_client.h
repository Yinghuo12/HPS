#ifndef __SYLAR_RPC_ETCD_CLIENT_H__
#define __SYLAR_RPC_ETCD_CLIENT_H__

#include <stdint.h>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "sylar/core/thread.h"

namespace sylar {
namespace rpc {

// ===== KV / Lease 原语层 =====

// etcd v3 客户端封装（基于 etcd-cpp-apiv3），底层细节全部隔离在 .cc 内（PImpl）。
class EtcdClient {
public:
    typedef std::shared_ptr<EtcdClient> ptr;

    // 键值对
    struct KV {
        std::string key;
        std::string value;
        int64_t lease = 0;   // 关联的租约 ID（0 表示无租约/持久键）
    };

    explicit EtcdClient(
        const std::string& endpoint = "http://127.0.0.1:2379",
        uint64_t timeout_ms = 3000);
    ~EtcdClient();

    // 单键写入；lease_id > 0 表示绑定租约（等价 ZK 临时节点）。
    bool put(const std::string& key, const std::string& value, int64_t lease_id = 0);
    // 单键读取，未命中返回 false。
    bool get(const std::string& key, KV& out);
    // 按前缀读取全部键值（用于服务发现枚举同一方法下的所有实例）。
    bool getPrefix(const std::string& prefix, std::vector<KV>& out);
    // 删除单键。
    bool del(const std::string& key);
    // 删除整个前缀。
    bool delPrefix(const std::string& prefix);

    // 申请租约，返回 lease_id（<=0 表示失败）。
    int64_t leaseGrant(int ttl);
    // 续租，返回服务端剩余 TTL（<=0 表示租约已失效）。
    int64_t leaseKeepalive(int64_t lease_id);
    // 撤销租约（同时删除该租约名下的所有键）。
    bool leaseRevoke(int64_t lease_id);

    const std::string& getEndpoint() const;

    EtcdClient(const EtcdClient&) = delete;
    EtcdClient& operator=(const EtcdClient&) = delete;

private:
    // PImpl：隔离 etcd::Client（C++17）
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// ===== 服务注册层 =====

// 注册信息：键 / 值 / 租约 TTL（秒）。
struct EtcdRegisterInfo {
    std::string key;      // 例如 /FriendService/GetFriendList
    std::string value;    // 例如 127.0.0.1:9000
    int ttl = 30;         // 租约时间
};

// 服务注册器：内部为每个服务申请租约并绑定键，
// 由 etcd::KeepAlive 后台续租（RAII，无需自建线程）；续租失败时自动重新注册（自愈）。
class EtcdRegistrar {
public:
    typedef std::shared_ptr<EtcdRegistrar> ptr;

    EtcdRegistrar(
        const std::string& endpoint = "http://127.0.0.1:2379",
        uint64_t timeout_ms = 3000);
    ~EtcdRegistrar();

    // 注册一个服务。重复注册同名 key 返回 false。
    bool registerService(const EtcdRegisterInfo& info);
    // 注销一个服务（撤销租约 + 删除键）。
    bool unregisterService(const std::string& key);
    // 更新已有服务的值（复用原租约，TTL 不变）。
    bool updateService(const EtcdRegisterInfo& info);
    // 注销全部已注册服务。
    bool clear();
    // 当前已注册服务列表（拷贝，线程安全）。
    std::vector<EtcdRegisterInfo> haveServices() const;

    const std::string& getEndpoint() const;

    EtcdRegistrar(const EtcdRegistrar&) = delete;
    EtcdRegistrar& operator=(const EtcdRegistrar&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// ===== 服务发现监视层 =====

// 监视事件类型
enum EtcdWatchEvent {
    ETCD_PUT = 0,    // 新增或值变化
    ETCD_DELETE = 1  // 键被删除
};

// 监视回调：事件类型 + key + value（删除时 value 为空）
typedef std::function<void(EtcdWatchEvent, const std::string&, const std::string&)> EtcdWatchCallback;

// 监视项：前缀 / 回调。由 etcd::Watcher 在后台推送事件，无需轮询。
struct EtcdWatchInfo {
    std::string prefix;
    EtcdWatchCallback cb;
};

// 服务发现监视器：底层用 etcd::Watcher 对前缀做递归监听，
// etcd 服务端推送事件即触发回调，无需轮询。
class EtcdWatcher {
public:
    typedef std::shared_ptr<EtcdWatcher> ptr;

    EtcdWatcher(
        const std::string& endpoint = "http://127.0.0.1:2379",
        uint64_t timeout_ms = 3000);
    ~EtcdWatcher();

    // 添加一个前缀监视。
    bool watch(const EtcdWatchInfo& info);
    // 取消某个前缀的监视。
    bool cancel(const std::string& prefix);
    // 取消所有监视。
    void cancelAll();

    const std::string& getEndpoint() const;

    EtcdWatcher(const EtcdWatcher&) = delete;
    EtcdWatcher& operator=(const EtcdWatcher&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace rpc
}  // namespace sylar

#endif
