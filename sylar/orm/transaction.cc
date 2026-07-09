#include "sylar/orm/transaction.h"
#include "sylar/orm/connection.h"
#include "sylar/orm/util.h"
#include "sylar/core/log.h"
#include <stdarg.h>

namespace sylar {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

Transaction::Transaction(Connection* conn)
    : m_conn(conn)
    , m_depth(0)
    , m_finished(false)
    , m_hasError(false) {
}

Transaction::~Transaction() {
    // 未显式结束则整体回滚，避免连接带着未提交事务归还
    if(!m_finished && m_depth > 0 && m_conn) {
        m_conn->execute("ROLLBACK");
        m_finished = true;
    }
}

bool Transaction::begin() {
    if(!m_conn || m_finished) {
        return false;
    }
    std::string sql;
    if(m_depth == 0) {
        sql = "BEGIN";
    } else {
        sql = Format("SAVEPOINT sp_%d", m_depth);
    }
    int r = m_conn->execute(sql);
    if(r) {
        m_hasError = true;
        SYLAR_LOG_ERROR(g_logger) << "transaction begin [" << sql << "] error";
        return false;
    }
    ++m_depth;
    return true;
}

bool Transaction::commit() {
    if(!m_conn || m_depth == 0) {
        return m_depth == 0;
    }
    --m_depth;
    std::string sql;
    if(m_depth == 0) {
        sql = "COMMIT";
    } else {
        sql = Format("RELEASE SAVEPOINT sp_%d", m_depth);
    }
    int r = m_conn->execute(sql);
    if(r) {
        m_hasError = true;
        SYLAR_LOG_ERROR(g_logger) << "transaction commit [" << sql << "] error";
        return false;
    }
    if(m_depth == 0) {
        m_finished = true;
    }
    return true;
}

bool Transaction::rollback() {
    if(!m_conn) {
        return false;
    }
    if(m_depth == 0) {
        m_finished = true;
        return true;
    }
    --m_depth;
    std::string sql;
    if(m_depth == 0) {
        sql = "ROLLBACK";
    } else {
        sql = Format("ROLLBACK TO SAVEPOINT sp_%d", m_depth);
    }
    int r = m_conn->execute(sql);
    if(r) {
        m_hasError = true;
        SYLAR_LOG_ERROR(g_logger) << "transaction rollback [" << sql << "] error";
        return false;
    }
    if(m_depth == 0) {
        m_finished = true;
    }
    return true;
}

int Transaction::execute(const std::string& sql) {
    if(!m_conn) return -1;
    if(m_finished) {
        SYLAR_LOG_ERROR(g_logger) << "transaction is finished, sql=" << sql;
        return -1;
    }
    int r = m_conn->execute(sql);
    if(r) m_hasError = true;
    return r;
}

int Transaction::execute(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::string sql = Formatv(fmt, ap);
    va_end(ap);
    return execute(sql);
}

int64_t Transaction::getLastInsertId() {
    if(!m_conn) return 0;
    return m_conn->getLastInsertId();
}

}
