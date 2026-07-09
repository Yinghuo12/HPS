#ifndef __SYLAR_ORM_CONNECTION_H__
#define __SYLAR_ORM_CONNECTION_H__

#include <mysql/mysql.h>
#include <memory>
#include <string>
#include <map>
#include "sylar/orm/value.h"
#include "sylar/orm/result.h"
#include "sylar/orm/stmt.h"

namespace sylar {

class Transaction;
class ConnectionPool;

// 单条 MySQL 连接的封装。
// 由 ConnectionPool 统一管理生命周期；也可单独构造使用。
class Connection {
friend class ConnectionPool;
public:
    typedef std::shared_ptr<Connection> ptr;

    // 参数键：host / port / user / passwd / dbname / charset / auto_reconnect / pool / timeout
    Connection();
    explicit Connection(const std::map<std::string, std::string>& params);
    ~Connection();

    // 建立连接；已连接且无错误时直接返回 true
    bool connect();
    // 心跳检测
    bool ping();
    // 切换数据库
    bool use(const std::string& dbname);

    // 非查询：返回 0 表示成功
    int execute(const std::string& sql);
    int execute(const char* fmt, ...);

    // 查询：失败返回 nullptr
    Result::ptr query(const std::string& sql);
    Result::ptr query(const char* fmt, ...);

    // 预处理
    PreparedStmt::ptr prepare(const std::string& sql);

    // 事务（绑定到当前连接，事务期间该连接独占）
    std::shared_ptr<Transaction> openTransaction();

    // 字符串转义（基于当前连接的字符集）
    std::string escape(const std::string& s) const;

    int64_t getLastInsertId();
    uint64_t getAffectedRows();

    int getErrno() const;
    std::string getErrStr() const;

    bool hasError() const { return m_hasError; }
    void setHasError(bool v) { m_hasError = v; }
    time_t lastUsedTime() const { return m_lastUsedTime; }
    void touch() { m_lastUsedTime = time(0); }

    const std::map<std::string, std::string>& params() const { return m_params; }
    bool isConnected() const { return m_mysql != nullptr; }
    MYSQL* getRaw() const { return m_mysql; }

    // 连接池容量提示（由池设置，用于归还时判断是否回收）
    void setPoolSize(int n) { m_poolSize = n; }
    int getPoolSize() const { return m_poolSize; }

    bool isNeedCheck() const;

private:
    bool doConnect();

    std::map<std::string, std::string> m_params;
    MYSQL* m_mysql;
    std::string m_dbname;
    std::string m_lastCmd;
    time_t m_lastUsedTime;
    bool m_hasError;
    int m_poolSize;
};

// 线程级 MySQL 初始化（每个线程首次使用 MySQL C API 前调用）
struct MysqlThreadIniter {
    MysqlThreadIniter();
    ~MysqlThreadIniter();
};

// 库级初始化/收尾（进程首尾调用一次即可，重复调用安全）
void MysqlLibraryInit();
void MysqlLibraryEnd();

}

#endif
