#include "sylar/orm/query.h"
#include "sylar/orm/connection.h"
#include "sylar/orm/result.h"
#include "sylar/orm/util.h"
#include "sylar/core/log.h"
#include <stdio.h>
#include <algorithm>

namespace sylar {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

static std::string toLower(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(), ::tolower);
    return r;
}

Query::Query()
    : m_conn(nullptr)
    , m_limit(-1)
    , m_offset(-1) {
}

Query::Query(Connection* conn)
    : m_conn(conn)
    , m_limit(-1)
    , m_offset(-1) {
}

Query::Query(Connection* conn, const std::string& table)
    : m_conn(conn)
    , m_table(table)
    , m_limit(-1)
    , m_offset(-1) {
}

Query& Query::select(const std::string& field) {
    m_select.push_back(field);
    return *this;
}

Query& Query::select(const std::vector<std::string>& fields) {
    for(size_t i = 0; i < fields.size(); ++i) {
        m_select.push_back(fields[i]);
    }
    return *this;
}

Query& Query::join(const std::string& table, const std::string& alias,
                   const std::string& on, const std::string& type) {
    Join j;
    j.table = table;
    j.alias = alias;
    j.on = on;
    j.type = type.empty() ? "inner" : toLower(type);
    m_joins.push_back(j);
    return *this;
}

Query& Query::where(const std::string& raw) {
    if(!raw.empty()) m_wheres.push_back("(" + raw + ")");
    return *this;
}

Query& Query::where(const std::string& field, const Value& v) {
    if(v.isNull()) {
        m_wheres.push_back("(" + field + " IS NULL)");
    } else {
        m_wheres.push_back("(" + field + " = " + v.toSql() + ")");
    }
    return *this;
}

Query& Query::where(const std::string& field, const std::string& op, const Value& v) {
    std::string o = toLower(op);
    if(o == "=" && v.isNull()) {
        m_wheres.push_back("(" + field + " IS NULL)");
    } else if((o == "!=" || o == "<>") && v.isNull()) {
        m_wheres.push_back("(" + field + " IS NOT NULL)");
    } else if(o == "is") {
        m_wheres.push_back("(" + field + " IS " + v.toSql() + ")");
    } else if(o == "is not") {
        m_wheres.push_back("(" + field + " IS NOT " + v.toSql() + ")");
    } else if(o == "like" || o == "not like" || o == "=" || o == "!=" || o == "<>" ||
              o == "<" || o == "<=" || o == ">" || o == ">=") {
        m_wheres.push_back("(" + field + " " + op + " " + v.toSql() + ")");
    } else {
        SYLAR_LOG_WARN(g_logger) << "Query::where unsupported op: " << op
            << ", treat as raw";
        m_wheres.push_back("(" + field + " " + op + " " + v.toSql() + ")");
    }
    return *this;
}

Query& Query::where(const std::string& field, const std::string& op,
                    std::initializer_list<Value> list) {
    std::string o = toLower(op);
    std::string vals;
    for(auto it = list.begin(); it != list.end(); ++it) {
        if(it != list.begin()) vals += ",";
        vals += it->toSql();
    }
    if(o == "not in") {
        m_wheres.push_back("(" + field + " NOT IN (" + vals + "))");
    } else {
        // in 或未知，默认按 in
        m_wheres.push_back("(" + field + " IN (" + vals + "))");
    }
    return *this;
}

Query& Query::where(const std::string& field, const std::string& op,
                    const Value& v1, const Value& v2) {
    std::string o = toLower(op);
    if(o == "between") {
        m_wheres.push_back("(" + field + " BETWEEN " + v1.toSql() + " AND " + v2.toSql() + ")");
    } else if(o == "not between") {
        m_wheres.push_back("(" + field + " NOT BETWEEN " + v1.toSql() + " AND " + v2.toSql() + ")");
    } else {
        // 退化：两个条件用 AND 连接
        m_wheres.push_back("(" + field + " " + op + " " + v1.toSql() +
                           " AND " + field + " " + op + " " + v2.toSql() + ")");
    }
    return *this;
}

Query& Query::clear() {
    m_select.clear();
    m_joins.clear();
    m_wheres.clear();
    m_group.clear();
    m_having.clear();
    m_order.clear();
    m_alias.clear();
    m_limit = -1;
    m_offset = -1;
    m_lastSql.clear();
    return *this;
}

std::string Query::buildFrom() const {
    std::string sql = " FROM " + m_table;
    if(!m_alias.empty()) sql += " AS " + m_alias;
    for(size_t i = 0; i < m_joins.size(); ++i) {
        const Join& j = m_joins[i];
        sql += " " + j.type + " JOIN " + j.table;
        if(!j.alias.empty()) sql += " AS " + j.alias;
        if(!j.on.empty()) sql += " ON " + j.on;
    }
    return sql;
}

std::string Query::buildWhere() const {
    if(m_wheres.empty()) return "";
    std::string sql = " WHERE ";
    for(size_t i = 0; i < m_wheres.size(); ++i) {
        if(i) sql += " AND ";
        sql += m_wheres[i];
    }
    return sql;
}

