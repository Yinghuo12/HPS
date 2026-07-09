#ifndef __SYLAR_ORM_STMT_H__
#define __SYLAR_ORM_STMT_H__

#include <mysql/mysql.h>
#include <memory>
#include <string>
#include <vector>
#include <map>
#include "sylar/orm/value.h"

namespace sylar {

// 预处理语句（MySQL prepared statement）。
// 适合需要防 SQL 注入或高频重复执行的 DML，例如批量插入。
// 列下标 idx 从 1 开始，与 MySQL 习惯一致。
class PreparedStmt : public std::enable_shared_from_this<PreparedStmt> {
public:
    typedef std::shared_ptr<PreparedStmt> ptr;

    static PreparedStmt::ptr Create(MYSQL* mysql, const std::string& sql);
    ~PreparedStmt();

    int bindInt8(int idx, int8_t v);
    int bindUint8(int idx, uint8_t v);
    int bindInt16(int idx, int16_t v);
    int bindUint16(int idx, uint16_t v);
    int bindInt32(int idx, int32_t v);
    int bindUint32(int idx, uint32_t v);
    int bindInt64(int idx, int64_t v);
    int bindUint64(int idx, uint64_t v);
    int bindFloat(int idx, float v);
    int bindDouble(int idx, double v);
    int bindString(int idx, const char* v);
    int bindString(int idx, const std::string& v);
    int bindBlob(int idx, const void* v, int64_t size);
    int bindBlob(int idx, const std::string& v);
    int bindTime(int idx, time_t v);
    int bindNull(int idx);

    // 执行非查询语句，返回 mysql_stmt_execute 的返回值（0 成功）
    int execute();
    int64_t getLastInsertId();
    uint64_t getAffectedRows();

    // 执行查询语句，直接物化为 字段名->值 的行集合
    std::vector<std::map<std::string, Value> > queryRows();

    int getErrno();
    std::string getErrStr();

    MYSQL_STMT* getRaw() const { return m_stmt; }

private:
    PreparedStmt(MYSQL* mysql, const std::string& sql);
    PreparedStmt(const PreparedStmt&);
    PreparedStmt& operator=(const PreparedStmt&);

    MYSQL_STMT* m_stmt;
    std::vector<MYSQL_BIND> m_binds;   // 输入参数绑定
};

}

#endif
