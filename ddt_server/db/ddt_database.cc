#include "ddt_database.h"
#include "sylar/iomanager.h"
#include "sylar/fiber.h"

#include <cstring>
#include <cstdlib>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <thread>
#include <queue>
#include <condition_variable>
#include <type_traits>

namespace ddt {

// =========================================================================
// 核心修复：内建的异步数据库工作线程池，将同步阻塞任务转为协程异步挂起
// =========================================================================
class DBThreadPool {
public:
    static DBThreadPool& Instance() { 
        static DBThreadPool pool; 
        return pool; 
    }
    
    void start(int num_threads) {
        if (m_running) return;
        m_running = true;
        for (int i = 0; i < num_threads; ++i) {
            m_threads.emplace_back([this]() {
                while (m_running) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(m_mutex);
                        m_cv.wait(lock, [this]() { return !m_tasks.empty() || !m_running; });
                        if (!m_running && m_tasks.empty()) return;
                        task = std::move(m_tasks.front());
                        m_tasks.pop();
                    }
                    if (task) {
                        try { task(); } catch (...) {}
                    }
                }
            });
        }
    }
    
    void stop() {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_running = false;
        }
        m_cv.notify_all();
        for (auto& t : m_threads) {
            if (t.joinable()) t.join();
        }
    }
    
    // 同步执行：将任务抛给DB线程池执行，挂起当前网络协程；DB执行完后自动唤醒协程
    template<typename F>
    auto executeSync(F&& f) -> typename std::enable_if<!std::is_same<decltype(f()), void>::value, decltype(f())>::type {
        using ReturnType = decltype(f());
        auto iom = sylar::IOManager::GetThis();
        auto fiber = sylar::Fiber::GetThis();
        
        // 容错：如果不在协程环境中(比如启动阶段)，直接在当前线程同步执行
        if (!iom || !fiber || !m_running) {
            return f();
        }
        
        std::shared_ptr<ReturnType> result = std::make_shared<ReturnType>();
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_tasks.emplace([f, result, iom, fiber]() {
                *result = f();
                // 任务完成，将原先挂起的协程重新丢回网络 IO 队列恢复执行
                iom->schedule(fiber); 
            });
        }
        m_cv.notify_one();
        sylar::Fiber::YieldToHold(); // 交出当前网络IO线程的CPU控制权
        return *result;
    }

    // 异步执行 (Fire and Forget)：纯写入操作，不需要等待结果，投递后协程立即继续执行
    template<typename F>
    void executeAsync(F&& f) {
        if (!m_running) { 
            f(); 
            return; 
        }
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_tasks.emplace([f]() { f(); });
        }
        m_cv.notify_one();
    }

private:
    DBThreadPool() = default;
    ~DBThreadPool() { stop(); }
    
    std::vector<std::thread> m_threads;
    std::queue<std::function<void()>> m_tasks;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_running = false;
};

// =========================================================================

DDTDatabase& DDTDatabase::Instance() {
    static DDTDatabase inst;
    return inst;
}

// --- Connection Pool ---

DDTDatabase::MySQLConn* DDTDatabase::acquireMySQL() {
    m_mysqlSem->wait();
    std::lock_guard<std::mutex> lk(m_poolMutex);
    for (auto* c : m_mysqlPool) {
        if (!c->in_use) {
            c->in_use = true;
            return c;
        }
    }
    return nullptr;
}

void DDTDatabase::releaseMySQL(MySQLConn* conn) {
    {
        std::lock_guard<std::mutex> lk(m_poolMutex);
        conn->in_use = false;
    }
    m_mysqlSem->notify();
}

// --- SHA-256 inline implementation ---

static const uint32_t SHA256_K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

static inline uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

