#ifndef __SYLAR_ORM_TRANSACTION_H__
#define __SYLAR_ORM_TRANSACTION_H__

#include <memory>
#include <string>
#include <stdint.h>

namespace sylar {

class Connection;

// 事务。绑定到单条 Connection，事务期间该连接被独占。
//
// 支持嵌套：在同一个 Transaction 对象上多次调用 begin()/commit()/rollback()
// 时，外层用真正的 BEGIN/COMMIT/ROLLBACK，内层用 SAVEPOINT/RELEASE/ROLLBACK TO，
// 从而在 MySQL 同一会话不支持嵌套 BEGIN 的限制下，仍给出正确的嵌套事务语义。
//
// 注意：事务不能跨连接（会话）。
class Transaction {
public:
    typedef std::shared_ptr<Transaction> ptr;

    explicit Transaction(Connection* conn);
    ~Transaction();

    bool begin();
    bool commit();
    bool rollback();

    int execute(const std::string& sql);
    int execute(const char* fmt, ...);

    int64_t getLastInsertId();

    bool isFinished() const { return m_finished; }
    bool hasError() const { return m_hasError; }
    int depth() const { return m_depth; }

private:
    Connection* m_conn;
    int m_depth;
    bool m_finished;
    bool m_hasError;
};

}

#endif
