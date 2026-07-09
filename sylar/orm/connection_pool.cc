#include "sylar/orm/connection_pool.h"
#include "sylar/orm/util.h"
#include "sylar/core/log.h"
#include <chrono>
#include <vector>
#include <time.h>

namespace sylar {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

ConnectionPool::ConnectionPool()
    : m_maxSize(10)
    , m_created(0)
    , m_pingInterval(3600)
    , m_closing(false)
    , m_run(false)
    , m_hbThread(nullptr) {
}

ConnectionPool::~ConnectionPool() {
    stopHeartbeat();
    {
        std::lock_guard<MutexType> lock(m_mutex);
        m_closing = true;
        m_cv.notify_all();
        for(auto c : m_conns) {
            delete c;
        }
        m_conns.clear();
    }
}

bool ConnectionPool::create(const std::string& host, int port,
                            const std::string& user, const std::string& passwd,
                            const std::string& dbname,
                            const std::string& charset,
                            bool auto_reconnect) {
    std::map<std::string, std::string> params;
    params["host"] = host;
    params["port"] = std::to_string(port);
    params["user"] = user;
    params["passwd"] = passwd;
    params["dbname"] = dbname;
    params["charset"] = charset;
    params["auto_reconnect"] = auto_reconnect ? "true" : "false";
    return create(params);
}

bool ConnectionPool::create(const std::map<std::string, std::string>& params) {
    {
        std::lock_guard<MutexType> lock(m_mutex);
        m_params = params;
        if(m_created != 0) {
            SYLAR_LOG_WARN(g_logger) << "ConnectionPool::create called more than once";
        }
    }
    int warm = GetParamValue(params, "pool", m_maxSize);
    warmup(warm);
    startHeartbeat();
    return true;
}

Connection* ConnectionPool::newConn() {
    Connection* c = new Connection(m_params);
    c->setPoolSize(m_maxSize);
    if(!c->connect()) {
        SYLAR_LOG_ERROR(g_logger) << "ConnectionPool new connection connect fail";
        delete c;
        return nullptr;
    }
    return c;
}

void ConnectionPool::warmup(int n) {
    if(n <= 0) n = m_maxSize;
    for(int i = 0; i < n; ++i) {
        bool canCreate = false;
        {
            std::lock_guard<MutexType> lock(m_mutex);
            if(m_created < m_maxSize) {
                ++m_created;
                canCreate = true;
            }
        }
        if(!canCreate) break;
        Connection* c = newConn();
        if(c) {
            std::lock_guard<MutexType> lock(m_mutex);
            m_conns.push_back(c);
            m_cv.notify_one();
        } else {
            std::lock_guard<MutexType> lock(m_mutex);
            --m_created;
        }
    }
}

bool ConnectionPool::checkAndFix(Connection* c) {
    if(!c) return false;
    if(!c->isNeedCheck()) return true;
    if(c->ping()) return true;
    SYLAR_LOG_WARN(g_logger) << "ConnectionPool: connection lost, reconnecting";
    if(c->connect()) return true;
    return false;
}

Connection* ConnectionPool::get() {
    while(true) {
        Connection* c = nullptr;
        bool doCreate = false;
        {
            std::unique_lock<MutexType> lock(m_mutex);
            m_cv.wait(lock, [this]{
                return m_closing || !m_conns.empty() || m_created < m_maxSize;
            });
            if(m_closing) return nullptr;
            if(!m_conns.empty()) {
                c = m_conns.front();
                m_conns.pop_front();
            } else {
                // 谓词成立且无空闲，说明还有创建名额
                ++m_created;
                doCreate = true;
            }
        }
        if(doCreate) {
            Connection* nc = newConn();
            if(nc) {
                nc->touch();
                return nc;
            }
            std::lock_guard<MutexType> lock(m_mutex);
            --m_created;
            m_cv.notify_one();
            continue;
        }
        // 从空闲取到 c，健康检查（持锁外做，避免长时间持锁）
        if(checkAndFix(c)) {
            c->touch();
            return c;
        }
        delete c;
        std::lock_guard<MutexType> lock(m_mutex);
        if(m_created > 0) --m_created;
        m_cv.notify_one();
        // 继续重试
    }
}

bool ConnectionPool::put(Connection* c) {
    if(!c) return false;
    {
        std::lock_guard<MutexType> lock(m_mutex);
        m_conns.push_back(c);
    }
    m_cv.notify_one();
    return true;
}

void ConnectionPool::checkConnections(int sec) {
    time_t now = time(0);
    std::vector<Connection*> toDel;
    {
        std::lock_guard<MutexType> lock(m_mutex);
        for(auto it = m_conns.begin(); it != m_conns.end();) {
            if((long)(now - (*it)->lastUsedTime()) >= (long)sec) {
                toDel.push_back(*it);
                it = m_conns.erase(it);
                if(m_created > 0) --m_created;
            } else {
                ++it;
            }
        }
    }
    for(auto c : toDel) {
        delete c;
    }
}

int ConnectionPool::idleCount() {
    std::lock_guard<MutexType> lock(m_mutex);
    return (int)m_conns.size();
}

void ConnectionPool::startHeartbeat() {
    bool expected = false;
    if(!m_run.compare_exchange_strong(expected, true)) {
        return;
    }
    m_hbThread = new (std::nothrow) std::thread(&ConnectionPool::heartbeatLoop, this);
}

void ConnectionPool::stopHeartbeat() {
    bool expected = true;
    if(!m_run.compare_exchange_strong(expected, false)) {
        return;
    }
    {
        std::lock_guard<std::mutex> lk(m_hbMutex);
        m_hbCV.notify_all();
    }
    if(m_hbThread) {
        if(m_hbThread->joinable()) m_hbThread->join();
        delete m_hbThread;
        m_hbThread = nullptr;
    }
}

void ConnectionPool::heartbeatLoop() {
    while(m_run.load()) {
        {
            std::unique_lock<std::mutex> lk(m_hbMutex);
            int secs = m_pingInterval > 0 ? m_pingInterval : 3600;
            m_hbCV.wait_for(lk, std::chrono::seconds(secs),
                            [this]{ return !m_run.load(); });
        }
        if(!m_run.load()) break;
        // 对所有空闲连接做心跳；失败则标记，get 时自动重连修复
        std::vector<Connection*> snapshot;
        {
            std::lock_guard<MutexType> lock(m_mutex);
            snapshot.assign(m_conns.begin(), m_conns.end());
        }
        for(auto c : snapshot) {
            if(!c->ping()) {
                SYLAR_LOG_WARN(g_logger) << "ConnectionPool heartbeat ping fail";
                c->setHasError(true);
            }
        }
    }
}

}
