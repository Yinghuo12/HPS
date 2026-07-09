#ifndef __SYLAR_ORM_MODEL_H__
#define __SYLAR_ORM_MODEL_H__

#include <string>
#include <vector>
#include <map>
#include <initializer_list>
#include "sylar/orm/value.h"
#include "sylar/orm/query.h"
#include "sylar/orm/connection.h"
#include "sylar/orm/database.h"

namespace sylar {

template<class T> class Batch;

// ORM 实体基类（CRTP）。
//
// 派生类需提供：
//   std::string table() const;         // 表名
//   std::string primary_key() const;   // 主键字段（默认约定 "id"）
//
// 用法示例见 orm.md：链式 where/select/join/... 之后接 one()/all()/save()/... 等终结操作。
template<class T>
class Model {
public:
    Model() : m_conn(nullptr) {}

    Model(Connection* conn) : m_conn(conn), m_query(conn) {}
    Model(Connection& conn) : m_conn(&conn), m_query(&conn) {}
    Model(Database& db) : m_conn(nullptr) {
        Connection* c = db.getConnection();
        m_conn = c;
        m_query.setConnection(c);
    }

    // 字段赋值 / 读取
    Value& operator[](const std::string& k) { return m_data[k]; }
    Value operator()(const std::string& k) const {
        typename std::map<std::string, Value>::const_iterator it = m_data.find(k);
        return it == m_data.end() ? Value() : it->second;
    }
    bool has(const std::string& k) const { return m_data.find(k) != m_data.end(); }

    // 调试输出：表名{字段=值, ...}
    std::string str() const {
        std::string s = tableName() + "{";
        bool first = true;
        for(typename std::map<std::string, Value>::const_iterator it = m_data.begin();
            it != m_data.end(); ++it) {
            if(!first) s += ", ";
            s += it->first + "=" + it->second.str();
            first = false;
        }
        s += "}";
        return s;
    }

    // 从查询行装载（含连接，便于后续 save/remove）
    void fromRow(const std::map<std::string, Value>& row, Connection* conn) {
        m_data = row;
        m_conn = conn;
        m_query.setConnection(conn);
    }

    // ---- 链式子句（返回派生类引用，便于连写）----
    T& where(const std::string& raw) { syncQuery(); m_query.where(raw); return cast(); }
    T& where(const std::string& f, const Value& v) { syncQuery(); m_query.where(f, v); return cast(); }
    T& where(const std::string& f, const std::string& op, const Value& v) {
        syncQuery(); m_query.where(f, op, v); return cast();
    }
    T& where(const std::string& f, const std::string& op, std::initializer_list<Value> list) {
        syncQuery(); m_query.where(f, op, list); return cast();
    }
    T& where(const std::string& f, const std::string& op, const Value& v1, const Value& v2) {
        syncQuery(); m_query.where(f, op, v1, v2); return cast();
    }

    T& select(const std::string& f) { syncQuery(); m_query.select(f); return cast(); }
    T& select(const std::vector<std::string>& fs) { syncQuery(); m_query.select(fs); return cast(); }
    template<class... Args>
    T& select(const std::string& f, const Args&... rest) {
        syncQuery(); m_query.select(f, rest...); return cast();
    }

    T& alias(const std::string& a) { syncQuery(); m_query.alias(a); return cast(); }
    T& join(const std::string& table, const std::string& alias,
            const std::string& on, const std::string& type = "inner") {
        syncQuery(); m_query.join(table, alias, on, type); return cast();
    }
    T& group(const std::string& g) { syncQuery(); m_query.group(g); return cast(); }
    T& having(const std::string& h) { syncQuery(); m_query.having(h); return cast(); }
    T& order(const std::string& o) { syncQuery(); m_query.order(o); return cast(); }
    T& limit(int n) { syncQuery(); m_query.limit(n); return cast(); }
    T& offset(int n) { syncQuery(); m_query.offset(n); return cast(); }

    // ---- CRUD ----

