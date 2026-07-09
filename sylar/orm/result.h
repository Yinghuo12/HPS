#ifndef __SYLAR_ORM_RESULT_H__
#define __SYLAR_ORM_RESULT_H__

#include <mysql/mysql.h>
#include <memory>
#include <string>
#include <map>
#include <vector>
#include "sylar/orm/value.h"

namespace sylar {

class Connection;

// 一次 SELECT 的结果集游标。
// 内部持有 MYSQL_RES，析构时自动释放。支持按列下标或列名取值，
// 也可一次性物化为 vector<map<string,Value>> 供 ORM 上层使用。
class Result {
public:
    typedef std::shared_ptr<Result> ptr;

    Result(MYSQL_RES* res, int eno, const std::string& errstr);
    ~Result();

    bool valid() const { return m_data.get() != nullptr; }
    int getErrno() const { return m_errno; }
    const std::string& getErrStr() const { return m_errstr; }

    // 行数 / 列数
    uint64_t getDataCount() const;
    int getColumnCount() const;

    // 列信息
    std::string getColumnName(int idx) const;
    int getColumnIndex(const std::string& name) const;
    int getColumnType(int idx) const;

    // 游标推进；调用 next() 之后再读取当前行
    bool next();

    // 当前行按列下标取值
    bool isNull(int idx) const;
    int64_t getInt(int idx) const;
    uint64_t getUint(int idx) const;
    double getDouble(int idx) const;
    std::string getString(int idx) const;
    std::string getBlob(int idx) const;
    time_t getTime(int idx) const;

    // 当前行按列名取值（找不到列时按 NULL/0 处理）
    bool isNull(const std::string& name) const;
    int64_t getInt(const std::string& name) const;
    uint64_t getUint(const std::string& name) const;
    double getDouble(const std::string& name) const;
    std::string getString(const std::string& name) const;
    std::string getBlob(const std::string& name) const;
    time_t getTime(const std::string& name) const;

    Value getValue(int idx) const;
    Value getValue(const std::string& name) const;

    // 把当前行抓取为 字段名->值 的映射
    void currentToMap(std::map<std::string, Value>& out) const;

    // 物化整张结果集（重置游标从头扫描）
    std::vector<std::map<std::string, Value> > fetchAll();

private:
    int m_errno;
    std::string m_errstr;
    MYSQL_ROW m_cur;
    unsigned long* m_curLength;
    std::shared_ptr<MYSQL_RES> m_data;
    std::map<std::string, int> m_colMap;     // 列名(小写) -> 下标
    std::vector<std::string> m_colNames;
    int m_fieldCount;
};

}

#endif
