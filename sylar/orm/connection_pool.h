#ifndef __SYLAR_ORM_CONNECTION_POOL_H__
#define __SYLAR_ORM_CONNECTION_POOL_H__

#include "sylar/orm/connection.h"
#include <list>
#include <map>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace sylar {

// MySQL 连接池。
//
//   - size(n)              设置容量上限
//   - create(...)          用连接参数初始化并预热连接
//   - get() / put()        借出 / 归还（池空且已达容量上限时阻塞等待）
//   - ping(sec)            设置后台心跳间隔（默认 3600s），定时对空闲连接 ping
//   - checkConnections(s)  清理空闲超过 s 秒的连接
//
// 借出连接时做健康检查：空闲较久或曾报错则 ping，失败则尝试重连，
// 仍失败则丢弃并新建，实现「自动重连、修复失效连接」。
class ConnectionPool {
public:
    typedef std::mutex MutexType;

    ConnectionPool();
    ~ConnectionPool();

    void size(int n) { m_maxSize = n; }
    int size() const { return m_maxSize; }

    void ping(int sec) { m_pingInterval = sec > 0 ? sec : 3600; }
    int ping() const { return m_pingInterval; }

    // 便捷构造（对齐 orm.md：host, port, user, password, dbname, charset, auto_reconnect）
    bool create(const std::string& host, int port,
                const std::string& user, const std::string& passwd,
                const std::string& dbname,
                const std::string& charset = "utf8",
                bool auto_reconnect = true);

    bool create(const std::map<std::string, std::string>& params);

    // 预热 n 条连接（0 或负数表示预热到容量上限）
    void warmup(int n = 0);

    // 借出连接（池空且已达容量上限时阻塞等待）
    Connection* get();
    // 归还连接
    bool put(Connection* c);

    // 清理空闲超过 sec 秒的连接
    void checkConnections(int sec);

    // 当前空闲连接数
    int idleCount();

    const std::map<std::string, std::string>& params() const { return m_params; }

private:
    Connection* newConn();
    bool checkAndFix(Connection* c);
    void heartbeatLoop();
    void startHeartbeat();
    void stopHeartbeat();

    mutable MutexType m_mutex;
    std::condition_variable m_cv;          // 借还协调：有空闲或有空位时唤醒
    std::list<Connection*> m_conns;
    std::map<std::string, std::string> m_params;
    int m_maxSize;
    int m_created;
    int m_pingInterval;
    bool m_closing;

    std::atomic<bool> m_run;
    std::thread* m_hbThread;
    std::mutex m_hbMutex;
    std::condition_variable m_hbCV;
};

}

#endif
