#ifndef __DDT_SESSION_MGR_H__
#define __DDT_SESSION_MGR_H__

#include "sylar/http/ws_servlet.h"
#include "ddt_player.h"
#include "sylar/thread.h"
#include <map>
#include <atomic>

namespace ddt {

class SessionManager {
public:
    static constexpr int SHARD_COUNT = 16;

    void addSession(sylar::http::WSSession::ptr session, Player::ptr player);
    void removeSession(sylar::http::WSSession* session);

    Player::ptr getPlayer(sylar::http::WSSession* session) const;
    Player::ptr getPlayerByAccountId(uint64_t accountId) const;
    sylar::http::WSSession* getSessionByAccountId(uint64_t accountId) const;
    uint32_t getNextPlayerId() { return m_nextPlayerId++; }
    
    void kickExistingSession(uint64_t accountId);
    void updateHeartbeat(sylar::http::WSSession* session);
    void cleanExpiredSessions(uint64_t timeoutMs);
    
    // 用于给全体在线玩家发送系统广播
    std::map<sylar::http::WSSession*, Player::ptr> getAllSessions() const;

private:
    void kickSession(sylar::http::WSSession* session);
    
    // 用会话指针地址取哈希分片，保证同一个连接的操作都在同一个无竞争的锁中
    uint32_t getSessionShard(sylar::http::WSSession* session) const {
        return std::hash<sylar::http::WSSession*>()(session) % SHARD_COUNT;
    }

    struct SessionShard {
        mutable sylar::RWMutex mutex;
        std::map<sylar::http::WSSession*, Player::ptr> sessionPlayers;
        std::map<sylar::http::WSSession*, uint64_t> lastRecvTime;
    };

    struct AccountShard {
        mutable sylar::RWMutex mutex;
        std::map<uint64_t, sylar::http::WSSession*> accountSessions;
    };

    SessionShard m_sessionShards[SHARD_COUNT];
    AccountShard m_accountShards[SHARD_COUNT];
    std::atomic<uint32_t> m_nextPlayerId{1};
};

} // namespace ddt

#endif