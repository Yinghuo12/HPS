#include "ddt_chat_manager.h"
#include "ddt_database.h"
#include "ddt_player.h"
#include "ddt_room_manager.h"

#include <ctime>
#include <set>

namespace ddt {

DDTChatManager& DDTChatManager::Instance() {
    static DDTChatManager inst;
    return inst;
}

void DDTChatManager::setBroadcastFn(BroadcastFn fn) {
    m_broadcastFn = fn;
}

static void sendToRawSession(sylar::http::WSSession* session,
                              const ddt::GameMessage& msg) {
    if (!session) return;
    std::string data;
    msg.SerializeToString(&data);
    session->sendMessage(data, sylar::http::WSFrameHead::BIN_FRAME);
}

void DDTChatManager::sendToSession(sylar::http::WSSession::ptr session,
                                    const ddt::GameMessage& msg) {
    sendToRawSession(session.get(), msg);
}

void DDTChatManager::handleChatRequest(
        uint64_t senderAccountId, const std::string& senderName,
        uint32_t senderPlayerId,
        const ddt::ChatRequest& req,
        sylar::http::WSSession::ptr session,
        const std::map<sylar::http::WSSession*, std::shared_ptr<Player>>& sessionPlayers) {

    ddt::ChatNotify notify;
    notify.set_channel(req.channel());
    notify.set_sender_id(senderPlayerId);
    notify.set_sender_name(senderName);
    notify.set_message(req.message());
    notify.set_timestamp((uint64_t)time(nullptr));

    ddt::GameMessage wrap;
    *wrap.mutable_chat_notify() = notify;

    switch (req.channel()) {
        case ddt::CHANNEL_WORLD:
            for (auto& kv : sessionPlayers) {
                sendToRawSession(kv.first, wrap);
            }
            DDTDatabase::Instance().saveChatMessage(
                req.channel(), senderAccountId, senderName, req.message());
            break;

        case ddt::CHANNEL_ROOM:
        case ddt::CHANNEL_ALL: {
            auto senderIt = sessionPlayers.find(session.get());
            if (senderIt == sessionPlayers.end()) break;

            auto room = RoomManager::Instance().findRoomByPlayer(senderPlayerId);
            if (room) {
                for (auto& kv : sessionPlayers) {
                    if (kv.second && room->hasPlayer(kv.second->getId())) {
                        sendToRawSession(kv.first, wrap);
                    }
                }
            } else {
                sendToSession(session, wrap);
            }
            if (req.channel() == ddt::CHANNEL_ROOM) {
                DDTDatabase::Instance().saveChatMessage(
                    req.channel(), senderAccountId, senderName, req.message());
            }
            break;
        }

        case ddt::CHANNEL_TEAM: {
            auto room = RoomManager::Instance().findRoomByPlayer(senderPlayerId);
            if (room) {
                ddt::TeamSide senderTeam = room->getPlayerTeam(senderPlayerId);
                for (auto& kv : sessionPlayers) {
                    if (kv.second && room->hasPlayer(kv.second->getId()) &&
                        room->getPlayerTeam(kv.second->getId()) == senderTeam) {
                        sendToRawSession(kv.first, wrap);
                    }
                }
            } else {
                sendToSession(session, wrap);
            }
            break;
        }

        case ddt::CHANNEL_SYSTEM:
        case ddt::CHANNEL_BROADCAST:
            break;

        default:
            break;
    }
}

void DDTChatManager::handlePrivateChat(
        uint64_t senderAccountId, const std::string& senderName,
        uint32_t senderPlayerId,
        const ddt::PrivateChatRequest& req,
        sylar::http::WSSession::ptr session,
        const std::map<sylar::http::WSSession*, std::shared_ptr<Player>>& sessionPlayers) {

    ddt::ChatNotify notify;
    notify.set_channel(ddt::CHANNEL_PRIVATE);
    notify.set_sender_id(senderPlayerId);
    notify.set_sender_name(senderName);
    notify.set_message(req.message());
    notify.set_timestamp((uint64_t)time(nullptr));

    ddt::GameMessage wrap;
    *wrap.mutable_chat_notify() = notify;

    for (auto& kv : sessionPlayers) {
        if (kv.second && kv.second->getId() == req.target_id()) {
            sendToRawSession(kv.first, wrap);
            break;
        }
    }

    sendToSession(session, wrap);

    DDTDatabase::Instance().saveChatMessage(
        ddt::CHANNEL_PRIVATE, senderAccountId, senderName,
        req.message(), req.target_id());
}

std::vector<ddt::ChatNotify> DDTChatManager::getHistory(ddt::ChannelType channel, int count) {
    auto records = DDTDatabase::Instance().getChatHistory(channel, count);
    std::vector<ddt::ChatNotify> result;
    for (auto& r : records) {
        ddt::ChatNotify notify;
        notify.set_channel((ddt::ChannelType)r.channel);
        notify.set_sender_id(r.sender_id);
        notify.set_sender_name(r.sender_name);
        notify.set_message(r.message);
        notify.set_timestamp(r.timestamp);
        result.push_back(notify);
    }
    return result;
}

std::vector<ddt::ChatNotify> DDTChatManager::getPrivateHistory(
        uint64_t myId, uint64_t targetId, int count) {
    auto records = DDTDatabase::Instance().getPrivateHistory(myId, targetId, count);
    std::vector<ddt::ChatNotify> result;
    for (auto& r : records) {
        ddt::ChatNotify notify;
        notify.set_channel(ddt::CHANNEL_PRIVATE);
        notify.set_sender_id(r.sender_id);
        notify.set_sender_name(r.sender_name);
        notify.set_message(r.message);
        notify.set_timestamp(r.timestamp);
        result.push_back(notify);
    }
    return result;
}

void DDTChatManager::sendSystemMessage(
        const std::string& message,
        const std::map<sylar::http::WSSession*, std::shared_ptr<Player>>& sessionPlayers) {
    ddt::ChatNotify notify;
    notify.set_channel(ddt::CHANNEL_SYSTEM);
    notify.set_sender_id(0);
    notify.set_sender_name("System");
    notify.set_message(message);
    notify.set_timestamp((uint64_t)time(nullptr));

    ddt::GameMessage wrap;
    *wrap.mutable_chat_notify() = notify;

    for (auto& kv : sessionPlayers) {
        sendToRawSession(kv.first, wrap);
    }

    DDTDatabase::Instance().saveChatMessage(
        ddt::CHANNEL_SYSTEM, 0, "System", message);
}

} // namespace ddt
