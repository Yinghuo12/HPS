#include "sylar/orm/connection.h"
#include "sylar/orm/transaction.h"
#include "sylar/orm/util.h"
#include "sylar/core/log.h"
#include "sylar/core/sys_util.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>

namespace sylar {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

// ---------------- 库/线程级初始化 ----------------
static int g_lib_init_ref = 0;

void MysqlLibraryInit() {
    if(g_lib_init_ref++ == 0) {
        mysql_library_init(0, nullptr, nullptr);
    }
}
void MysqlLibraryEnd() {
    if(g_lib_init_ref > 0 && --g_lib_init_ref == 0) {
        mysql_library_end();
    }
}

MysqlThreadIniter::MysqlThreadIniter() {
    mysql_thread_init();
}
MysqlThreadIniter::~MysqlThreadIniter() {
    mysql_thread_end();
}

// ---------------- 连接建立 ----------------
static MYSQL* raw_connect(const std::map<std::string, std::string>& params) {
    static thread_local MysqlThreadIniter s_thread_initer;
    (void)s_thread_initer;

    MYSQL* m = ::mysql_init(nullptr);
    if(!m) {
        SYLAR_LOG_ERROR(g_logger) << "mysql_init error";
        return nullptr;
    }

    int timeout = GetParamValue(params, "timeout", 0);
    if(timeout > 0) {
        mysql_options(m, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
    }
    bool reconnect = GetParamValue<bool>(params, "auto_reconnect", true);
    mysql_options(m, MYSQL_OPT_RECONNECT, &reconnect);
    std::string charset = GetParamValue<std::string>(params, "charset", std::string("utf8mb4"));
    mysql_options(m, MYSQL_SET_CHARSET_NAME, charset.c_str());

    std::string host = GetParamValue<std::string>(params, "host", std::string("127.0.0.1"));
    int port = GetParamValue(params, "port", 3306);
    std::string user = GetParamValue<std::string>(params, "user");
    std::string passwd = GetParamValue<std::string>(params, "passwd");
    std::string dbname = GetParamValue<std::string>(params, "dbname");

    if(::mysql_real_connect(m, host.c_str(), user.c_str(), passwd.c_str(),
                            dbname.empty() ? nullptr : dbname.c_str(),
                            (unsigned int)port, nullptr, 0) == nullptr) {
        SYLAR_LOG_ERROR(g_logger) << "mysql_real_connect(" << host << ":" << port
            << "," << dbname << ") error: " << mysql_error(m);
        mysql_close(m);
        return nullptr;
    }
    return m;
}

Connection::Connection()
    : m_mysql(nullptr)
    , m_lastUsedTime(0)
    , m_hasError(false)
    , m_poolSize(10) {
    MysqlLibraryInit();
}

Connection::Connection(const std::map<std::string, std::string>& params)
    : m_params(params)
    , m_mysql(nullptr)
    , m_lastUsedTime(0)
    , m_hasError(false)
    , m_poolSize(GetParamValue(params, "pool", 10)) {
    MysqlLibraryInit();
}

Connection::~Connection() {
    if(m_mysql) {
        mysql_close(m_mysql);
        m_mysql = nullptr;
    }
}

bool Connection::doConnect() {
    if(m_mysql && !m_hasError) {
        return true;
    }
    if(m_mysql) {
        mysql_close(m_mysql);
        m_mysql = nullptr;
    }
    MYSQL* m = raw_connect(m_params);
    if(!m) {
        m_hasError = true;
        return false;
    }
    m_mysql = m;
    m_hasError = false;
    m_dbname = GetParamValue<std::string>(m_params, "dbname");
    m_lastUsedTime = time(0);
    return true;
}

bool Connection::connect() {
    return doConnect();
}

bool Connection::ping() {
    if(!m_mysql) {
        return doConnect();
    }
    if(mysql_ping(m_mysql)) {
        m_hasError = true;
        return false;
    }
    m_hasError = false;
    return true;
}

bool Connection::use(const std::string& dbname) {
    if(!m_mysql) return false;
    if(m_dbname == dbname) return true;
    if(mysql_select_db(m_mysql, dbname.c_str()) == 0) {
        m_dbname = dbname;
        m_hasError = false;
        return true;
    }
    m_hasError = true;
    return false;
}

int Connection::execute(const std::string& sql) {
    if(!m_mysql && !doConnect()) return -1;
    m_lastCmd = sql;
    int r = ::mysql_query(m_mysql, sql.c_str());
    m_hasError = (r != 0);
    m_lastUsedTime = time(0);
    if(r) {
        SYLAR_LOG_ERROR(g_logger) << "execute [" << sql << "] error: " << getErrStr();
    }
    return r;
}

int Connection::execute(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::string sql = Formatv(fmt, ap);
    va_end(ap);
    return execute(sql);
}

Result::ptr Connection::query(const std::string& sql) {
    if(!m_mysql && !doConnect()) return nullptr;
    m_lastCmd = sql;
    if(::mysql_query(m_mysql, sql.c_str())) {
        m_hasError = true;
        m_lastUsedTime = time(0);
        SYLAR_LOG_ERROR(g_logger) << "query [" << sql << "] error: " << getErrStr();
        return nullptr;
    }
    MYSQL_RES* res = mysql_store_result(m_mysql);
    m_hasError = false;
    m_lastUsedTime = time(0);
    if(!res) {
        // 非查询语句或取结果失败
        if(mysql_field_count(m_mysql) != 0) {
            SYLAR_LOG_ERROR(g_logger) << "query [" << sql << "] store_result error: " << getErrStr();
            m_hasError = true;
        }
        return nullptr;
    }
    return Result::ptr(new Result(res, mysql_errno(m_mysql), mysql_error(m_mysql)));
}

Result::ptr Connection::query(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::string sql = Formatv(fmt, ap);
    va_end(ap);
    return query(sql);
}

PreparedStmt::ptr Connection::prepare(const std::string& sql) {
    if(!m_mysql && !doConnect()) return nullptr;
    m_lastUsedTime = time(0);
    return PreparedStmt::Create(m_mysql, sql);
}

std::shared_ptr<Transaction> Connection::openTransaction() {
    return std::shared_ptr<Transaction>(new Transaction(this));
}

std::string Connection::escape(const std::string& s) const {
    if(!m_mysql) {
        // 没有连接时退化为通用转义
        return MysqlEscape(s);
    }
    std::string out(s.size() * 2 + 1, '\0');
    unsigned long n = mysql_real_escape_string(m_mysql, &out[0], s.c_str(), (unsigned long)s.size());
    out.resize(n);
    return out;
}

int64_t Connection::getLastInsertId() {
    if(!m_mysql) return 0;
    return (int64_t)mysql_insert_id(m_mysql);
}

uint64_t Connection::getAffectedRows() {
    if(!m_mysql) return 0;
    return (uint64_t)mysql_affected_rows(m_mysql);
}

int Connection::getErrno() const {
    if(!m_mysql) return -1;
    return mysql_errno(m_mysql);
}

std::string Connection::getErrStr() const {
    if(!m_mysql) return "mysql is null";
    const char* e = mysql_error(m_mysql);
    return e ? e : "";
}

bool Connection::isNeedCheck() const {
    // 5 秒内用过且无错误，跳过健康检查
    if(!m_hasError && (time(0) - m_lastUsedTime) < 5) {
        return false;
    }
    return true;
}

}
