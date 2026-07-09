#include "sylar/orm/stmt.h"
#include "sylar/orm/util.h"
#include "sylar/core/log.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <new>

namespace sylar {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

static inline bool field_is_unsigned(const MYSQL_FIELD& f) {
    return (f.flags & UNSIGNED_FLAG) != 0;
}

PreparedStmt::ptr PreparedStmt::Create(MYSQL* mysql, const std::string& sql) {
    if(!mysql) {
        return nullptr;
    }
    MYSQL_STMT* st = mysql_stmt_init(mysql);
    if(!st) {
        SYLAR_LOG_ERROR(g_logger) << "mysql_stmt_init error";
        return nullptr;
    }
    if(mysql_stmt_prepare(st, sql.c_str(), (unsigned long)sql.size())) {
        SYLAR_LOG_ERROR(g_logger) << "stmt=" << sql
            << " errno=" << mysql_stmt_errno(st)
            << " errstr=" << mysql_stmt_error(st);
        mysql_stmt_close(st);
        return nullptr;
    }
    PreparedStmt::ptr p(new PreparedStmt(mysql, sql));
    p->m_stmt = st;
    unsigned long count = mysql_stmt_param_count(st);
    p->m_binds.resize(count);
    memset(p->m_binds.data(), 0, sizeof(MYSQL_BIND) * count);
    return p;
}

PreparedStmt::PreparedStmt(MYSQL* /*mysql*/, const std::string& /*sql*/)
    : m_stmt(nullptr) {
}

PreparedStmt::~PreparedStmt() {
    if(m_stmt) {
        mysql_stmt_close(m_stmt);
    }
    for(size_t i = 0; i < m_binds.size(); ++i) {
        if(m_binds[i].buffer) {
            free(m_binds[i].buffer);
        }
    }
}

// 复制定长数据到绑定槽（注意：参数名避开 size，避免与 m_binds.size() 冲突）
#define BIND_NUM_COPY(idx, ptr, sz, btype, unsign) \
    do { \
        unsigned long i = (unsigned long)(idx) - 1; \
        if(i >= m_binds.size()) return -1; \
        if(m_binds[i].buffer == nullptr) { m_binds[i].buffer = malloc(sz); } \
        memcpy(m_binds[i].buffer, (ptr), (sz)); \
        m_binds[i].buffer_type = (btype); \
        m_binds[i].is_unsigned = (unsign); \
        m_binds[i].buffer_length = (unsigned long)(sz); \
    } while(0)

int PreparedStmt::bindInt8(int idx, int8_t v)   { BIND_NUM_COPY(idx, &v, sizeof(v), MYSQL_TYPE_TINY, 0); return 0; }
int PreparedStmt::bindUint8(int idx, uint8_t v) { BIND_NUM_COPY(idx, &v, sizeof(v), MYSQL_TYPE_TINY, 1); return 0; }
int PreparedStmt::bindInt16(int idx, int16_t v)   { BIND_NUM_COPY(idx, &v, sizeof(v), MYSQL_TYPE_SHORT, 0); return 0; }
int PreparedStmt::bindUint16(int idx, uint16_t v) { BIND_NUM_COPY(idx, &v, sizeof(v), MYSQL_TYPE_SHORT, 1); return 0; }
int PreparedStmt::bindInt32(int idx, int32_t v)   { BIND_NUM_COPY(idx, &v, sizeof(v), MYSQL_TYPE_LONG, 0); return 0; }
int PreparedStmt::bindUint32(int idx, uint32_t v) { BIND_NUM_COPY(idx, &v, sizeof(v), MYSQL_TYPE_LONG, 1); return 0; }
int PreparedStmt::bindInt64(int idx, int64_t v)   { BIND_NUM_COPY(idx, &v, sizeof(v), MYSQL_TYPE_LONGLONG, 0); return 0; }
int PreparedStmt::bindUint64(int idx, uint64_t v) { BIND_NUM_COPY(idx, &v, sizeof(v), MYSQL_TYPE_LONGLONG, 1); return 0; }
int PreparedStmt::bindFloat(int idx, float v)   { BIND_NUM_COPY(idx, &v, sizeof(v), MYSQL_TYPE_FLOAT, 0); return 0; }
int PreparedStmt::bindDouble(int idx, double v) { BIND_NUM_COPY(idx, &v, sizeof(v), MYSQL_TYPE_DOUBLE, 0); return 0; }
#undef BIND_NUM_COPY

#define BIND_VAR_COPY(idx, ptr, sz, btype) \
    do { \
        unsigned long i = (unsigned long)(idx) - 1; \
        if(i >= m_binds.size()) return -1; \
        if(m_binds[i].buffer == nullptr) { \
            m_binds[i].buffer = malloc(sz); \
        } else if(m_binds[i].buffer_length < (unsigned long)(sz)) { \
            free(m_binds[i].buffer); \
            m_binds[i].buffer = malloc(sz); \
        } \
        memcpy(m_binds[i].buffer, (ptr), (sz)); \
        m_binds[i].buffer_type = (btype); \
        m_binds[i].buffer_length = (unsigned long)(sz); \
    } while(0)

int PreparedStmt::bindString(int idx, const char* v) {
    if(!v) return bindNull(idx);
    BIND_VAR_COPY(idx, v, strlen(v), MYSQL_TYPE_STRING);
    return 0;
}
int PreparedStmt::bindString(int idx, const std::string& v) {
    BIND_VAR_COPY(idx, v.c_str(), v.size(), MYSQL_TYPE_STRING);
    return 0;
}
int PreparedStmt::bindBlob(int idx, const void* v, int64_t size) {
    BIND_VAR_COPY(idx, v, (size_t)size, MYSQL_TYPE_BLOB);
    return 0;
}
int PreparedStmt::bindBlob(int idx, const std::string& v) {
    BIND_VAR_COPY(idx, v.c_str(), v.size(), MYSQL_TYPE_BLOB);
    return 0;
}
#undef BIND_VAR_COPY

int PreparedStmt::bindTime(int idx, time_t v) {
    // 时间统一按字符串 'YYYY-MM-DD HH:MM:SS' 传，避免类型映射分歧
    return bindString(idx, sylar::Time2Str(v));
}

int PreparedStmt::bindNull(int idx) {
    unsigned long i = (unsigned long)idx - 1;
    if(i >= m_binds.size()) return -1;
    m_binds[i].buffer_type = MYSQL_TYPE_NULL;
    return 0;
}

int PreparedStmt::execute() {
    if(!m_binds.empty()) {
        if(mysql_stmt_bind_param(m_stmt, m_binds.data())) {
            return mysql_stmt_errno(m_stmt);
        }
    }
    return mysql_stmt_execute(m_stmt);
}

int64_t PreparedStmt::getLastInsertId() {
    return (int64_t)mysql_stmt_insert_id(m_stmt);
}

uint64_t PreparedStmt::getAffectedRows() {
    return (uint64_t)mysql_stmt_affected_rows(m_stmt);
}

int PreparedStmt::getErrno() {
    return mysql_stmt_errno(m_stmt);
}
std::string PreparedStmt::getErrStr() {
    const char* e = mysql_stmt_error(m_stmt);
    return e ? e : "";
}

// ---- 查询：输出绑定 + 物化 ----
namespace {
struct OutBind {
    OutBind() : is_null(0), error(0), length(0), type(MYSQL_TYPE_NULL), data(nullptr), data_len(0) {}
    ~OutBind() { if(data) delete[] data; }
    bool is_null;
    bool error;
    unsigned long length;
    enum_field_types type;
    char* data;
    size_t data_len;
    void alloc(size_t n) {
        if(data) delete[] data;
        data = new (std::nothrow) char[n]();
        data_len = n;
        length = (unsigned long)n;
    }
};
}

std::vector<std::map<std::string, Value> > PreparedStmt::queryRows() {
    std::vector<std::map<std::string, Value> > rows;
    if(!m_stmt) return rows;

    MYSQL_RES* meta = mysql_stmt_result_metadata(m_stmt);
    if(!meta) {
        // 非查询语句
        return rows;
    }
    int num = (int)mysql_num_fields(meta);
    MYSQL_FIELD* fields = mysql_fetch_fields(meta);

    std::vector<MYSQL_BIND> ob(num);
    std::vector<OutBind> store(num);
    std::vector<std::string> names(num);
    memset(ob.data(), 0, sizeof(MYSQL_BIND) * num);

    for(int i = 0; i < num; ++i) {
        names[i] = fields[i].name ? fields[i].name : "";
        store[i].type = fields[i].type;
        switch(fields[i].type) {
            case MYSQL_TYPE_TINY:       store[i].alloc(sizeof(int8_t)); break;
            case MYSQL_TYPE_SHORT:      store[i].alloc(sizeof(int16_t)); break;
            case MYSQL_TYPE_LONG:
            case MYSQL_TYPE_INT24:      store[i].alloc(sizeof(int32_t)); break;
            case MYSQL_TYPE_LONGLONG:   store[i].alloc(sizeof(int64_t)); break;
            case MYSQL_TYPE_FLOAT:      store[i].alloc(sizeof(float)); break;
            case MYSQL_TYPE_DOUBLE:     store[i].alloc(sizeof(double)); break;
            case MYSQL_TYPE_TIMESTAMP:
            case MYSQL_TYPE_DATETIME:
            case MYSQL_TYPE_DATE:
            case MYSQL_TYPE_TIME:       store[i].alloc(sizeof(MYSQL_TIME)); break;
            default:
                // 变长字段（字符串/BLOB），先给一个初始缓冲，length 会被回填
                store[i].alloc(fields[i].length > 0 ? fields[i].length : 128);
                break;
        }
        ob[i].buffer_type = store[i].type;
        ob[i].buffer = store[i].data;
        ob[i].buffer_length = store[i].data_len;
        ob[i].length = &store[i].length;
        ob[i].is_null = &store[i].is_null;
        ob[i].error = &store[i].error;
    }
    mysql_free_result(meta);

    if(mysql_stmt_bind_result(m_stmt, ob.data())) {
        return rows;
    }
    if(mysql_stmt_execute(m_stmt)) {
        return rows;
    }
    if(mysql_stmt_store_result(m_stmt)) {
        return rows;
    }

    while(true) {
        int rc = mysql_stmt_fetch(m_stmt);
        if(rc == 1) break;          // 错误
        if(rc == MYSQL_NO_DATA) break;

        // 处理截断（MYSQL_DATA_TRUNCATED）：对被截断的变长字段按真实长度重新拉取
        if(rc == MYSQL_DATA_TRUNCATED) {
            for(int i = 0; i < num; ++i) {
                if(store[i].error && store[i].length >= store[i].data_len) {
                    size_t need = (size_t)store[i].length + 1;
                    delete[] store[i].data;
                    store[i].data = new (std::nothrow) char[need]();
                    store[i].data_len = need;
                    ob[i].buffer = store[i].data;
                    ob[i].buffer_length = (unsigned long)store[i].data_len;
                    mysql_stmt_fetch_column(m_stmt, &ob[i], (unsigned int)i, 0);
                    store[i].error = 0;
                }
            }
        }

        std::map<std::string, Value> row;
        for(int i = 0; i < num; ++i) {
            if(store[i].is_null) {
                row[names[i]] = Value();
                continue;
            }
            switch(store[i].type) {
                case MYSQL_TYPE_TINY:
                    row[names[i]] = field_is_unsigned(fields[i])
                        ? Value(*(uint8_t*)store[i].data)
                        : Value(*(int8_t*)store[i].data);
                    break;
                case MYSQL_TYPE_SHORT:
                    row[names[i]] = field_is_unsigned(fields[i])
                        ? Value(*(uint16_t*)store[i].data)
                        : Value(*(int16_t*)store[i].data);
                    break;
                case MYSQL_TYPE_LONG:
                case MYSQL_TYPE_INT24:
                    row[names[i]] = field_is_unsigned(fields[i])
                        ? Value(*(uint32_t*)store[i].data)
                        : Value(*(int32_t*)store[i].data);
                    break;
                case MYSQL_TYPE_LONGLONG:
                    row[names[i]] = field_is_unsigned(fields[i])
                        ? Value(*(uint64_t*)store[i].data)
                        : Value(*(int64_t*)store[i].data);
                    break;
                case MYSQL_TYPE_FLOAT:
                    row[names[i]] = Value(*(float*)store[i].data);
                    break;
                case MYSQL_TYPE_DOUBLE:
                    row[names[i]] = Value(*(double*)store[i].data);
                    break;
                default:
                    row[names[i]] = Value(std::string(store[i].data, (size_t)store[i].length));
                    break;
            }
        }
        rows.push_back(row);
    }
    mysql_stmt_free_result(m_stmt);
    return rows;
}

}