std::string DDTDatabase::sha256(const std::string& input) {
    uint32_t h[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };

    std::vector<uint8_t> msg(input.begin(), input.end());
    uint64_t bitLen = msg.size() * 8;
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) msg.push_back(0);
    for (int i = 7; i >= 0; i--) msg.push_back((bitLen >> (i * 8)) & 0xFF);
    
    for (size_t offset = 0; offset < msg.size(); offset += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; i++) {
            w[i] = (msg[offset+i*4]<<24) | (msg[offset+i*4+1]<<16) |
                   (msg[offset+i*4+2]<<8) | msg[offset+i*4+3];
        }
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = rotr(w[i-15],7) ^ rotr(w[i-15],18) ^ (w[i-15]>>3);
            uint32_t s1 = rotr(w[i-2],17) ^ rotr(w[i-2],19) ^ (w[i-2]>>10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        uint32_t a=h[0], b=h[1], c=h[2], d=h[3], e=h[4], f=h[5], g=h[6], hh=h[7];
        for (int i = 0; i < 64; i++) {
            uint32_t S1 = rotr(e,6) ^ rotr(e,11) ^ rotr(e,25);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t temp1 = hh + S1 + ch + SHA256_K[i] + w[i];
            uint32_t S0 = rotr(a,2) ^ rotr(a,13) ^ rotr(a,22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t temp2 = S0 + maj;
            hh=g; g=f; f=e; e=d+temp1; d=c; c=b; b=a; a=temp1+temp2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
    }
    
    std::string result;
    result.reserve(32);
    for (int i = 0; i < 8; i++) {
        result.push_back((h[i]>>24)&0xFF);
        result.push_back((h[i]>>16)&0xFF);
        result.push_back((h[i]>>8)&0xFF);
        result.push_back(h[i]&0xFF);
    }
    return result;
}

static std::string toHex(const std::string& bin) {
    static const char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(bin.size() * 2);
    for (unsigned char c : bin) {
        out.push_back(hex[c >> 4]);
        out.push_back(hex[c & 0x0F]);
    }
    return out;
}

std::string DDTDatabase::generateSalt() {
    srand((unsigned)time(nullptr) ^ (unsigned)(uintptr_t)this);
    std::string salt;
    static const char cs[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++)
        salt.push_back(cs[rand() % 16]);
    return salt;
}

std::string DDTDatabase::hashPassword(const std::string& password, const std::string& salt) {
    return toHex(sha256(password + salt));
}

bool DDTDatabase::init(const std::string& mysql_host, int mysql_port,
                       const std::string& mysql_user, const std::string& mysql_pass,
                       const std::string& mysql_db,
                       const std::string& redis_host, int redis_port,
                       int poolSize) {
    
    // 启动负责将阻塞请求异步化的数据库线程池
    DBThreadPool::Instance().start(poolSize);

    for (int i = 0; i < poolSize; i++) {
        MySQLConn* conn = new MySQLConn();
        mysql_options(&conn->handle, MYSQL_SET_CHARSET_NAME, "utf8mb4");
        MYSQL* result = mysql_real_connect(&conn->handle, mysql_host.c_str(),
                                            mysql_user.c_str(), mysql_pass.c_str(),
                                            mysql_db.c_str(), mysql_port, nullptr, 0);
        if (!result) {
            fprintf(stderr, "MySQL pool connection %d failed: %s\n", i, mysql_error(&conn->handle));
            delete conn;
            if (i == 0) return false;
            break;
        }
        m_mysqlPool.push_back(conn);
    }
    m_mysqlSem.reset(new sylar::Semaphore(m_mysqlPool.size()));
    fprintf(stderr, "MySQL pool: %zu connections to %s:%d/%s\n",
            m_mysqlPool.size(), mysql_host.c_str(), mysql_port, mysql_db.c_str());

    m_redis = redisConnect(redis_host.c_str(), redis_port);
    if (!m_redis || m_redis->err) {
        fprintf(stderr, "Redis connect failed: %s\n", m_redis ? m_redis->errstr : "alloc error");
        if (m_redis) { redisFree(m_redis); m_redis = nullptr; }
        return false;
    }
    fprintf(stderr, "Redis connected: %s:%d\n", redis_host.c_str(), redis_port);
    return true;
}

// --- Account ---

std::pair<bool, uint64_t> DDTDatabase::registerAccount(const std::string& name,
                                                        const std::string& rawPassword) {
    return DBThreadPool::Instance().executeSync([this, name, rawPassword]() -> std::pair<bool, uint64_t> {
        MySQLGuard conn(this);

        std::string esc_name;
        esc_name.resize(name.size() * 2 + 1);
        esc_name.resize(mysql_real_escape_string(conn.get(), &esc_name[0], name.c_str(), name.size()));
        
        std::string query = "SELECT id FROM accounts WHERE name='" + esc_name + "'";
        if (mysql_query(conn.get(), query.c_str()) != 0) return {false, 0};
        MYSQL_RES* res = mysql_store_result(conn.get());
        if (mysql_num_rows(res) > 0) {
            mysql_free_result(res);
            return {false, 0};
        }
        mysql_free_result(res);
        
        std::string salt = generateSalt();
        std::string hash = hashPassword(rawPassword, salt);
        
        std::string esc_hash, esc_salt;
        esc_hash.resize(hash.size() * 2 + 1);
        esc_hash.resize(mysql_real_escape_string(conn.get(), &esc_hash[0], hash.c_str(), hash.size()));
        esc_salt.resize(salt.size() * 2 + 1);
        esc_salt.resize(mysql_real_escape_string(conn.get(), &esc_salt[0], salt.c_str(), salt.size()));
        
        query = "INSERT INTO accounts(name, password_hash, salt) VALUES('" +
                esc_name + "','" + esc_hash + "','" + esc_salt + "')";
        if (mysql_query(conn.get(), query.c_str()) != 0) return {false, 0};
        
        uint64_t id = mysql_insert_id(conn.get());
        
        query = "INSERT INTO player_profiles(account_id, nickname) VALUES(" +
                std::to_string(id) + ",'" + esc_name + "')";
        mysql_query(conn.get(), query.c_str());
        
        return {true, id};
    });
}

std::pair<bool, uint64_t> DDTDatabase::loginAccount(const std::string& name,
                                                     const std::string& rawPassword) {
    return DBThreadPool::Instance().executeSync([this, name, rawPassword]() -> std::pair<bool, uint64_t> {
        MySQLGuard conn(this);

        std::string esc_name;
        esc_name.resize(name.size() * 2 + 1);
        esc_name.resize(mysql_real_escape_string(conn.get(), &esc_name[0], name.c_str(), name.size()));
        
        std::string query = "SELECT id, password_hash, salt FROM accounts WHERE name='" + esc_name + "'";
        if (mysql_query(conn.get(), query.c_str()) != 0) return {false, 0};
        
        MYSQL_RES* res = mysql_store_result(conn.get());
        MYSQL_ROW row = mysql_fetch_row(res);
        if (!row) {
            mysql_free_result(res);
            return {false, 0};
        }
        
        uint64_t id = std::stoull(row[0]);
        std::string storedHash(row[1]);
        std::string salt(row[2]);
        mysql_free_result(res);
        
        std::string hash = hashPassword(rawPassword, salt);
        if (hash != storedHash) return {false, 0};
        
        return {true, id};
    });
}

bool DDTDatabase::getAccountName(uint64_t accountId, std::string& outName) {
    // 捕获引用的前提是闭包执行期间局部变量 outName 生命周期有效，executeSync通过挂起协程确保了这点
    return DBThreadPool::Instance().executeSync([this, accountId, &outName]() -> bool {
        MySQLGuard conn(this);
        std::string query = "SELECT name FROM accounts WHERE id=" + std::to_string(accountId);
        if (mysql_query(conn.get(), query.c_str()) != 0) return false;
        MYSQL_RES* res = mysql_store_result(conn.get());
        MYSQL_ROW row = mysql_fetch_row(res);
        if (!row) { mysql_free_result(res); return false; }
        outName = row[0];
        mysql_free_result(res);
        return true;
    });
}

// --- Token (Redis) ---

std::string DDTDatabase::generateToken(uint64_t accountId) {
    return DBThreadPool::Instance().executeSync([this, accountId]() -> std::string {
        srand((unsigned)time(nullptr) ^ (unsigned)accountId ^ (unsigned)(uintptr_t)this);
        std::string token;
        static const char hex[] = "0123456789abcdef";
        for (int i = 0; i < 32; i++)
            token.push_back(hex[rand() % 16]);

        std::lock_guard<std::mutex> lk(m_redisMutex);
        redisReply* reply = (redisReply*)redisCommand(m_redis,
            "SET session:%s %llu EX 86400", token.c_str(), (unsigned long long)accountId);
        if (reply) freeReplyObject(reply);
        return token;
    });
}

bool DDTDatabase::validateToken(const std::string& token, uint64_t& outAccountId) {
    return DBThreadPool::Instance().executeSync([this, token, &outAccountId]() -> bool {
        std::lock_guard<std::mutex> lk(m_redisMutex);
        redisReply* reply = (redisReply*)redisCommand(m_redis,
            "GET session:%s", token.c_str());
        if (!reply || reply->type != REDIS_REPLY_STRING) {
            if (reply) freeReplyObject(reply);
            return false;
        }
        outAccountId = std::stoull(reply->str);
        freeReplyObject(reply);
        return true;
    });
}

// --- Session ---

void DDTDatabase::setOnline(uint64_t accountId) {
    // 纯写入行为，不关注结果，异步派发，协程不挂起
    DBThreadPool::Instance().executeAsync([this, accountId]() {
        std::lock_guard<std::mutex> lk(m_redisMutex);
        redisReply* reply = (redisReply*)redisCommand(m_redis,
            "SET online:%llu 1 EX 300", (unsigned long long)accountId);
        if (reply) freeReplyObject(reply);
    });
}

void DDTDatabase::setOffline(uint64_t accountId) {
    DBThreadPool::Instance().executeAsync([this, accountId]() {
        std::lock_guard<std::mutex> lk(m_redisMutex);
        redisReply* reply = (redisReply*)redisCommand(m_redis,
            "DEL online:%llu", (unsigned long long)accountId);
        if (reply) freeReplyObject(reply);
    });
}

std::vector<uint64_t> DDTDatabase::getOnlinePlayers() {
    return DBThreadPool::Instance().executeSync([this]() -> std::vector<uint64_t> {
        std::lock_guard<std::mutex> lk(m_redisMutex);
        std::vector<uint64_t> result;
        redisReply* reply = (redisReply*)redisCommand(m_redis, "KEYS online:*");
        if (!reply || reply->type != REDIS_REPLY_ARRAY) {
            if (reply) freeReplyObject(reply);
            return result;
        }
        for (size_t i = 0; i < reply->elements; i++) {
            std::string key(reply->element[i]->str);
            auto pos = key.find(':');
            if (pos != std::string::npos) {
                result.push_back(std::stoull(key.substr(pos + 1)));
            }
        }
        freeReplyObject(reply);
        return result;
    });
}

// --- Chat ---

void DDTDatabase::saveChatMessage(int channel, uint64_t senderId,
                                   const std::string& senderName,
                                   const std::string& message,
                                   uint64_t targetId) {
    // 聊天记录入库异步落盘，无需阻塞游戏协程
    DBThreadPool::Instance().executeAsync([=]() {
        MySQLGuard conn(this);

        std::string esc_name, esc_msg;
        esc_name.resize(senderName.size() * 2 + 1);
        esc_name.resize(mysql_real_escape_string(conn.get(), &esc_name[0],
                                                  senderName.c_str(), senderName.size()));
        esc_msg.resize(message.size() * 2 + 1);
        esc_msg.resize(mysql_real_escape_string(conn.get(), &esc_msg[0],
                                                 message.c_str(), message.size()));
        
        std::string query = "INSERT INTO chat_history(channel, sender_id, sender_name, message, target_id) VALUES(" +
            std::to_string(channel) + "," + std::to_string(senderId) + ",'" +
            esc_name + "','" + esc_msg + "'," + std::to_string(targetId) + ")";
        mysql_query(conn.get(), query.c_str());
    });
}

std::vector<ChatRecord> DDTDatabase::getChatHistory(int channel, int count) {
    return DBThreadPool::Instance().executeSync([this, channel, count]() -> std::vector<ChatRecord> {
        MySQLGuard conn(this);
        std::vector<ChatRecord> result;

        std::string query = "SELECT channel, sender_id, sender_name, message, target_id, "
                             "UNIX_TIMESTAMP(created_at) FROM chat_history WHERE channel=" +
            std::to_string(channel) + " AND target_id=0 ORDER BY id DESC LIMIT " + std::to_string(count);
        
        if (mysql_query(conn.get(), query.c_str()) != 0) return result;
        MYSQL_RES* res = mysql_store_result(conn.get());
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
            ChatRecord r;
            r.channel = std::stoi(row[0]);
            r.sender_id = std::stoull(row[1]);
            r.sender_name = row[2] ? row[2] : "";
            r.message = row[3] ? row[3] : "";
            r.target_id = row[4] ? std::stoull(row[4]) : 0;
            r.timestamp = row[5] ? std::stoull(row[5]) : 0;
            result.push_back(r);
        }
        mysql_free_result(res);
        std::reverse(result.begin(), result.end());
        return result;
    });
}

