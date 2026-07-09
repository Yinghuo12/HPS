#include "sylar/orm/value.h"
#include "sylar/orm/util.h"
#include <stdio.h>

namespace sylar {

Value::Value()
    : m_type(TYPE_NULL)
    , m_int(0)
    , m_uint(0)
    , m_double(0) {
}

Value::Value(bool v)
    : m_type(TYPE_BOOL)
    , m_int(v ? 1 : 0)
    , m_uint(0)
    , m_double(0) {
}
Value::Value(int8_t v)  : m_type(TYPE_INT), m_int(v), m_uint(0), m_double(0) {}
Value::Value(int16_t v) : m_type(TYPE_INT), m_int(v), m_uint(0), m_double(0) {}
Value::Value(int32_t v) : m_type(TYPE_INT), m_int(v), m_uint(0), m_double(0) {}
Value::Value(int64_t v) : m_type(TYPE_INT), m_int(v), m_uint(0), m_double(0) {}
Value::Value(uint8_t v)  : m_type(TYPE_UINT), m_int(0), m_uint(v), m_double(0) {}
Value::Value(uint16_t v) : m_type(TYPE_UINT), m_int(0), m_uint(v), m_double(0) {}
Value::Value(uint32_t v) : m_type(TYPE_UINT), m_int(0), m_uint(v), m_double(0) {}
Value::Value(uint64_t v) : m_type(TYPE_UINT), m_int(0), m_uint(v), m_double(0) {}
Value::Value(float v)  : m_type(TYPE_FLOAT), m_int(0), m_uint(0), m_double(v) {}
Value::Value(double v) : m_type(TYPE_FLOAT), m_int(0), m_uint(0), m_double(v) {}

Value::Value(const char* v)
    : m_type(v ? TYPE_STRING : TYPE_NULL)
    , m_int(0)
    , m_uint(0)
    , m_double(0)
    , m_str(v ? v : "") {
}

Value::Value(const std::string& v)
    : m_type(TYPE_STRING)
    , m_int(0)
    , m_uint(0)
    , m_double(0)
    , m_str(v) {
}

Value::Value(const std::string& v, bool is_blob)
    : m_type(is_blob ? TYPE_BLOB : TYPE_STRING)
    , m_int(0)
    , m_uint(0)
    , m_double(0)
    , m_str(v) {
}

Value::operator bool() const {
    if(m_type == TYPE_BOOL || m_type == TYPE_INT) return m_int != 0;
    if(m_type == TYPE_UINT) return m_uint != 0;
    if(m_type == TYPE_FLOAT) return m_double != 0;
    if(m_type == TYPE_STRING || m_type == TYPE_BLOB) return !m_str.empty();
    return false;
}
Value::operator int8_t() const  { return (int8_t)(int64_t)(*this); }
Value::operator uint8_t() const { return (uint8_t)(int64_t)(*this); }
Value::operator int16_t() const  { return (int16_t)(int64_t)(*this); }
Value::operator uint16_t() const { return (uint16_t)(int64_t)(*this); }
Value::operator int32_t() const  { return (int32_t)(int64_t)(*this); }
Value::operator uint32_t() const { return (uint32_t)(int64_t)(*this); }
Value::operator int64_t() const {
    switch(m_type) {
        case TYPE_BOOL:
        case TYPE_INT:   return m_int;
        case TYPE_UINT:  return (int64_t)m_uint;
        case TYPE_FLOAT: return (int64_t)m_double;
        case TYPE_STRING:
        case TYPE_BLOB:  return TypeUtil::ToInt64(m_str.c_str());
        default:         return 0;
    }
}
Value::operator uint64_t() const {
    switch(m_type) {
        case TYPE_BOOL:
        case TYPE_INT:   return (uint64_t)m_int;
        case TYPE_UINT:  return m_uint;
        case TYPE_FLOAT: return (uint64_t)m_double;
        case TYPE_STRING:
        case TYPE_BLOB:  return TypeUtil::ToUint64(m_str.c_str());
        default:         return 0;
    }
}
Value::operator float() const  { return (float)(double)(*this); }
Value::operator double() const {
    switch(m_type) {
        case TYPE_BOOL:
        case TYPE_INT:   return (double)m_int;
        case TYPE_UINT:  return (double)m_uint;
        case TYPE_FLOAT: return m_double;
        case TYPE_STRING:
        case TYPE_BLOB:  return TypeUtil::ToDouble(m_str.c_str());
        default:         return 0;
    }
}
Value::operator std::string() const {
    switch(m_type) {
        case TYPE_NULL:  return "";
        case TYPE_BOOL:  return m_int ? "1" : "0";
        case TYPE_INT:   { char b[32]; snprintf(b, sizeof(b), "%lld", (long long)m_int); return b; }
        case TYPE_UINT:  { char b[32]; snprintf(b, sizeof(b), "%llu", (unsigned long long)m_uint); return b; }
        case TYPE_FLOAT: { char b[64]; snprintf(b, sizeof(b), "%g", m_double); return b; }
        case TYPE_STRING:
        case TYPE_BLOB:  return m_str;
        default:         return "";
    }
}

std::string Value::str() const {
    if(m_type == TYPE_NULL) return "NULL";
    return (std::string)(*this);
}

std::string Value::toSql() const {
    switch(m_type) {
        case TYPE_NULL:  return "NULL";
        case TYPE_BOOL:  return m_int ? "1" : "0";
        case TYPE_INT:   { char b[32]; snprintf(b, sizeof(b), "%lld", (long long)m_int); return b; }
        case TYPE_UINT:  { char b[32]; snprintf(b, sizeof(b), "%llu", (unsigned long long)m_uint); return b; }
        case TYPE_FLOAT: { char b[64]; snprintf(b, sizeof(b), "%g", m_double); return b; }
        case TYPE_STRING:
        case TYPE_BLOB:  return "'" + MysqlEscape(m_str) + "'";
        default:         return "NULL";
    }
}

}