    // 保存：有主键则 UPDATE，否则 INSERT（成功后回填自增主键）
    bool save() {
        if(!m_conn) return false;
        syncQuery();
        std::string pk = pkName();
        typename std::map<std::string, Value>::iterator it = m_data.find(pk);
        bool ok = false;
        if(!pk.empty() && it != m_data.end() && !it->second.isNull()) {
            std::map<std::string, Value> setmap = m_data;
            Value pkVal = it->second;
            m_query.clear();
            syncQuery();
            m_query.where(pk, pkVal);
            ok = m_conn->execute(m_query.buildUpdate(setmap)) == 0;
        } else {
            ok = m_conn->execute(m_query.buildInsert(m_data)) == 0;
            if(ok) {
                int64_t id = m_conn->getLastInsertId();
                if(id > 0 && !pk.empty() && m_data.find(pk) == m_data.end()) {
                    m_data[pk] = Value(id);
                }
            }
        }
        m_query.clear();
        syncQuery();
        return ok;
    }

    // 批量插入
    bool insert(const std::vector<T>& rows) {
        if(!m_conn || rows.empty()) return false;
        syncQuery();
        std::vector<std::map<std::string, Value> > data;
        data.reserve(rows.size());
        for(size_t i = 0; i < rows.size(); ++i) {
            data.push_back(rows[i].m_data);
        }
        bool ok = m_conn->execute(m_query.buildInsertBatch(data)) == 0;
        m_query.clear();
        syncQuery();
        return ok;
    }

    // 条件更新（使用当前 where 与传入的字段集合）
    bool update(const std::map<std::string, Value>& row) {
        if(!m_conn || row.empty()) return false;
        syncQuery();
        bool ok = m_conn->execute(m_query.buildUpdate(row)) == 0;
        m_query.clear();
        syncQuery();
        return ok;
    }

    // 删除：有 where 按条件删，否则按主键删
    bool remove() {
        if(!m_conn) return false;
        syncQuery();
        bool ok = false;
        if(m_query.hasWhere()) {
            ok = m_conn->execute(m_query.buildDelete()) == 0;
        } else {
            std::string pk = pkName();
            typename std::map<std::string, Value>::iterator it = m_data.find(pk);
            if(pk.empty() || it == m_data.end() || it->second.isNull()) {
                m_query.clear();
                syncQuery();
                return false;
            }
            m_query.where(pk, it->second);
            ok = m_conn->execute(m_query.buildDelete()) == 0;
        }
        m_query.clear();
        syncQuery();
        return ok;
    }

    // 清空整张表
    bool truncate() {
        if(!m_conn) return false;
        syncQuery();
        bool ok = m_conn->execute(m_query.buildTruncate()) == 0;
        m_query.clear();
        syncQuery();
        return ok;
    }

    // ---- 查询 ----

    T one() {
        T ret;
        if(!m_conn) return ret;
        syncQuery();
        if(!m_query.hasLimit()) m_query.limit(1);
        std::vector<std::map<std::string, Value> > rows = m_query.get();
        if(!rows.empty()) {
            ret.fromRow(rows[0], m_conn);
        }
        m_query.clear();
        syncQuery();
        return ret;
    }

    std::vector<T> all() {
        std::vector<T> ret;
        if(!m_conn) return ret;
        syncQuery();
        std::vector<std::map<std::string, Value> > rows = m_query.get();
        for(size_t i = 0; i < rows.size(); ++i) {
            T obj;
            obj.fromRow(rows[i], m_conn);
            ret.push_back(obj);
        }
        m_query.clear();
        syncQuery();
        return ret;
    }

    // 分页批量查询，返回可迭代对象；每项为一页（vector<T>）
    Batch<T> batch(int pageSize) {
        syncQuery();
        Batch<T> b(m_conn, m_query, pageSize);
        m_query.clear();
        syncQuery();
        return b;
    }

    // ---- 聚合 ----
    int64_t count() {
        if(!m_conn) return 0;
        syncQuery();
        int64_t c = m_query.count();
        m_query.clear();
        syncQuery();
        return c;
    }
    double sum(const std::string& field) { return aggregateDo("SUM", field); }
    double min(const std::string& field) { return aggregateDo("MIN", field); }
    double max(const std::string& field) { return aggregateDo("MAX", field); }
    double average(const std::string& field) { return aggregateDo("AVG", field); }

