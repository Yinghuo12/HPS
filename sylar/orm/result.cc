#include "sylar/orm/result.h"
#include "sylar/orm/util.h"
#include <algorithm>
#include <string.h>

namespace sylar {

static std::string toLower(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(), ::tolower);
    return r;
}

Result::Result(MYSQL_RES* res, int eno, const std::string& errstr)
    : m_errno(eno)
    , m_errstr(errstr)
    , m_cur(nullptr)
    , m_curLength(nullptr)
    , m_fieldCount(0) {
    if(res) {
        m_data.reset(res, mysql_free_result);
        m_fieldCount = (int)mysql_num_fields(res);
        MYSQL_FIELD* fields = mysql_fetch_fields(res);
        if(fields) {
            for(int i = 0; i < m_fieldCount; ++i) {
                std::string name = fields[i].name ? fields[i].name : "";
                m_colNames.push_back(name);
                m_colMap[toLower(name)] = i;
            }
        }
    }
}

Result::~Result() {
}

uint64_t Result::getDataCount() const {
    if(!m_data) return 0;
    return mysql_num_rows(m_data.get());
}

int Result::getColumnCount() const {
    return m_fieldCount;
}

std::string Result::getColumnName(int idx) const {
    if(idx < 0 || idx >= (int)m_colNames.size()) return "";
    return m_colNames[idx];
}

int Result::getColumnIndex(const std::string& name) const {
    auto it = m_colMap.find(toLower(name));
    if(it == m_colMap.end()) return -1;
    return it->second;
}

int Result::getColumnType(int idx) const {
    if(!m_data || idx < 0 || idx >= m_fieldCount) return MYSQL_TYPE_NULL;
    MYSQL_FIELD* fields = mysql_fetch_fields(m_data.get());
    if(!fields) return MYSQL_TYPE_NULL;
    return (int)fields[idx].type;
}

bool Result::next() {
    if(!m_data) return false;
    m_cur = mysql_fetch_row(m_data.get());
    m_curLength = mysql_fetch_lengths(m_data.get());
    return m_cur != nullptr;
}

bool Result::isNull(int idx) const {
    if(!m_cur || idx < 0 || idx >= m_fieldCount) return true;
    return m_cur[idx] == nullptr;
}
int64_t Result::getInt(int idx) const {
    if(!m_cur || idx < 0 || idx >= m_fieldCount || !m_cur[idx]) return 0;
    return TypeUtil::ToInt64(m_cur[idx]);
}
uint64_t Result::getUint(int idx) const {
    if(!m_cur || idx < 0 || idx >= m_fieldCount || !m_cur[idx]) return 0;
    return TypeUtil::ToUint64(m_cur[idx]);
}
double Result::getDouble(int idx) const {
    if(!m_cur || idx < 0 || idx >= m_fieldCount || !m_cur[idx]) return 0;
    return TypeUtil::ToDouble(m_cur[idx]);
}
std::string Result::getString(int idx) const {
    if(!m_cur || idx < 0 || idx >= m_fieldCount || !m_cur[idx]) return "";
    return std::string(m_cur[idx], m_curLength ? m_curLength[idx] : strlen(m_cur[idx]));
}
std::string Result::getBlob(int idx) const {
    return getString(idx);
}
time_t Result::getTime(int idx) const {
    if(!m_cur || idx < 0 || idx >= m_fieldCount || !m_cur[idx]) return 0;
    return Str2Time(m_cur[idx]);
}

bool Result::isNull(const std::string& name) const {
    return isNull(getColumnIndex(name));
}
int64_t Result::getInt(const std::string& name) const {
    return getInt(getColumnIndex(name));
}
uint64_t Result::getUint(const std::string& name) const {
    return getUint(getColumnIndex(name));
}
double Result::getDouble(const std::string& name) const {
    return getDouble(getColumnIndex(name));
}
std::string Result::getString(const std::string& name) const {
    return getString(getColumnIndex(name));
}
std::string Result::getBlob(const std::string& name) const {
    return getBlob(getColumnIndex(name));
}
time_t Result::getTime(const std::string& name) const {
    return getTime(getColumnIndex(name));
}

Value Result::getValue(int idx) const {
    if(!m_cur || idx < 0 || idx >= m_fieldCount || !m_cur[idx]) {
        return Value();
    }
    int t = getColumnType(idx);
    const char* raw = m_cur[idx];
    switch(t) {
        case MYSQL_TYPE_TINY:
        case MYSQL_TYPE_SHORT:
        case MYSQL_TYPE_LONG:
        case MYSQL_TYPE_INT24:
        case MYSQL_TYPE_LONGLONG:
            return Value(TypeUtil::ToInt64(raw));
        case MYSQL_TYPE_FLOAT:
        case MYSQL_TYPE_DOUBLE:
        case MYSQL_TYPE_DECIMAL:
        case MYSQL_TYPE_NEWDECIMAL:
            return Value(TypeUtil::ToDouble(raw));
        default:
            // 含 TIMESTAMP/DATETIME/DATE/TIME/STRING/BLOB 等都按字符串保存
            return Value(std::string(raw, m_curLength ? m_curLength[idx] : strlen(raw)));
    }
}

Value Result::getValue(const std::string& name) const {
    return getValue(getColumnIndex(name));
}

void Result::currentToMap(std::map<std::string, Value>& out) const {
    out.clear();
    if(!m_cur) return;
    for(int i = 0; i < m_fieldCount; ++i) {
        out[m_colNames[i]] = getValue(i);
    }
}

std::vector<std::map<std::string, Value> > Result::fetchAll() {
    std::vector<std::map<std::string, Value> > rows;
    if(!m_data) return rows;
    // 游标回到起点
    mysql_data_seek(m_data.get(), 0);
    while(next()) {
        std::map<std::string, Value> row;
        currentToMap(row);
        rows.push_back(row);
    }
    return rows;
}

}