std::vector<ChatRecord> DDTDatabase::getPrivateHistory(uint64_t id1, uint64_t id2, int count) {
    return DBThreadPool::Instance().executeSync([this, id1, id2, count]() -> std::vector<ChatRecord> {
        MySQLGuard conn(this);
        std::vector<ChatRecord> result;

        std::string query = "SELECT channel, sender_id, sender_name, message, target_id, "
                             "UNIX_TIMESTAMP(created_at) FROM chat_history "
                             "WHERE channel=6 AND ((sender_id=" + std::to_string(id1) +
                             " AND target_id=" + std::to_string(id2) + ") OR (sender_id=" +
                             std::to_string(id2) + " AND target_id=" + std::to_string(id1) +
                             ")) ORDER BY id DESC LIMIT " + std::to_string(count);
        
        if (mysql_query(conn.get(), query.c_str()) != 0) return result;
        MYSQL_RES* res = mysql_store_result(conn.get());
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
            ChatRecord r;
            r.channel = std::stoi(row[0]);
            r.sender_id = std::stoull(row[1]);
            r.sender_name = row[2] ? row[2] : "";
            r.message = row[3] ? row[3] : "";
            r.target_id = row[4] ? std::stoull(row[4]) : 0;
            r.timestamp = row[5] ? std::stoull(row[5]) : 0;
            result.push_back(r);
        }
        mysql_free_result(res);
        std::reverse(result.begin(), result.end());
        return result;
    });
}

