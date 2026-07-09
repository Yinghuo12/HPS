#include "sylar/orm/database.h"
#include "sylar/orm/connection.h"
#include "sylar/orm/connection_pool.h"
#include "sylar/orm/result.h"
#include "sylar/core/log.h"

namespace sylar {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

Database::Database()
    : m_conn(nullptr)
    , m_pool(nullptr) {
}

Database::Database(Connection* conn)
    : m_conn(conn)
    , m_pool(nullptr) {
}

Database::Database(Connection& conn)
    : m_conn(&conn)
    , m_pool(nullptr) {
}

Database::Database(ConnectionPool* pool)
    : m_conn(nullptr)
    , m_pool(pool) {
    if(m_pool) {
        m_conn = m_pool->get();
    }
}

Database::Database(ConnectionPool& pool)
    : m_conn(nullptr)
    , m_pool(&pool) {
    m_conn = m_pool->get();
}

Database::~Database() {
    if(m_pool && m_conn) {
        m_pool->put(m_conn);
    }
}

std::vector<std::map<std::string, Value> > Database::query(const std::string& sql) {
    std::vector<std::map<std::string, Value> > empty;
    if(!m_conn) {
        SYLAR_LOG_ERROR(g_logger) << "Database::query no connection";
        return empty;
    }
    Result::ptr r = m_conn->query(sql);
    if(!r) return empty;
    return r->fetchAll();
}

bool Database::execute(const std::string& sql) {
    if(!m_conn) {
        SYLAR_LOG_ERROR(g_logger) << "Database::execute no connection";
        return false;
    }
    return m_conn->execute(sql) == 0;
}

int64_t Database::getLastInsertId() {
    if(!m_conn) return 0;
    return m_conn->getLastInsertId();
}

}
