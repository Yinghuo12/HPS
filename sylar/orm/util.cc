// vasprintf / strptime 是 GNU 扩展；-std=c++11 下默认不暴露声明，需显式开启。
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "sylar/orm/util.h"
#include <stdio.h>
#include <stdarg.h>

namespace sylar {

std::string Format(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::string s = Formatv(fmt, ap);
    va_end(ap);
    return s;
}

std::string Formatv(const char* fmt, va_list ap) {
    char* buf = nullptr;
    va_list ap2;
    va_copy(ap2, ap);
    int len = vasprintf(&buf, fmt, ap2);
    va_end(ap2);
    std::string s;
    if(len > 0 && buf) {
        s.assign(buf, len);
    }
    if(buf) {
        free(buf);
    }
    return s;
}

int8_t TypeUtil::ToInt8(const char* v) {
    return (int8_t)ToInt64(v);
}
uint8_t TypeUtil::ToUint8(const char* v) {
    return (uint8_t)ToInt64(v);
}
int16_t TypeUtil::ToInt16(const char* v) {
    return (int16_t)ToInt64(v);
}
uint16_t TypeUtil::ToUint16(const char* v) {
    return (uint16_t)ToInt64(v);
}
int32_t TypeUtil::ToInt32(const char* v) {
    return (int32_t)ToInt64(v);
}
uint32_t TypeUtil::ToUint32(const char* v) {
    return (uint32_t)ToInt64(v);
}
int64_t TypeUtil::ToInt64(const char* v) {
    if(v == nullptr) {
        return 0;
    }
    return strtoll(v, nullptr, 10);
}
uint64_t TypeUtil::ToUint64(const char* v) {
    if(v == nullptr) {
        return 0;
    }
    return strtoull(v, nullptr, 10);
}
float TypeUtil::ToFloat(const char* v) {
    if(v == nullptr) {
        return 0;
    }
    return strtof(v, nullptr);
}
double TypeUtil::ToDouble(const char* v) {
    if(v == nullptr) {
        return 0;
    }
    return strtod(v, nullptr);
}

time_t Str2Time(const char* str, const char* format) {
    if(str == nullptr) {
        return 0;
    }
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    if(strptime(str, format, &tm) == nullptr) {
        // 退而求其次，尝试不带秒的日期格式
        if(strptime(str, "%Y-%m-%d", &tm) == nullptr) {
            return 0;
        }
    }
    return mktime(&tm);
}

std::string MysqlEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() * 2 + 1);
    for(size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        switch(c) {
            case '\\': out += "\\\\"; break;
            case '\'': out += "\\'"; break;
            case '\"': out += "\\\""; break;
            case '\0': out += "\\0";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\x1a': out += "\\Z"; break;
            default: out += c; break;
        }
    }
    return out;
}

template<>
bool GetParamValue<bool>(const std::map<std::string, std::string>& m,
                         const std::string& key, const bool& def) {
    auto it = m.find(key);
    if(it == m.end()) {
        return def;
    }
    const std::string& v = it->second;
    if(v == "1" || v == "true" || v == "True" || v == "TRUE" || v == "yes" || v == "on") {
        return true;
    }
    return false;
}

template<>
std::string GetParamValue<std::string>(const std::map<std::string, std::string>& m,
                                       const std::string& key, const std::string& def) {
    auto it = m.find(key);
    if(it == m.end()) {
        return def;
    }
    return it->second;
}

}
