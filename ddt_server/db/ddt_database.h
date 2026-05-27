#ifndef __DDT_DATABASE_H__
#define __DDT_DATABASE_H__

#include <string>
#include <vector>
#include <cstdint>
#include <utility>
#include <mutex>
#include <memory>
#include <mysql/mysql.h>
#include <hiredis/hiredis.h>
#include "sylar/thread.h"

namespace ddt {

struct ChatRecord {
    int channel;
    uint64_t sender_id;
    std::string sender_name;
    std::string message;
    uint64_t target_id;
    uint64_t timestamp;
};

struct FriendRecord {
    uint64_t account_id;
    std::string name;
    int level;
};

class DDTDatabase {
public:
    static DDTDatabase& Instance();

    bool init(const std::string& mysql_host, int mysql_port,
              const std::string& mysql_user, const std::string& mysql_pass,
              const std::string& mysql_db,
              const std::string& redis_host, int redis_port,
              int poolSize = 4);

    // Account
    std::pair<bool, uint64_t> registerAccount(const std::string& name,
                                               const std::string& rawPassword);
    std::pair<bool, uint64_t> loginAccount(const std::string& name,
                                            const std::string& rawPassword);
    std::string generateToken(uint64_t accountId);
    bool validateToken(const std::string& token, uint64_t& outAccountId);
    bool getAccountName(uint64_t accountId, std::string& outName);

    // Session (Redis)
    void setOnline(uint64_t accountId);
    void setOffline(uint64_t accountId);
    std::vector<uint64_t> getOnlinePlayers();

    // Chat
    void saveChatMessage(int channel, uint64_t senderId,
                         const std::string& senderName,
                         const std::string& message,
                         uint64_t targetId = 0);
    std::vector<ChatRecord> getChatHistory(int channel, int count);
    std::vector<ChatRecord> getPrivateHistory(uint64_t id1, uint64_t id2, int count);

    // Friends
    bool addFriend(uint64_t accountId, uint64_t friendId);
    bool isFriend(uint64_t accountId, uint64_t friendId);
    std::vector<FriendRecord> getFriendList(uint64_t accountId);

    // Game records
    void saveGameRecord(uint64_t p1, uint64_t p2, uint64_t winner, int duration);

    // Profile
    void updateWinLoss(uint64_t accountId, bool win);

private:
    DDTDatabase() = default;

    struct MySQLConn {
        MYSQL handle;
        bool in_use;
        MySQLConn() : in_use(false) { mysql_init(&handle); }
    };

    MySQLConn* acquireMySQL();
    void releaseMySQL(MySQLConn* conn);

    struct MySQLGuard {
        DDTDatabase* db;
        MySQLConn* conn;
        MySQLGuard(DDTDatabase* d) : db(d), conn(d->acquireMySQL()) {}
        ~MySQLGuard() { if (conn) db->releaseMySQL(conn); }
        MYSQL* get() { return &conn->handle; }
    };

    std::string sha256(const std::string& input);
    std::string generateSalt();
    std::string hashPassword(const std::string& password, const std::string& salt);

    std::vector<MySQLConn*> m_mysqlPool;
    std::unique_ptr<sylar::Semaphore> m_mysqlSem;
    std::mutex m_poolMutex;

    std::mutex m_redisMutex;
    redisContext* m_redis = nullptr;
};

} // namespace ddt

#endif
