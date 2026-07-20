// etcd 客户端封装：本文件以 C++17 编译，其余 rpc/core 模块保持 C++11 不变。
#include "sylar/rpc/etcd_client.h"

#include <etcd/SyncClient.hpp>
#include <etcd/KeepAlive.hpp>
#include <etcd/Response.hpp>
#include <etcd/Value.hpp>
#include <etcd/Watcher.hpp>

#include <chrono>
#include <memory>
#include <utility>

#include "sylar/core/log.h"
#include "sylar/core/thread.h"

namespace sylar {
namespace rpc {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("rpc");

// 计算前缀 range 的 range_end：最后一字节 +1（全 0xFF 则到键空间末尾）。
static std::string prefixRangeEnd(const std::string& prefix) {
    if (prefix.empty()) {
        return "\0";
    }
    std::string end = prefix;
    for (int i = (int)end.size() - 1; i >= 0; --i) {
        unsigned char c = (unsigned char)end[i];
        if (c < 0xFF) {
            end[i] = (char)(c + 1);
            end.resize(i + 1);
            return end;
        }
    }
    return "\0";
}

// EtcdClient::Impl：etcd 同步客户端实现。
struct EtcdClient::Impl {
    std::shared_ptr<etcd::SyncClient> client;
    std::string endpoint;

    Impl(const std::string& ep, uint64_t timeout_ms)
        : endpoint(ep) {
        try {
            client = std::make_shared<etcd::SyncClient>(ep);
            // 防止 etcd 宕机/网络断连时同步调用永久阻塞协程/线程
            client->set_grpc_timeout(std::chrono::milliseconds(timeout_ms));
        } catch (const std::exception& e) {
            SYLAR_LOG_ERROR(g_logger) << "etcd::SyncClient init fail, endpoint=" << ep
                                      << ", err=" << e.what();
        } catch (...) {
            SYLAR_LOG_ERROR(g_logger) << "etcd::SyncClient init unknown error, endpoint=" << ep;
        }
    }

