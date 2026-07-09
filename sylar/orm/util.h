#ifndef __SYLAR_ORM_UTIL_H__
#define __SYLAR_ORM_UTIL_H__

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <string>
#include <map>

// ORM 模块自带的轻量工具集。
// 上游 sylar 带有 StringUtil / TypeUtil / nop 等工具，但本项目精简过 util.h，
// 这些符号在本工程内并不存在；为避免改动其它 sylar 文件，ORM 模块在此自洽实现。
namespace sylar {

// printf 风格格式化（与上游 StringUtil::Formatv 行为一致）
std::string Format(const char* fmt, ...);
std::string Formatv(const char* fmt, va_list ap);

// 字符串到数值的安全转换（与上游 TypeUtil 行为一致）
struct TypeUtil {
    static int8_t   ToInt8(const char* v);
    static uint8_t  ToUint8(const char* v);
    static int16_t  ToInt16(const char* v);
    static uint16_t ToUint16(const char* v);
    static int32_t  ToInt32(const char* v);
    static uint32_t ToUint32(const char* v);
    static int64_t  ToInt64(const char* v);
    static uint64_t ToUint64(const char* v);
    static float    ToFloat(const char* v);
    static double   ToDouble(const char* v);

    static int64_t  Atoi(const char* v) { return ToInt64(v); }
    static double   Atof(const char* v) { return ToDouble(v); }
    static int64_t  Atoi(const std::string& v) { return ToInt64(v.c_str()); }
    static double   Atof(const std::string& v) { return ToDouble(v.c_str()); }
};

// 字符串解析为时间戳，format 形如 "%Y-%m-%d %H:%M:%S"
time_t Str2Time(const char* str, const char* format = "%Y-%m-%d %H:%M:%S");

// MySQL 字符串转义：把 ' \ " \0 \n \r \x1a 等特殊字符转义，用于拼装 SQL 字面量。
// 不依赖连接句柄，使用 MySQL 标准转义规则（单引号用 '' 表示）。
std::string MysqlEscape(const std::string& s);

// shared_ptr 空删除器，用于把裸指针包装成 shared_ptr 而不释放它。
template<class T>
void nop(T*) {}

// 从连接参数 map 中取值，带类型转换与默认值。
// 例：int port = GetParamValue(params, "port", 0);
template<class T>
T GetParamValue(const std::map<std::string, std::string>& m,
                const std::string& key, const T& def = T()) {
    auto it = m.find(key);
    if(it == m.end()) {
        return def;
    }
    return TypeUtil::ToInt64(it->second.c_str());
}

// bool 特化
template<>
bool GetParamValue<bool>(const std::map<std::string, std::string>& m,
                         const std::string& key, const bool& def);

// 字符串特化
template<>
std::string GetParamValue<std::string>(const std::map<std::string, std::string>& m,
                                       const std::string& key, const std::string& def);

}

#endif