// --- Friends ---

bool DDTDatabase::addFriend(uint64_t accountId, uint64_t friendId) {
    return DBThreadPool::Instance().executeSync([this, accountId, friendId]() -> bool {
        MySQLGuard conn(this);
        std::string query = "INSERT IGNORE INTO friends(account_id, friend_id) VALUES(" +
            std::to_string(accountId) + "," + std::to_string(friendId) + "),(" +
            std::to_string(friendId) + "," + std::to_string(accountId) + ")";
        return mysql_query(conn.get(), query.c_str()) == 0;
    });
}

bool DDTDatabase::isFriend(uint64_t accountId, uint64_t friendId) {
    return DBThreadPool::Instance().executeSync([this, accountId, friendId]() -> bool {
        MySQLGuard conn(this);
        std::string query = "SELECT 1 FROM friends WHERE account_id=" +
            std::to_string(accountId) + " AND friend_id=" + std::to_string(friendId);
        if (mysql_query(conn.get(), query.c_str()) != 0) return false;
        MYSQL_RES* res = mysql_store_result(conn.get());
        bool found = mysql_num_rows(res) > 0;
        mysql_free_result(res);
        return found;
    });
}

std::vector<FriendRecord> DDTDatabase::getFriendList(uint64_t accountId) {
    return DBThreadPool::Instance().executeSync([this, accountId]() -> std::vector<FriendRecord> {
        MySQLGuard conn(this);
        std::vector<FriendRecord> result;

        std::string query = "SELECT f.friend_id, a.name, COALESCE(p.level,1) "
                             "FROM friends f "
                             "JOIN accounts a ON f.friend_id = a.id "
                             "LEFT JOIN player_profiles p ON f.friend_id = p.account_id "
                             "WHERE f.account_id=" + std::to_string(accountId);
        
        if (mysql_query(conn.get(), query.c_str()) != 0) return result;
        MYSQL_RES* res = mysql_store_result(conn.get());
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
            FriendRecord r;
            r.account_id = std::stoull(row[0]);
            r.name = row[1] ? row[1] : "";
            r.level = row[2] ? std::stoi(row[2]) : 1;
            result.push_back(r);
        }
        mysql_free_result(res);
        return result;
    });
}

// --- Game Records ---

void DDTDatabase::saveGameRecord(uint64_t p1, uint64_t p2, uint64_t winner, int duration) {
    DBThreadPool::Instance().executeAsync([=]() {
        MySQLGuard conn(this);
        std::string query = "INSERT INTO game_records(player1_id, player2_id, winner_id, duration) VALUES(" +
            std::to_string(p1) + "," + std::to_string(p2) + "," +
            std::to_string(winner) + "," + std::to_string(duration) + ")";
        mysql_query(conn.get(), query.c_str());
    });
}

void DDTDatabase::updateWinLoss(uint64_t accountId, bool win) {
    DBThreadPool::Instance().executeAsync([=]() {
        MySQLGuard conn(this);
        std::string field = win ? "wins" : "losses";
        std::string query = "UPDATE player_profiles SET " + field + "=" + field + "+1 "
                             "WHERE account_id=" + std::to_string(accountId);
        mysql_query(conn.get(), query.c_str());
    });
}

} // namespace ddt