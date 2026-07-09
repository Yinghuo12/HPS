#ifndef __SYLAR_ORM_QUERY_H__
#define __SYLAR_ORM_QUERY_H__

#include <string>
#include <vector>
#include <map>
#include <initializer_list>
#include "sylar/orm/value.h"

namespace sylar {

class Connection;
class Result;

// 链式 SQL 查询构建器。
// 既可作为纯 SQL 生成器（buildXxx 系列返回字符串），也可在持有 Connection 时直接执行。
// where 条件之间默认以 AND 连接。
class Query {
public:
    Query();
    explicit Query(Connection* conn);
    Query(Connection* conn, const std::string& table);

    void setConnection(Connection* c) { m_conn = c; }
    void setTable(const std::string& t) { m_table = t; }
    const std::string& table() const { return m_table; }

    // 单字段 select；模板变参支持 select("a","b","c")
    Query& select(const std::string& field);
    Query& select(const std::vector<std::string>& fields);
    template<class... Args>
    Query& select(const std::string& field, const Args&... rest) {
        m_select.push_back(field);
        return select(rest...);
    }

    Query& alias(const std::string& a) { m_alias = a; return *this; }

    // type: inner / left / right，默认 inner
    Query& join(const std::string& table, const std::string& alias,
                const std::string& on, const std::string& type = "inner");

    // ---- where 多重载 ----
    Query& where(const std::string& raw);                                              // 原生字符串
    Query& where(const std::string& field, const Value& v);                            // 等值
    Query& where(const std::string& field, const std::string& op, const Value& v);     // 通用运算符
    Query& where(const std::string& field, const std::string& op,                      // in / not in
                 std::initializer_list<Value> list);
    Query& where(const std::string& field, const std::string& op,                      // between
                 const Value& v1, const Value& v2);

    Query& group(const std::string& g) { m_group = g; return *this; }
    Query& having(const std::string& h) { m_having = h; return *this; }
    Query& order(const std::string& o) { m_order = o; return *this; }
    Query& limit(int n) { m_limit = n; return *this; }
    Query& offset(int n) { m_offset = n; return *this; }

    // 仅清空 select 投影列表（保留 where 等其它子句）
    Query& resetSelect() { m_select.clear(); return *this; }

    // 清空查询子句（保留表名与连接）
    Query& clear();

    // ---- SQL 生成 ----
    std::string buildSelect() const;
    std::string buildCount() const;
    std::string buildAggregate(const std::string& func, const std::string& field) const;
    std::string buildExists() const;
    std::string buildInsert(const std::map<std::string, Value>& row) const;
    std::string buildInsertBatch(const std::vector<std::map<std::string, Value> >& rows) const;
    std::string buildUpdate(const std::map<std::string, Value>& row) const;
    std::string buildDelete() const;
    std::string buildTruncate() const;

    std::string valueToSql(const Value& v) const { return v.toSql(); }

    bool hasWhere() const { return !m_wheres.empty(); }
    bool hasLimit() const { return m_limit > 0; }

    // ---- 执行（需要 connection）----
    std::vector<std::map<std::string, Value> > get();
    bool exec(const std::string& sql);
    int64_t count();
    double aggregate(const std::string& func, const std::string& field);
    bool exists();
    int64_t executeReturnAffected(const std::string& sql);

    const std::string& lastSql() const { return m_lastSql; }

private:
    std::string buildWhere() const;
    std::string buildFrom() const;        // FROM table [AS alias] [JOIN ...]
    std::string buildTail() const;        // GROUP/HAVING/ORDER/LIMIT/OFFSET

    Connection* m_conn;
    std::string m_table;
    std::string m_alias;
    std::vector<std::string> m_select;
    struct Join { std::string table, alias, on, type; };
    std::vector<Join> m_joins;
    std::vector<std::string> m_wheres;
    std::string m_group;
    std::string m_having;
    std::string m_order;
    int m_limit;
    int m_offset;
    mutable std::string m_lastSql;
};

}

#endif
