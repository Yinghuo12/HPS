#ifndef __DDT_CHAT_MANAGER_H__
#define __DDT_CHAT_MANAGER_H__

#include <string>
#include <cstdint>
#include <functional>
#include <vector>
#include "sylar/http/ws_session.h"
#include "ddt.pb.h"

namespace ddt {

class Player;

class DDTChatManager {
public:
    static DDTChatManager& Instance();

    using BroadcastFn = std::function<void(sylar::http::WSSession::ptr,
                                           const ddt::GameMessage&)>;

    void setBroadcastFn(BroadcastFn fn);

    // Chat routing
    void handleChatRequest(uint64_t senderAccountId, const std::string& senderName,
                           uint32_t senderPlayerId,
                           const ddt::ChatRequest& req,
                           sylar::http::WSSession::ptr session,
                           const std::map<sylar::http::WSSession*, std::shared_ptr<Player>>& sessionPlayers);

    void handlePrivateChat(uint64_t senderAccountId, const std::string& senderName,
                           uint32_t senderPlayerId,
                           const ddt::PrivateChatRequest& req,
                           sylar::http::WSSession::ptr session,
                           const std::map<sylar::http::WSSession*, std::shared_ptr<Player>>& sessionPlayers);

    // History
    std::vector<ddt::ChatNotify> getHistory(ddt::ChannelType channel, int count);
    std::vector<ddt::ChatNotify> getPrivateHistory(uint64_t myId, uint64_t targetId, int count);

    // System broadcast
    void sendSystemMessage(const std::string& message,
                           const std::map<sylar::http::WSSession*, std::shared_ptr<Player>>& sessionPlayers);

    void sendToSession(sylar::http::WSSession::ptr session, const ddt::GameMessage& msg);

private:
    DDTChatManager() = default;

    BroadcastFn m_broadcastFn;
};

} // namespace ddt

#endif
