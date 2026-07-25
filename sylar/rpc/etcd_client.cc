#include "sylar/rpc/etcd_client.h"

#include <etcd/Response.hpp>
#include <etcd/Value.hpp>

#include "sylar/core/log.h"

namespace sylar {
namespace rpc {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("rpc");

// 计算前缀 range_end：最后一字节 +1
static std::string prefixRangeEnd(const std::string& prefix) {
    if(prefix.empty()) {
        return "\0";
    }
    std::string end = prefix;
    for(int i = (int)end.size() - 1; i >= 0; --i) {
        unsigned char c = (unsigned char)end[i];
        if(c < 0xFF) {
            end[i] = (char)(c + 1);
            end.resize(i + 1);
            return end;
        }
    }
    return "\0";
}

// ---- EtcdClient ----

EtcdClient::EtcdClient(const std::string& endpoint, uint64_t timeout_ms)
    : m_endpoint(endpoint) {
    try {
        m_client = std::make_shared<etcd::SyncClient>(endpoint);
        m_client->set_grpc_timeout(std::chrono::milliseconds(timeout_ms));
    } catch(const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << "etcd::SyncClient init fail, endpoint=" << endpoint
                                  << ", err=" << e.what();
    } catch(...) {
        SYLAR_LOG_ERROR(g_logger) << "etcd::SyncClient init unknown error, endpoint=" << endpoint;
    }
}

EtcdClient::~EtcdClient() {
}

bool EtcdClient::put(const std::string& key, const std::string& value, int64_t lease_id) {
    if(!ready()) {
        return false;
    }
    try {
        etcd::Response resp = (lease_id > 0)
            ? m_client->put(key, value, lease_id)
            : m_client->put(key, value);
        if(!resp.is_ok()) {
            SYLAR_LOG_ERROR(g_logger) << "etcd put fail key=" << key
                                      << ", err=" << resp.error_message();
            return false;
        }
        return true;
    } catch(const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << "etcd put exception: " << e.what();
        return false;
    } catch(...) {
        SYLAR_LOG_ERROR(g_logger) << "etcd put unknown exception";
        return false;
    }
}

bool EtcdClient::get(const std::string& key, KV& out) {
    if(!ready()) {
        return false;
    }
    try {
        etcd::Response resp = m_client->get(key);
        if(!resp.is_ok()) {
            return false;
        }
        const etcd::Value& v = resp.value();
        out.key = v.key();
        out.value = v.as_string();
        out.lease = v.lease();
        return true;
    } catch(const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << "etcd get exception: " << e.what();
        return false;
    } catch(...) {
        SYLAR_LOG_ERROR(g_logger) << "etcd get unknown exception";
        return false;
    }
}

bool EtcdClient::getPrefix(const std::string& prefix, std::vector<KV>& out) {
    if(!ready()) {
        return false;
    }
    try {
        etcd::Response resp = m_client->ls(prefix, prefixRangeEnd(prefix));
        if(!resp.is_ok()) {
            return false;
        }
        out.clear();
        out.reserve(resp.keys().size());
        for(int i = 0; i < (int)resp.keys().size(); ++i) {
            KV kv;
            kv.key = resp.key(i);
            kv.value = resp.values()[i].as_string();
            kv.lease = resp.values()[i].lease();
            out.push_back(std::move(kv));
        }
        return true;
    } catch(const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << "etcd getPrefix exception: " << e.what();
        return false;
    } catch(...) {
        SYLAR_LOG_ERROR(g_logger) << "etcd getPrefix unknown exception";
        return false;
    }
}

bool EtcdClient::del(const std::string& key) {
    if(!ready()) {
        return false;
    }
    try {
        etcd::Response resp = m_client->rm(key);
        return resp.is_ok();
    } catch(const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << "etcd del exception: " << e.what();
        return false;
    } catch(...) {
        return false;
    }
}

bool EtcdClient::delPrefix(const std::string& prefix) {
    if(!ready()) {
        return false;
    }
    try {
        etcd::Response resp = m_client->rmdir(prefix, prefixRangeEnd(prefix));
        return resp.is_ok();
    } catch(const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << "etcd delPrefix exception: " << e.what();
        return false;
    } catch(...) {
        return false;
    }
}

int64_t EtcdClient::leaseGrant(int ttl) {
    if(!ready()) {
        return 0;
    }
    try {
        etcd::Response resp = m_client->leasegrant(ttl);
        if(!resp.is_ok()) {
            SYLAR_LOG_ERROR(g_logger) << "etcd leasegrant fail: " << resp.error_message();
            return 0;
        }
        return resp.value().lease();
    } catch(const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << "etcd leasegrant exception: " << e.what();
        return 0;
    } catch(...) {
        return 0;
    }
}

int64_t EtcdClient::leaseKeepalive(int64_t lease_id) {
    if(!ready()) {
        return 0;
    }
    try {
        etcd::Response resp = m_client->leasetimetolive(lease_id);
        if(!resp.is_ok()) {
            return 0;
        }
        return resp.value().ttl() > 0 ? resp.value().ttl() : 0;
    } catch(const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << "etcd leaseKeepalive exception: " << e.what();
        return 0;
    } catch(...) {
        return 0;
    }
}

bool EtcdClient::leaseRevoke(int64_t lease_id) {
    if(!ready()) {
        return false;
    }
    try {
        etcd::Response resp = m_client->leaserevoke(lease_id);
        return resp.is_ok();
    } catch(const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << "etcd leaseRevoke exception: " << e.what();
        return false;
    } catch(...) {
        return false;
    }
}

// ---- EtcdRegistrar ----

EtcdRegistrar::EtcdRegistrar(const std::string& endpoint, uint64_t timeout_ms)
    : m_endpoint(endpoint) {
    try {
        m_client = std::make_shared<etcd::SyncClient>(endpoint);
        m_client->set_grpc_timeout(std::chrono::milliseconds(timeout_ms));
    } catch(const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << "EtcdRegistrar init fail: " << e.what();
    } catch(...) {
        SYLAR_LOG_ERROR(g_logger) << "EtcdRegistrar init unknown error";
    }
}

EtcdRegistrar::~EtcdRegistrar() {
    clear();
}

bool EtcdRegistrar::doRegisterLocked(const EtcdRegisterInfo& info, Entry& out) {
    std::shared_ptr<etcd::KeepAlive> ka;
    try {
        ka = std::make_shared<etcd::KeepAlive>(
            *m_client,
            [info](std::exception_ptr eptr) {
                if(eptr) {
                    try {
                        std::rethrow_exception(eptr);
                    } catch(const std::exception& e) {
                        SYLAR_LOG_ERROR(g_logger) << "etcd KeepAlive fail key="
                                                  << info.key << ", err=" << e.what();
                    } catch(...) {
                        SYLAR_LOG_ERROR(g_logger) << "etcd KeepAlive fail key=" << info.key;
                    }
                }
            },
            info.ttl);
    } catch(const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << "etcd KeepAlive create fail key=" << info.key
                                  << ", err=" << e.what();
        return false;
    } catch(...) {
        SYLAR_LOG_ERROR(g_logger) << "etcd KeepAlive create unknown error key=" << info.key;
        return false;
    }
    int64_t leaseId = ka->Lease();

    try {
        etcd::Response resp = m_client->put(info.key, info.value, leaseId);
        if(!resp.is_ok()) {
            SYLAR_LOG_ERROR(g_logger) << "etcd put fail key=" << info.key
                                      << ", err=" << resp.error_message();
            ka->Cancel();
            return false;
        }
    } catch(const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << "etcd put exception key=" << info.key
                                  << ", err=" << e.what();
        ka->Cancel();
        return false;
    } catch(...) {
        ka->Cancel();
        return false;
    }

    out.info = info;
    out.keepalive = ka;
    out.leaseId = leaseId;
    return true;
}

bool EtcdRegistrar::registerService(const EtcdRegisterInfo& info) {
    if(!m_client) {
        return false;
    }
    RWMutex::WriteLock lock(m_mutex);
    if(m_entries.count(info.key)) {
        SYLAR_LOG_WARN(g_logger) << "etcd service already registered: " << info.key;
        return false;
    }
    Entry e;
    if(!doRegisterLocked(info, e)) {
        return false;
    }
    m_entries[info.key] = std::move(e);
    SYLAR_LOG_INFO(g_logger) << "etcd register ok key=" << info.key
                             << " value=" << info.value << " ttl=" << info.ttl;
    return true;
}

bool EtcdRegistrar::unregisterService(const std::string& key) {
    if(!m_client) {
        return false;
    }
    int64_t leaseId = 0;
    {
        RWMutex::WriteLock lock(m_mutex);
        auto it = m_entries.find(key);
        if(it == m_entries.end()) {
            return false;
        }
        leaseId = it->second.leaseId;
        m_entries.erase(it);
    }
    if(leaseId > 0) {
        try {
            m_client->leaserevoke(leaseId);
        } catch(...) {}
    }
    SYLAR_LOG_INFO(g_logger) << "etcd unregister ok key=" << key;
    return true;
}

bool EtcdRegistrar::updateService(const EtcdRegisterInfo& info) {
    if(!m_client) {
        return false;
    }
    int64_t leaseId = 0;
    {
        RWMutex::ReadLock lock(m_mutex);
        auto it = m_entries.find(info.key);
        if(it == m_entries.end()) {
            SYLAR_LOG_WARN(g_logger) << "etcd updateService not found: " << info.key;
            return false;
        }
        leaseId = it->second.leaseId;
    }
    try {
        etcd::Response resp = m_client->put(info.key, info.value, leaseId);
        if(!resp.is_ok()) {
            SYLAR_LOG_ERROR(g_logger) << "etcd updateService put fail key=" << info.key
                                      << ", err=" << resp.error_message();
            return false;
        }
    } catch(const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << "etcd updateService exception: " << e.what();
        return false;
    } catch(...) {
        return false;
    }
    RWMutex::WriteLock lock(m_mutex);
    auto it = m_entries.find(info.key);
    if(it != m_entries.end()) {
        it->second.info.value = info.value;
    }
    SYLAR_LOG_INFO(g_logger) << "etcd update ok key=" << info.key
                             << " value=" << info.value;
    return true;
}

bool EtcdRegistrar::clear() {
    if(!m_client) {
        return false;
    }
    std::vector<std::pair<std::string, int64_t>> snapshot;
    {
        RWMutex::WriteLock lock(m_mutex);
        snapshot.reserve(m_entries.size());
        for(auto& kv : m_entries) {
            snapshot.emplace_back(kv.first, kv.second.leaseId);
        }
        m_entries.clear();
    }
    bool all_ok = true;
    for(auto& kv : snapshot) {
        if(kv.second > 0) {
            try {
                etcd::Response resp = m_client->leaserevoke(kv.second);
                if(!resp.is_ok()) {
                    all_ok = false;
                }
            } catch(...) {
                all_ok = false;
            }
        }
    }
    return all_ok;
}

std::vector<EtcdRegisterInfo> EtcdRegistrar::haveServices() const {
    RWMutex::ReadLock lock(m_mutex);
    std::vector<EtcdRegisterInfo> out;
    out.reserve(m_entries.size());
    for(auto& kv : m_entries) {
        out.push_back(kv.second.info);
    }
    return out;
}

// ---- EtcdWatcher ----

EtcdWatcher::EtcdWatcher(const std::string& endpoint, uint64_t timeout_ms)
    : m_endpoint(endpoint) {
    try {
        m_client = std::make_shared<etcd::SyncClient>(endpoint);
        m_client->set_grpc_timeout(std::chrono::milliseconds(timeout_ms));
    } catch(const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << "EtcdWatcher init fail: " << e.what();
    } catch(...) {
        SYLAR_LOG_ERROR(g_logger) << "EtcdWatcher init unknown error";
    }
}

EtcdWatcher::~EtcdWatcher() {
    cancelAll();
}

bool EtcdWatcher::watch(const EtcdWatchInfo& info) {
    if(!m_client) {
        return false;
    }
    if(!info.cb) {
        SYLAR_LOG_WARN(g_logger) << "etcd watch cb empty, prefix=" << info.prefix;
        return false;
    }
    RWMutex::WriteLock lock(m_mutex);
    if(m_watches.count(info.prefix)) {
        SYLAR_LOG_WARN(g_logger) << "etcd watch already exists: " << info.prefix;
        return false;
    }

    WatchCtx ctx;
    ctx.prefix = info.prefix;
    try {
        ctx.watcher.reset(new etcd::Watcher(
            *m_client,
            info.prefix,
            prefixRangeEnd(info.prefix),
            [cb = info.cb](etcd::Response resp) {
                if(!resp.is_ok()) {
                    return;
                }
                for(auto const& ev : resp.events()) {
                    const etcd::Value& v = ev.kv();
                    if(ev.event_type() == etcd::Event::EventType::DELETE_) {
                        cb(ETCD_DELETE, v.key(), "");
                    } else {
                        cb(ETCD_PUT, v.key(), v.as_string());
                    }
                }
            }));
    } catch(const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << "etcd Watcher create fail prefix=" << info.prefix
                                  << ", err=" << e.what();
        return false;
    } catch(...) {
        SYLAR_LOG_ERROR(g_logger) << "etcd Watcher create unknown error prefix=" << info.prefix;
        return false;
    }

    m_watches[info.prefix] = std::move(ctx);
    SYLAR_LOG_INFO(g_logger) << "etcd watch ok prefix=" << info.prefix;
    return true;
}

bool EtcdWatcher::cancel(const std::string& prefix) {
    RWMutex::WriteLock lock(m_mutex);
    auto it = m_watches.find(prefix);
    if(it == m_watches.end()) {
        return false;
    }
    if(it->second.watcher) {
        it->second.watcher->Cancel();
    }
    m_watches.erase(it);
    return true;
}

void EtcdWatcher::cancelAll() {
    RWMutex::WriteLock lock(m_mutex);
    for(auto& kv : m_watches) {
        if(kv.second.watcher) {
            kv.second.watcher->Cancel();
        }
    }
    m_watches.clear();
}

}  // namespace rpc
}  // namespace sylar