std::string Query::buildTail() const {
    std::string sql;
    if(!m_group.empty())   sql += " GROUP BY " + m_group;
    if(!m_having.empty())  sql += " HAVING " + m_having;
    if(!m_order.empty())   sql += " ORDER BY " + m_order;
    if(m_offset > 0)       { char b[32]; snprintf(b, sizeof(b), "%d", m_offset); sql += " OFFSET " + std::string(b); }
    if(m_limit > 0)        { char b[32]; snprintf(b, sizeof(b), "%d", m_limit);  sql += " LIMIT " + std::string(b); }
    return sql;
}

static std::string buildSelectList(const std::vector<std::string>& sel) {
    if(sel.empty()) return "*";
    std::string s;
    for(size_t i = 0; i < sel.size(); ++i) {
        if(i) s += ",";
        s += sel[i];
    }
    return s;
}

std::string Query::buildSelect() const {
    std::string sql = "SELECT " + buildSelectList(m_select) + buildFrom() +
                      buildWhere() + buildTail();
    m_lastSql = sql;
    return sql;
}

std::string Query::buildCount() const {
    std::string sql = "SELECT COUNT(*) AS cnt" + buildFrom() + buildWhere();
    m_lastSql = sql;
    return sql;
}

std::string Query::buildAggregate(const std::string& func, const std::string& field) const {
    std::string f = field.empty() ? "*" : field;
    std::string sql = "SELECT " + func + "(" + f + ") AS agg" + buildFrom() + buildWhere();
    m_lastSql = sql;
    return sql;
}

std::string Query::buildExists() const {
    std::string sql = "SELECT 1" + buildFrom() + buildWhere() + " LIMIT 1";
    m_lastSql = sql;
    return sql;
}

std::string Query::buildInsert(const std::map<std::string, Value>& row) const {
    if(row.empty()) return "";
    std::string fields, vals;
    bool first = true;
    for(auto it = row.begin(); it != row.end(); ++it) {
        if(!first) { fields += ","; vals += ","; }
        fields += it->first;
        vals += it->second.toSql();
        first = false;
    }
    std::string sql = "INSERT INTO " + m_table + " (" + fields + ") VALUES (" + vals + ")";
    m_lastSql = sql;
    return sql;
}

std::string Query::buildInsertBatch(const std::vector<std::map<std::string, Value> >& rows) const {
    if(rows.empty()) return "";
    // 以第一行的字段顺序为准
    std::vector<std::string> order;
    for(auto it = rows[0].begin(); it != rows[0].end(); ++it) {
        order.push_back(it->first);
    }
    std::string fields;
    for(size_t i = 0; i < order.size(); ++i) {
        if(i) fields += ",";
        fields += order[i];
    }
    std::string sql = "INSERT INTO " + m_table + " (" + fields + ") VALUES ";
    for(size_t r = 0; r < rows.size(); ++r) {
        if(r) sql += ",";
        sql += "(";
        for(size_t i = 0; i < order.size(); ++i) {
            if(i) sql += ",";
            auto it = rows[r].find(order[i]);
            if(it != rows[r].end()) sql += it->second.toSql();
            else sql += "NULL";
        }
        sql += ")";
    }
    m_lastSql = sql;
    return sql;
}

std::string Query::buildUpdate(const std::map<std::string, Value>& row) const {
    if(row.empty()) return "";
    std::string sets;
    bool first = true;
    for(auto it = row.begin(); it != row.end(); ++it) {
        if(!first) sets += ",";
        sets += it->first + "=" + it->second.toSql();
        first = false;
    }
    std::string sql = "UPDATE " + m_table + " SET " + sets + buildWhere();
    m_lastSql = sql;
    return sql;
}

std::string Query::buildDelete() const {
    std::string sql = "DELETE" + buildFrom() + buildWhere();
    m_lastSql = sql;
    return sql;
}

std::string Query::buildTruncate() const {
    std::string sql = "TRUNCATE TABLE " + m_table;
    m_lastSql = sql;
    return sql;
}

// ---- 执行 ----
std::vector<std::map<std::string, Value> > Query::get() {
    std::vector<std::map<std::string, Value> > empty;
    if(!m_conn) {
        SYLAR_LOG_ERROR(g_logger) << "Query::get need connection";
        return empty;
    }
    std::string sql = buildSelect();
    Result::ptr r = m_conn->query(sql);
    if(!r) return empty;
    return r->fetchAll();
}

bool Query::exec(const std::string& sql) {
    if(!m_conn) {
        SYLAR_LOG_ERROR(g_logger) << "Query::exec need connection";
        return false;
    }
    m_lastSql = sql;
    return m_conn->execute(sql) == 0;
}

int64_t Query::executeReturnAffected(const std::string& sql) {
    if(!m_conn) return -1;
    m_lastSql = sql;
    int r = m_conn->execute(sql);
    if(r != 0) return -1;
    return (int64_t)m_conn->getAffectedRows();
}

int64_t Query::count() {
    if(!m_conn) return -1;
    std::string sql = buildCount();
    Result::ptr r = m_conn->query(sql);
    if(!r || !r->next()) return 0;
    return r->getInt("cnt");
}

double Query::aggregate(const std::string& func, const std::string& field) {
    if(!m_conn) return 0;
    std::string sql = buildAggregate(func, field);
    Result::ptr r = m_conn->query(sql);
    if(!r || !r->next()) return 0;
    return r->getDouble("agg");
}

bool Query::exists() {
    if(!m_conn) return false;
    std::string sql = buildExists();
    Result::ptr r = m_conn->query(sql);
    if(!r) return false;
    return r->next();
}

}