    // ---- 单值 / 列 ----
    Value scalar(const std::string& field) {
        if(!m_conn) return Value();
        syncQuery();
        m_query.resetSelect();
        m_query.select(field);
        m_query.limit(1);
        std::vector<std::map<std::string, Value> > rows = m_query.get();
        Value v;
        if(!rows.empty()) {
            typename std::map<std::string, Value>::const_iterator it = rows[0].find(field);
            if(it != rows[0].end()) v = it->second;
        }
        m_query.clear();
        syncQuery();
        return v;
    }

    std::vector<Value> column(const std::string& field) {
        std::vector<Value> ret;
        if(!m_conn) return ret;
        syncQuery();
        m_query.resetSelect();
        m_query.select(field);
        std::vector<std::map<std::string, Value> > rows = m_query.get();
        for(size_t i = 0; i < rows.size(); ++i) {
            typename std::map<std::string, Value>::const_iterator it = rows[i].find(field);
            ret.push_back(it == rows[i].end() ? Value() : it->second);
        }
        m_query.clear();
        syncQuery();
        return ret;
    }

    bool exists() {
        if(!m_conn) return false;
        syncQuery();
        bool e = m_query.exists();
        m_query.clear();
        syncQuery();
        return e;
    }

    // 最近一次生成/执行的 SQL
    std::string sql() const { return m_query.lastSql(); }

protected:
    T& cast() { return static_cast<T&>(*this); }

    void syncQuery() {
        m_query.setConnection(m_conn);
        m_query.setTable(tableName());
    }

    std::string tableName() const { return static_cast<const T*>(this)->table(); }
    std::string pkName() const { return static_cast<const T*>(this)->primary_key(); }

    double aggregateDo(const std::string& func, const std::string& field) {
        if(!m_conn) return 0;
        syncQuery();
        double v = m_query.aggregate(func, field);
        m_query.clear();
        syncQuery();
        return v;
    }

    Connection* m_conn;
    Query m_query;
    std::map<std::string, Value> m_data;
};

// 分页批量查询结果。可按范围 for 迭代，每项是一页（vector<T>）；
// 也可老式迭代器写法，迭代器本身暴露 begin()/end() 以遍历当前页。
template<class T>
class Batch {
public:
    Batch(Connection* conn, const Query& base, int pageSize)
        : m_conn(conn)
        , m_query(base)
        , m_pageSize(pageSize > 0 ? pageSize : 1) {
        m_query.setConnection(conn);
    }

    class iterator {
    public:
        iterator(Batch* b, bool end)
            : m_batch(b)
            , m_offset(0)
            , m_end(end) {
            if(!end) fetch();
        }
        iterator& operator++() {
            if(m_end) return *this;
            m_offset += m_batch->m_pageSize;
            fetch();
            return *this;
        }
        std::vector<T>& operator*() { return m_page; }
        std::vector<T>* operator->() { return &m_page; }
        typename std::vector<T>::iterator begin() { return m_page.begin(); }
        typename std::vector<T>::iterator end() { return m_page.end(); }
        bool operator!=(const iterator& o) const { return m_end != o.m_end; }
        bool operator==(const iterator& o) const { return m_end == o.m_end; }
    private:
        void fetch() {
            m_page.clear();
            if(!m_batch->m_conn) { m_end = true; return; }
            Query& q = m_batch->m_query;
            q.limit(m_batch->m_pageSize);
            q.offset(m_offset);
            std::vector<std::map<std::string, Value> > rows = q.get();
            if(rows.empty()) { m_end = true; return; }
            for(size_t i = 0; i < rows.size(); ++i) {
                T obj;
                obj.fromRow(rows[i], m_batch->m_conn);
                m_page.push_back(obj);
            }
            m_end = false;
        }
        Batch* m_batch;
        std::vector<T> m_page;
        int m_offset;
        bool m_end;
    };

    iterator begin() { return iterator(this, false); }
    iterator end() { return iterator(this, true); }

private:
    friend class iterator;
    Connection* m_conn;
    Query m_query;
    int m_pageSize;
};

}

#endif
