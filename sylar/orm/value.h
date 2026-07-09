#ifndef __SYLAR_ORM_VALUE_H__
#define __SYLAR_ORM_VALUE_H__

#include <stdint.h>
#include <string>
#include <cstring>

namespace sylar {

// ORM 的通用值类型：可表示 NULL / 布尔 / 整数 / 无符号整数 / 浮点 / 字符串 / 二进制。
// 提供丰富的隐式转换，使 `int id = user("id");` 与 `string s = user("name");` 这类
// 用法自然成立（与 orm.md 中 Value 的用法一致）。
class Value {
public:
    enum Type {
        TYPE_NULL = 0,
        TYPE_BOOL,
        TYPE_INT,
        TYPE_UINT,
        TYPE_FLOAT,
        TYPE_STRING,
        TYPE_BLOB
    };

    Value();
    Value(bool v);
    Value(int8_t v);
    Value(uint8_t v);
    Value(int16_t v);
    Value(uint16_t v);
    Value(int32_t v);
    Value(uint32_t v);
    Value(int64_t v);
    Value(uint64_t v);
    Value(float v);
    Value(double v);
    Value(const char* v);
    Value(const std::string& v);
    // 二进制数据标签构造，避免与普通 string 混淆
    Value(const std::string& v, bool is_blob);

    Type type() const { return m_type; }
    bool isNull() const { return m_type == TYPE_NULL; }
    bool isBlob() const { return m_type == TYPE_BLOB; }
    bool isString() const { return m_type == TYPE_STRING || m_type == TYPE_BLOB; }

    // 隐式转换到目标类型：null 视为 0 / 空
    operator bool() const;
    operator int8_t() const;
    operator uint8_t() const;
    operator int16_t() const;
    operator uint16_t() const;
    operator int32_t() const;
    operator uint32_t() const;
    operator int64_t() const;
    operator uint64_t() const;
    operator float() const;
    operator double() const;
    operator std::string() const;

    // 文本形式（debug 用），null 返回 "NULL"
    std::string str() const;

    // 生成 SQL 字面量：null→NULL，字符串→'转义后'，数字→原文
    std::string toSql() const;

private:
    Type m_type;
    int64_t m_int;
    uint64_t m_uint;
    double m_double;
    std::string m_str;
};

}

#endif
