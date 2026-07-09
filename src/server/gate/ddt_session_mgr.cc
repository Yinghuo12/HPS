#include "ddt_session_mgr.h"
#include "ddt_room_manager.h"
#include "ddt_auth.h"
#include "ddt_chat_manager.h"
#include "sylar/core/log.h"
#include "sylar/scheduler/iomanager.h"

namespace ddt {

static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

void SessionManager::addSession(sylar::http::WSSession::ptr session, Player::ptr player) {
    uint32_t sIdx = getSessionShard(session.get());
    {
        sylar::RWMutex::WriteLock lock(m_sessionShards[sIdx].mutex);
        m_sessionShards[sIdx].sessionPlayers[session.get()] = player;
        m_sessionShards[sIdx].lastRecvTime[session.get()] = sylar::GetCurrentMS();
    }
    
    uint64_t accId = player->getAccountId();
    if (accId > 0) {
        uint32_t aIdx = accId % SHARD_COUNT;
        sylar::RWMutex::WriteLock lock(m_accountShards[aIdx].mutex);
        m_accountShards[aIdx].accountSessions[accId] = session.get();
    }
}

void SessionManager::removeSession(sylar::http::WSSession* session) {
    uint32_t sIdx = getSessionShard(session);
    Player::ptr player;
    
    {
        sylar::RWMutex::WriteLock lock(m_sessionShards[sIdx].mutex);
        m_sessionShards[sIdx].lastRecvTime.erase(session);
        auto it = m_sessionShards[sIdx].sessionPlayers.find(session);
        if (it != m_sessionShards[sIdx].sessionPlayers.end()) {
            player = it->second;
            m_sessionShards[sIdx].sessionPlayers.erase(it);
        }
    }
    
    if (!player) return;

    uint32_t pid = player->getId();
    uint64_t accountId = player->getAccountId();
    std::string name = player->getName();

    RoomManager::Instance().leaveRoom(pid);
    
    if (accountId > 0) {
        uint32_t aIdx = accountId % SHARD_COUNT;
        {
            sylar::RWMutex::WriteLock lock(m_accountShards[aIdx].mutex);
            auto acctIt = m_accountShards[aIdx].accountSessions.find(accountId);
            if (acctIt != m_accountShards[aIdx].accountSessions.end() && acctIt->second == session) {
                m_accountShards[aIdx].accountSessions.erase(acctIt);
            }
        }
        DDTAuthManager::Instance().handleLogout(accountId);
        auto sessions = getAllSessions();
        DDTChatManager::Instance().sendSystemMessage(name + " left the game", sessions);
    }
    SYLAR_LOG_INFO(g_logger) << "Player " << pid << " disconnected";
}

Player::ptr SessionManager::getPlayer(sylar::http::WSSession* session) const {
    uint32_t sIdx = getSessionShard(session);
    sylar::RWMutex::ReadLock lock(m_sessionShards[sIdx].mutex);
    auto it = m_sessionShards[sIdx].sessionPlayers.find(session);
    return (it != m_sessionShards[sIdx].sessionPlayers.end()) ? it->second : nullptr;
}

Player::ptr SessionManager::getPlayerByAccountId(uint64_t accountId) const {
    sylar::http::WSSession* session = getSessionByAccountId(accountId);
    if (!session) return nullptr;
    return getPlayer(session);
}

sylar::http::WSSession* SessionManager::getSessionByAccountId(uint64_t accountId) const {
    uint32_t aIdx = accountId % SHARD_COUNT;
    sylar::RWMutex::ReadLock lock(m_accountShards[aIdx].mutex);
    auto it = m_accountShards[aIdx].accountSessions.find(accountId);
    return (it != m_accountShards[aIdx].accountSessions.end()) ? it->second : nullptr;
}

void SessionManager::kickExistingSession(uint64_t accountId) {
    uint32_t aIdx = accountId % SHARD_COUNT;
    sylar::http::WSSession* oldSession = nullptr;
    
    {
        sylar::RWMutex::WriteLock lock(m_accountShards[aIdx].mutex);
        auto it = m_accountShards[aIdx].accountSessions.find(accountId);
        if (it == m_accountShards[aIdx].accountSessions.end()) return;
        oldSession = it->second;
        m_accountShards[aIdx].accountSessions.erase(it);
    }
    
    if (oldSession) {
        uint32_t sIdx = getSessionShard(oldSession);
        sylar::RWMutex::WriteLock lock(m_sessionShards[sIdx].mutex);
        auto oldIt = m_sessionShards[sIdx].sessionPlayers.find(oldSession);
        if (oldIt != m_sessionShards[sIdx].sessionPlayers.end()) {
            SYLAR_LOG_WARN(g_logger) << "Kicking old session for account " << accountId;
            kickSession(oldSession);

            // [核心修复]: 顶号时，强制旧玩家实体退出所在房间！否则会造成永不消失的幽灵房间。
            uint32_t oldPid = oldIt->second->getId();
            RoomManager::Instance().leaveRoom(oldPid);

            m_sessionShards[sIdx].sessionPlayers.erase(oldIt);
        }
    }
}

void SessionManager::kickSession(sylar::http::WSSession* session) {
    if (!session) return;
    ddt::GameMessage msg;
    auto* err = msg.mutable_error_notify();
    err->set_code(409);
    err->set_msg("Account logged in from another device");
    std::string data;
    msg.SerializeToString(&data);
    session->sendMessage(data, sylar::http::WSFrameHead::BIN_FRAME);
}

void SessionManager::updateHeartbeat(sylar::http::WSSession* session) {
    uint32_t sIdx = getSessionShard(session);
    sylar::RWMutex::WriteLock lock(m_sessionShards[sIdx].mutex);
    m_sessionShards[sIdx].lastRecvTime[session] = sylar::GetCurrentMS();
}

void SessionManager::cleanExpiredSessions(uint64_t timeoutMs) {
    uint64_t now = sylar::GetCurrentMS();
    std::vector<sylar::http::WSSession::ptr> expired;

    for (int i = 0; i < SHARD_COUNT; ++i) {
        sylar::RWMutex::WriteLock lock(m_sessionShards[i].mutex);
        for (auto it = m_sessionShards[i].lastRecvTime.begin(); it != m_sessionShards[i].lastRecvTime.end(); ) {
            if (now - it->second > timeoutMs) {
                auto pit = m_sessionShards[i].sessionPlayers.find(it->first);
                if (pit != m_sessionShards[i].sessionPlayers.end() && pit->second) {
                    expired.push_back(pit->second->getSession());
                }
                it = m_sessionShards[i].lastRecvTime.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    for (auto& sess : expired) {
        if (sess) {
            SYLAR_LOG_WARN(g_logger) << "Heartbeat timeout, closing session " << sess.get();
            sess->close();
        }
    }
}

std::map<sylar::http::WSSession*, Player::ptr> SessionManager::getAllSessions() const {
    std::map<sylar::http::WSSession*, Player::ptr> result;
    for (int i = 0; i < SHARD_COUNT; ++i) {
        sylar::RWMutex::ReadLock lock(m_sessionShards[i].mutex);
        result.insert(m_sessionShards[i].sessionPlayers.begin(), m_sessionShards[i].sessionPlayers.end());
    }
    return result;
}

} // namespace ddt