    bool ready() const {
        return (bool)client;
    }
};

EtcdClient::EtcdClient(const std::string& endpoint, uint64_t timeout_ms)
    : m_impl(new Impl(endpoint, timeout_ms)) {
}

EtcdClient::~EtcdClient() = default;

// 写入键值；lease_id > 0 时绑定租约。
bool EtcdClient::put(const std::string& key, const std::string& value, int64_t lease_id) {
    if (!m_impl->ready()) {
        return false;
    }
    try {
        etcd::Response resp = (lease_id > 0)
            ? m_impl->client->put(key, value, lease_id)
            : m_impl->client->put(key, value);
        if (!resp.is_ok()) {
            SYLAR_LOG_ERROR(g_logger) << "etcd put fail key=" << key
                                      << ", err=" << resp.error_message();
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << "etcd put exception: " << e.what();
        return false;
    } catch (...) {
        SYLAR_LOG_ERROR(g_logger) << "etcd put unknown exception";
        return false;
    }
}

// 单键查询，键不存在 -> etcd 返回 !is_ok()。
bool EtcdClient::get(const std::string& key, KV& out) {
    if (!m_impl->ready()) {
        return false;
    }
    try {
        etcd::Response resp = m_impl->client->get(key);
        if (!resp.is_ok()) {
            return false;
        }
        const etcd::Value& v = resp.value();
        out.key = v.key();
        out.value = v.as_string();
        out.lease = v.lease();
        return true;
    } catch (const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << "etcd get exception: " << e.what();
        return false;
    } catch (...) {
        SYLAR_LOG_ERROR(g_logger) << "etcd get unknown exception";
        return false;
    }
}

// 前缀范围查询：ls(key, range_end) 取 [key, range_end) 区间。
bool EtcdClient::getPrefix(const std::string& prefix, std::vector<KV>& out) {
    if (!m_impl->ready()) {
        return false;
    }
    try {
        etcd::Response resp = m_impl->client->ls(prefix, prefixRangeEnd(prefix));
        if (!resp.is_ok()) {
            return false;
        }
        out.clear();
        out.reserve(resp.keys().size());
        for (int i = 0; i < (int)resp.keys().size(); ++i) {
            KV kv;
            kv.key = resp.key(i);
            kv.value = resp.values()[i].as_string();
            kv.lease = resp.values()[i].lease();
            out.push_back(std::move(kv));
        }
        return true;
    } catch (const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << "etcd getPrefix exception: " << e.what();
        return false;
    } catch (...) {
        SYLAR_LOG_ERROR(g_logger) << "etcd getPrefix unknown exception";
        return false;
    }
}

// 删除单键。
bool EtcdClient::del(const std::string& key) {
    if (!m_impl->ready()) {
        return false;
    }
    try {
        etcd::Response resp = m_impl->client->rm(key);
        return resp.is_ok();
    } catch (const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << "etcd del exception: " << e.what();
        return false;
    } catch (...) {
        return false;
    }
}

// 删除前缀范围。
bool EtcdClient::delPrefix(const std::string& prefix) {
    if (!m_impl->ready()) {
        return false;
    }
    try {
        etcd::Response resp = m_impl->client->rmdir(prefix, prefixRangeEnd(prefix));
        return resp.is_ok();
    } catch (const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << "etcd delPrefix exception: " << e.what();
        return false;
    } catch (...) {
        return false;
    }
}

// 申请租约，返回 lease_id（失败返回 0）。
int64_t EtcdClient::leaseGrant(int ttl) {
    if (!m_impl->ready()) {
        return 0;
    }
    try {
        etcd::Response resp = m_impl->client->leasegrant(ttl);
        if (!resp.is_ok()) {
            SYLAR_LOG_ERROR(g_logger) << "etcd leasegrant fail: " << resp.error_message();
            return 0;
        }
        return resp.value().lease();
    } catch (const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << "etcd leasegrant exception: " << e.what();
        return 0;
    } catch (...) {
        return 0;
    }
}

// 存活探测：leasetimetolive 返回租约剩余 TTL，>0 表示仍有效。
// 注意：SyncClient::leasekeepalive(ttl) 会重新创建 KeepAlive，这里只做存活探测。
int64_t EtcdClient::leaseKeepalive(int64_t lease_id) {
    if (!m_impl->ready()) {
        return 0;
    }
    try {
        etcd::Response resp = m_impl->client->leasetimetolive(lease_id);
        if (!resp.is_ok()) {
            return 0;
        }
        return resp.value().ttl() > 0 ? resp.value().ttl() : 0;
    } catch (const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << "etcd leaseKeepalive exception: " << e.what();
        return 0;
    } catch (...) {
        return 0;
    }
}

// 撤销租约。
bool EtcdClient::leaseRevoke(int64_t lease_id) {
    if (!m_impl->ready()) {
        return false;
    }
    try {
        etcd::Response resp = m_impl->client->leaserevoke(lease_id);
        return resp.is_ok();
    } catch (const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << "etcd leaseRevoke exception: " << e.what();
        return false;
    } catch (...) {
        return false;
    }
}

const std::string& EtcdClient::getEndpoint() const {
    return m_impl->endpoint;
}

// EtcdRegistrar::Impl：服务注册实现，每个服务由 Entry 维护 KeepAlive + lease。
struct EtcdRegistrar::Impl {
    std::shared_ptr<etcd::SyncClient> client;
    std::string endpoint;

    // 每个服务的记录：注册信息 + KeepAlive（RAII 自动续租）+ lease id
    struct Entry {
        EtcdRegisterInfo info;
        std::shared_ptr<etcd::KeepAlive> keepalive;
        int64_t leaseId = 0;
    };

    mutable RWMutex mutex;
    std::unordered_map<std::string, Entry> entries;

    Impl(const std::string& ep, uint64_t timeout_ms)
        : endpoint(ep) {
        try {
            client = std::make_shared<etcd::SyncClient>(ep);
            client->set_grpc_timeout(std::chrono::milliseconds(timeout_ms));
        } catch (const std::exception& e) {
            SYLAR_LOG_ERROR(g_logger) << "EtcdRegistrar etcd::SyncClient init fail, endpoint=" << ep
                                      << ", err=" << e.what();
        } catch (...) {
            SYLAR_LOG_ERROR(g_logger) << "EtcdRegistrar etcd::SyncClient init unknown error";
        }
    }

    bool ready() const {
        return (bool)client;
    }

    // 持锁时调用：KeepAlive(ttl) 自动申请租约并续租 + put 绑定租约。失败回滚。
    bool doRegisterLocked(const EtcdRegisterInfo& info, Entry& out) {
        // 1. KeepAlive(client, handler, ttl)：handler 用于续租失败自愈告警；
        //    构造即申请租约并开始周期续租（lease_id=0 表示新建）。
        std::shared_ptr<etcd::KeepAlive> ka;
        try {
            ka = std::make_shared<etcd::KeepAlive>(
                *client,
                [info](std::exception_ptr eptr) {
                    if (eptr) {
                        try {
                            std::rethrow_exception(eptr);
                        } catch (const std::exception& e) {
                            SYLAR_LOG_ERROR(g_logger) << "etcd KeepAlive fail key="
                                                      << info.key << ", err=" << e.what()
                                                      << " (lease will expire; restart service to re-register)";
                        } catch (...) {
                            SYLAR_LOG_ERROR(g_logger) << "etcd KeepAlive fail key=" << info.key;
                        }
                    }
                },
                info.ttl);
        } catch (const std::exception& e) {
            SYLAR_LOG_ERROR(g_logger) << "etcd KeepAlive create fail key=" << info.key
                                      << ", err=" << e.what();
            return false;
        } catch (...) {
            SYLAR_LOG_ERROR(g_logger) << "etcd KeepAlive create unknown error key=" << info.key;
            return false;
        }
        int64_t leaseId = ka->Lease();

        // 2. 绑定租约写入键
        try {
            etcd::Response resp = client->put(info.key, info.value, leaseId);
            if (!resp.is_ok()) {
                SYLAR_LOG_ERROR(g_logger) << "etcd put fail key=" << info.key
                                          << ", err=" << resp.error_message();
                ka->Cancel();   // 停止续租，租约随之过期
                return false;
            }
        } catch (const std::exception& e) {
            SYLAR_LOG_ERROR(g_logger) << "etcd put exception key=" << info.key
                                      << ", err=" << e.what();
            ka->Cancel();
            return false;
        } catch (...) {
            ka->Cancel();
            return false;
        }

        out.info = info;
        out.keepalive = ka;
        out.leaseId = leaseId;
        return true;
    }
};

EtcdRegistrar::EtcdRegistrar(const std::string& endpoint, uint64_t timeout_ms)
    : m_impl(new Impl(endpoint, timeout_ms)) {
}

// RAII：析构撤销全部注册并停止 KeepAlive（销毁 KeepAlive 停止续租 + revoke 删键）。
EtcdRegistrar::~EtcdRegistrar() {
    clear();
}

// 注册一个服务：重复 key 直接告警返回 false。
bool EtcdRegistrar::registerService(const EtcdRegisterInfo& info) {
    if (!m_impl->ready()) {
        return false;
    }
    RWMutex::WriteLock lock(m_impl->mutex);
    if (m_impl->entries.count(info.key)) {
        SYLAR_LOG_WARN(g_logger) << "etcd service already registered: " << info.key;
        return false;
    }
    Impl::Entry e;
    if (!m_impl->doRegisterLocked(info, e)) {
        return false;
    }
    m_impl->entries[info.key] = std::move(e);
    SYLAR_LOG_INFO(g_logger) << "etcd register ok key=" << info.key
                             << " value=" << info.value << " ttl=" << info.ttl;
    return true;
}

// 注销服务：锁内先销毁 KeepAlive 停止续租，锁外做网络操作。
bool EtcdRegistrar::unregisterService(const std::string& key) {
    if (!m_impl->ready()) {
        return false;
    }
    int64_t leaseId = 0;
    {
        RWMutex::WriteLock lock(m_impl->mutex);
        auto it = m_impl->entries.find(key);
        if (it == m_impl->entries.end()) {
            return false;
        }
        leaseId = it->second.leaseId;
        m_impl->entries.erase(it);   // 先销毁 KeepAlive，停止续租
    }
    // 锁外做网络操作，避免长持锁
    if (leaseId > 0) {
        try {
            m_impl->client->leaserevoke(leaseId);   // 撤销租约删除键
        } catch (...) {
        }
    }
    SYLAR_LOG_INFO(g_logger) << "etcd unregister ok key=" << key;
    return true;
}

// 更新服务值：复用已有 lease，重新 put 同租约的新值。
bool EtcdRegistrar::updateService(const EtcdRegisterInfo& info) {
    if (!m_impl->ready()) {
        return false;
    }
    int64_t leaseId = 0;
    {
        RWMutex::ReadLock lock(m_impl->mutex);
        auto it = m_impl->entries.find(info.key);
        if (it == m_impl->entries.end()) {
            SYLAR_LOG_WARN(g_logger) << "etcd updateService not found: " << info.key;
            return false;
        }
        leaseId = it->second.leaseId;
    }
    try {
        etcd::Response resp = m_impl->client->put(info.key, info.value, leaseId);
        if (!resp.is_ok()) {
            SYLAR_LOG_ERROR(g_logger) << "etcd updateService put fail key=" << info.key
                                      << ", err=" << resp.error_message();
            return false;
        }
    } catch (const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << "etcd updateService exception: " << e.what();
        return false;
    } catch (...) {
        return false;
    }
    RWMutex::WriteLock lock(m_impl->mutex);
    auto it = m_impl->entries.find(info.key);
    if (it != m_impl->entries.end()) {
        it->second.info.value = info.value;
    }
    SYLAR_LOG_INFO(g_logger) << "etcd update ok key=" << info.key
                             << " value=" << info.value;
    return true;
}

// 清空所有注册：快照 lease 列表后撤销。
bool EtcdRegistrar::clear() {
    if (!m_impl->ready()) {
        return false;
    }
    std::vector<std::pair<std::string, int64_t>> snapshot;
    {
        RWMutex::WriteLock lock(m_impl->mutex);
        snapshot.reserve(m_impl->entries.size());
        for (auto& kv : m_impl->entries) {
            snapshot.emplace_back(kv.first, kv.second.leaseId);
        }
        m_impl->entries.clear();   // 先停所有 KeepAlive
    }
    bool all_ok = true;
    for (auto& kv : snapshot) {
        if (kv.second > 0) {
            try {
                etcd::Response resp = m_impl->client->leaserevoke(kv.second);
                if (!resp.is_ok()) {
                    all_ok = false;
                }
            } catch (...) {
                all_ok = false;
            }
        }
    }
    return all_ok;
}

std::vector<EtcdRegisterInfo> EtcdRegistrar::haveServices() const {
    RWMutex::ReadLock lock(m_impl->mutex);
    std::vector<EtcdRegisterInfo> out;
    out.reserve(m_impl->entries.size());
    for (auto& kv : m_impl->entries) {
        out.push_back(kv.second.info);
    }
    return out;
}

const std::string& EtcdRegistrar::getEndpoint() const {
    return m_impl->endpoint;
}

// EtcdWatcher::Impl：watch 实现，每个前缀一个 WatchCtx。
struct EtcdWatcher::Impl {
    std::shared_ptr<etcd::SyncClient> client;
    std::string endpoint;

    struct WatchCtx {
        std::string prefix;
        std::unique_ptr<etcd::Watcher> watcher;
    };

    mutable RWMutex mutex;
    std::unordered_map<std::string, WatchCtx> watches;

    Impl(const std::string& ep, uint64_t timeout_ms)
        : endpoint(ep) {
        try {
            client = std::make_shared<etcd::SyncClient>(ep);
            client->set_grpc_timeout(std::chrono::milliseconds(timeout_ms));
        } catch (const std::exception& e) {
            SYLAR_LOG_ERROR(g_logger) << "EtcdWatcher etcd::SyncClient init fail, endpoint=" << ep
                                      << ", err=" << e.what();
        } catch (...) {
            SYLAR_LOG_ERROR(g_logger) << "EtcdWatcher etcd::SyncClient init unknown error";
        }
    }

    bool ready() const {
        return (bool)client;
    }
};

EtcdWatcher::EtcdWatcher(const std::string& endpoint, uint64_t timeout_ms)
    : m_impl(new Impl(endpoint, timeout_ms)) {
}

EtcdWatcher::~EtcdWatcher() {
    cancelAll();
}

// 监听一个前缀：Watcher(client, key, range_end, callback) 监听 [prefix, range_end)。
// 库在后台 gRPC 流上推送事件，回调在工作线程执行。
bool EtcdWatcher::watch(const EtcdWatchInfo& info) {
    if (!m_impl->ready()) {
        return false;
    }
    if (!info.cb) {
        SYLAR_LOG_WARN(g_logger) << "etcd watch cb empty, prefix=" << info.prefix;
        return false;
    }
    RWMutex::WriteLock lock(m_impl->mutex);
    if (m_impl->watches.count(info.prefix)) {
        SYLAR_LOG_WARN(g_logger) << "etcd watch already exists: " << info.prefix;
        return false;
    }

    Impl::WatchCtx ctx;
    ctx.prefix = info.prefix;
    try {
        ctx.watcher.reset(new etcd::Watcher(
            *m_impl->client,
            info.prefix,
            prefixRangeEnd(info.prefix),
            [cb = info.cb](etcd::Response resp) {
                if (!resp.is_ok()) {
                    return;
                }
                for (auto const& ev : resp.events()) {
                    const etcd::Value& v = ev.kv();
                    if (ev.event_type() == etcd::Event::EventType::DELETE_) {
                        cb(ETCD_DELETE, v.key(), "");
                    } else {
                        // PUT / CREATE / SET / CAS 等都视为"有值可用"
                        cb(ETCD_PUT, v.key(), v.as_string());
                    }
                }
            }));
    } catch (const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << "etcd Watcher create fail prefix=" << info.prefix
                                  << ", err=" << e.what();
        return false;
    } catch (...) {
        SYLAR_LOG_ERROR(g_logger) << "etcd Watcher create unknown error prefix=" << info.prefix;
        return false;
    }

    m_impl->watches[info.prefix] = std::move(ctx);
    SYLAR_LOG_INFO(g_logger) << "etcd watch ok prefix=" << info.prefix;
    return true;
}

// 取消单个 watch：先 Cancel 再移除。
bool EtcdWatcher::cancel(const std::string& prefix) {
    RWMutex::WriteLock lock(m_impl->mutex);
    auto it = m_impl->watches.find(prefix);
    if (it == m_impl->watches.end()) {
        return false;
    }
    if (it->second.watcher) {
        it->second.watcher->Cancel();
    }
    m_impl->watches.erase(it);
    return true;
}

void EtcdWatcher::cancelAll() {
    RWMutex::WriteLock lock(m_impl->mutex);
    for (auto& kv : m_impl->watches) {
        if (kv.second.watcher) {
            kv.second.watcher->Cancel();
        }
    }
    m_impl->watches.clear();
}

const std::string& EtcdWatcher::getEndpoint() const {
    return m_impl->endpoint;
}

}   // namespace rpc
}   // namespace sylar
