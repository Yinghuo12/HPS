#ifndef __SYLAR_ORM_DATABASE_H__
#define __SYLAR_ORM_DATABASE_H__

#include <string>
#include <vector>
#include <map>
#include "sylar/orm/value.h"

namespace sylar {

class Connection;
class ConnectionPool;
class Result;

// Model 的入口适配层。
// 可由单条 Connection（借用，不持有）或 ConnectionPool（借出，析构时归还）构造。
// 同时提供 query / execute 的薄封装，供原生 SQL 场景使用（与 orm.md 的 db.query 一致）。
class Database {
public:
    Database();
    Database(Connection* conn);
    Database(Connection& conn);
    Database(ConnectionPool* pool);
    Database(ConnectionPool& pool);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    Connection* getConnection() const { return m_conn; }
    bool valid() const { return m_conn != nullptr; }

    // 原生查询：物化为 字段名->值 的行集合
    std::vector<std::map<std::string, Value> > query(const std::string& sql);
    // 原生执行（增删改），返回是否成功
    bool execute(const std::string& sql);
    int64_t getLastInsertId();

private:
    Connection* m_conn;
    ConnectionPool* m_pool;   // 非空表示从池借出，析构时归还
};

}

#endif
