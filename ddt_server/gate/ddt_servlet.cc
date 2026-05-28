#include "ddt_servlet.h"
#include "ddt_room_manager.h"
#include "ddt_auth.h"
#include "ddt_chat_manager.h"
#include "ddt_database.h"
#include "ddt.pb.h"
#include "sylar/log.h"

namespace ddt {

static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

DDTWSServlet::DDTWSServlet()
    : sylar::http::WSServlet("ddt_servlet") {}

void DDTWSServlet::broadcastShutdown() {
    ddt::GameMessage msg;
    auto* notify = msg.mutable_server_shutdown_notify();
    notify->set_reason("Server is shutting down");

    std::string data;
    msg.SerializeToString(&data);

    sylar::RWMutex::ReadLock lock(m_mutex);
    for (auto& kv : m_sessionPlayers) {
        if (kv.second) {
            kv.second->sendMessage(data);
        }
    }
    SYLAR_LOG_INFO(g_logger) << "Broadcast shutdown to " << m_sessionPlayers.size() << " clients";
}

void DDTWSServlet::kickSession(sylar::http::WSSession* session) {
    if (!session) return;
    // Send a close frame to the client
    ddt::GameMessage msg;
    auto* err = msg.mutable_error_notify();
    err->set_code(409);
    err->set_msg("Account logged in from another device");
    std::string data;
    msg.SerializeToString(&data);
    session->sendMessage(data, sylar::http::WSFrameHead::BIN_FRAME);
}

int32_t DDTWSServlet::onConnect(sylar::http::HttpRequest::ptr header,
                                sylar::http::WSSession::ptr session) {
    SYLAR_LOG_INFO(g_logger) << "DDT onConnect " << session;
    return 0;
}

int32_t DDTWSServlet::onClose(sylar::http::HttpRequest::ptr header,
                              sylar::http::WSSession::ptr session) {
    SYLAR_LOG_INFO(g_logger) << "DDT onClose " << session;

    sylar::RWMutex::WriteLock lock(m_mutex);
    auto it = m_sessionPlayers.find(session.get());
    if (it != m_sessionPlayers.end()) {
        uint32_t pid = it->second->getId();
        uint64_t accountId = it->second->getAccountId();
        std::string name = it->second->getName();
        lock.unlock();

        RoomManager::Instance().leaveRoom(pid);
        if (accountId > 0) {
            DDTAuthManager::Instance().handleLogout(accountId);
            DDTChatManager::Instance().sendSystemMessage(
                name + " left the game", m_sessionPlayers);
        }
        SYLAR_LOG_INFO(g_logger) << "Player " << pid << " disconnected";

        lock.lock();
        m_sessionPlayers.erase(session.get());
        if (accountId > 0) {
            auto acctIt = m_accountSessions.find(accountId);
            if (acctIt != m_accountSessions.end() && acctIt->second == session.get()) {
                m_accountSessions.erase(acctIt);
            }
        }
    }
    broadcastRoomListToAll();
    return 0;
}

int32_t DDTWSServlet::handle(sylar::http::HttpRequest::ptr header,
                             sylar::http::WSFrameMessage::ptr wsMsg,
                             sylar::http::WSSession::ptr session) {
    ddt::GameMessage msg;
    if (!msg.ParseFromString(wsMsg->getData())) {
        SYLAR_LOG_WARN(g_logger) << "Failed to parse protobuf message";
        sendError(session, 400, "invalid protobuf");
        return 0;
    }

    switch (msg.payload_case()) {
        // ---- 注册 ----
        case GameMessage::kRegisterRequest: {
            auto& req = msg.register_request();
            SYLAR_LOG_INFO(g_logger) << "Register: " << req.name();

            auto result = DDTAuthManager::Instance().handleRegister(
                req.name(), req.password());

            ddt::GameMessage resp;
            auto* r = resp.mutable_register_response();
            r->set_ok(result.ok);
            r->set_id(result.accountId);
            r->set_msg(result.msg);

            std::string data;
            resp.SerializeToString(&data);
            session->sendMessage(data, sylar::http::WSFrameHead::BIN_FRAME);
            break;
        }

        // ---- 登录（兼容：有密码走认证，无密码走旧逻辑）----
        case GameMessage::kLoginRequest: {
            auto& req = msg.login_request();
            SYLAR_LOG_INFO(g_logger) << "Login: " << req.name();

            if (!req.password().empty()) {
                // Auth login
                SYLAR_LOG_INFO(g_logger) << "Login auth start: " << req.name();
                auto result = DDTAuthManager::Instance().handleLogin(
                    req.name(), req.password());
                SYLAR_LOG_INFO(g_logger) << "Login auth done: " << req.name() << " ok=" << result.ok;

                std::shared_ptr<Player> player;
                {
                    sylar::RWMutex::WriteLock lock(m_mutex);

                    // Kick existing session for this account
                    if (result.ok && result.accountId > 0) {
                        auto acctIt = m_accountSessions.find(result.accountId);
                        if (acctIt != m_accountSessions.end()) {
                            auto* oldSession = acctIt->second;
                            auto oldIt = m_sessionPlayers.find(oldSession);
                            if (oldIt != m_sessionPlayers.end()) {
                                SYLAR_LOG_WARN(g_logger) << "Kicking old session for account " << result.accountId;
                                kickSession(oldSession);
                                m_sessionPlayers.erase(oldIt);
                            }
                            m_accountSessions.erase(acctIt);
                        }
                    }

                    uint32_t id = m_nextPlayerId++;
                    player = std::make_shared<Player>(id, req.name(), session);
                    player->setAccountId(result.accountId);
                    m_sessionPlayers[session.get()] = player;
                    if (result.ok && result.accountId > 0) {
                        m_accountSessions[result.accountId] = session.get();
                    }

                    ddt::GameMessage resp;
                    auto* loginResp = resp.mutable_login_response();
                    loginResp->set_ok(result.ok);
                    loginResp->set_id(id);
                    loginResp->set_msg(result.msg);
                    loginResp->set_token(result.token);

                    std::string data;
                    resp.SerializeToString(&data);
                    player->sendMessage(data);
                } // lock released here

                if (result.ok) {
                    sylar::RWMutex::ReadLock lock(m_mutex);
                    DDTChatManager::Instance().sendSystemMessage(
                        req.name() + " joined the game", m_sessionPlayers);
                }
            } else {
                // Legacy login (no password)
                sylar::RWMutex::WriteLock lock(m_mutex);
                uint32_t id = m_nextPlayerId++;
                auto player = std::make_shared<Player>(id, req.name(), session);
                m_sessionPlayers[session.get()] = player;

                ddt::GameMessage resp;
                auto* loginResp = resp.mutable_login_response();
                loginResp->set_ok(true);
                loginResp->set_id(id);
                loginResp->set_msg("ok");

                std::string data;
                resp.SerializeToString(&data);
                player->sendMessage(data);
            }
            break;
        }

        case GameMessage::kJoinRoomRequest: {
            auto& req = msg.join_room_request();
            SYLAR_LOG_INFO(g_logger) << "JoinRoom: player looking for room_id=" << req.room_id() << " team=" << req.team();

            sylar::RWMutex::ReadLock lock(m_mutex);
            auto it = m_sessionPlayers.find(session.get());
            if (it == m_sessionPlayers.end()) {
                sendError(session, 401, "not login");
                break;
            }
            auto player = it->second;
            lock.unlock();

            auto room = RoomManager::Instance().joinRoom(player, req.room_id(), req.team());

            ddt::GameMessage resp;
            auto* joinResp = resp.mutable_join_room_response();
            if (room) {
                joinResp->set_ok(true);
                joinResp->set_room_id(room->getId());
                joinResp->set_msg("ok");
            } else {
                joinResp->set_ok(false);
                joinResp->set_msg("room full or already in room");
            }

            std::string data;
            resp.SerializeToString(&data);
            player->sendMessage(data);

            if (room) broadcastRoomListToAll();
            break;
        }

        case GameMessage::kRoomListRequest: {
            sylar::RWMutex::ReadLock lock(m_mutex);
            auto it = m_sessionPlayers.find(session.get());
            if (it == m_sessionPlayers.end()) break;
            auto player = it->second;
            lock.unlock();

            auto rooms = RoomManager::Instance().getRoomList();

            ddt::GameMessage resp;
            auto* r = resp.mutable_room_list_response();
            for (auto& ri : rooms) {
                *r->add_rooms() = ri;
            }

            std::string data;
            resp.SerializeToString(&data);
            player->sendMessage(data);
            break;
        }

        case GameMessage::kCreateRoomRequest: {
            auto& req = msg.create_room_request();
            SYLAR_LOG_INFO(g_logger) << "CreateRoom: " << req.room_name();

            sylar::RWMutex::ReadLock lock(m_mutex);
            auto it = m_sessionPlayers.find(session.get());
            if (it == m_sessionPlayers.end()) break;
            auto player = it->second;
            lock.unlock();

            auto room = RoomManager::Instance().createRoom(player, req.room_name());

            ddt::GameMessage resp;
            auto* r = resp.mutable_create_room_response();
            if (room) {
                r->set_ok(true);
                r->set_room_id(room->getId());
                r->set_msg("ok");
            } else {
                r->set_ok(false);
                r->set_msg("already in room or error");
            }

            std::string data;
            resp.SerializeToString(&data);
            player->sendMessage(data);

            if (room) broadcastRoomListToAll();
            break;
        }

        case GameMessage::kReadyRequest: {
            auto& req = msg.ready_request();

            sylar::RWMutex::ReadLock lock(m_mutex);
            auto it = m_sessionPlayers.find(session.get());
            if (it == m_sessionPlayers.end()) break;
            auto player = it->second;
            lock.unlock();

            auto room = RoomManager::Instance().findRoomByPlayer(player->getId());
            if (room) {
                room->setReady(player->getId(), req.ready());
                broadcastRoomListToAll();
            }
            break;
        }

        case GameMessage::kLeaveRoomRequest: {
            sylar::RWMutex::ReadLock lock(m_mutex);
            auto it = m_sessionPlayers.find(session.get());
            if (it == m_sessionPlayers.end()) break;
            auto player = it->second;
            lock.unlock();

            RoomManager::Instance().leaveRoom(player->getId());
            broadcastRoomListToAll();
            break;
        }

        case GameMessage::kSwitchTeamRequest: {
            auto& req = msg.switch_team_request();

            sylar::RWMutex::ReadLock lock(m_mutex);
            auto it = m_sessionPlayers.find(session.get());
            if (it == m_sessionPlayers.end()) break;
            auto player = it->second;
            lock.unlock();

            auto room = RoomManager::Instance().findRoomByPlayer(player->getId());

            ddt::GameMessage resp;
            auto* r = resp.mutable_switch_team_response();
            if (room && room->switchTeam(player->getId(), req.team())) {
                r->set_ok(true);
                r->set_msg("ok");
                r->set_team(req.team());
            } else {
                r->set_ok(false);
                r->set_msg("cannot switch team");
            }

            std::string data;
            resp.SerializeToString(&data);
            player->sendMessage(data);
            broadcastRoomListToAll();
            break;
        }

        case GameMessage::kShootRequest: {
            auto& req = msg.shoot_request();

            sylar::RWMutex::ReadLock lock(m_mutex);
            auto it = m_sessionPlayers.find(session.get());
            if (it == m_sessionPlayers.end()) break;
            auto player = it->second;
            lock.unlock();

            auto room = RoomManager::Instance().findRoomByPlayer(player->getId());
            if (room) {
                room->handleShoot(player->getId(), req.angle(), req.force());
            }
            break;
        }

        case GameMessage::kMoveRequest: {
            auto& req = msg.move_request();

            sylar::RWMutex::ReadLock lock(m_mutex);
            auto it = m_sessionPlayers.find(session.get());
            if (it == m_sessionPlayers.end()) break;
            auto player = it->second;
            lock.unlock();

            auto room = RoomManager::Instance().findRoomByPlayer(player->getId());
            if (room) {
                room->handleMove(player->getId(), req.delta_x());
            }
            break;
        }

        // ---- 聊天 ----
        case GameMessage::kChatRequest: {
            auto& req = msg.chat_request();

            sylar::RWMutex::ReadLock lock(m_mutex);
            auto it = m_sessionPlayers.find(session.get());
            if (it == m_sessionPlayers.end()) break;
            auto player = it->second;

            DDTChatManager::Instance().handleChatRequest(
                player->getAccountId(), player->getName(),
                player->getId(), req, session, m_sessionPlayers);
            break;
        }

        case GameMessage::kPrivateChatRequest: {
            auto& req = msg.private_chat_request();

            sylar::RWMutex::ReadLock lock(m_mutex);
            auto it = m_sessionPlayers.find(session.get());
            if (it == m_sessionPlayers.end()) break;
            auto player = it->second;

            DDTChatManager::Instance().handlePrivateChat(
                player->getAccountId(), player->getName(),
                player->getId(), req, session, m_sessionPlayers);
            break;
        }

        case GameMessage::kChatHistoryRequest: {
            auto& req = msg.chat_history_request();

            sylar::RWMutex::ReadLock lock(m_mutex);
            auto it = m_sessionPlayers.find(session.get());
            if (it == m_sessionPlayers.end()) break;
            auto player = it->second;

            auto history = DDTChatManager::Instance().getHistory(
                req.channel(), req.count());

            ddt::GameMessage resp;
            auto* histResp = resp.mutable_chat_history_response();
            histResp->set_channel(req.channel());
            for (auto& n : history) {
                *histResp->add_messages() = n;
            }

            std::string data;
            resp.SerializeToString(&data);
            player->sendMessage(data);
            break;
        }

        // ---- 好友 ----
        case GameMessage::kFriendAddRequest: {
            auto& req = msg.friend_add_request();

            sylar::RWMutex::ReadLock lock(m_mutex);
            auto it = m_sessionPlayers.find(session.get());
            if (it == m_sessionPlayers.end()) break;
            auto player = it->second;
            uint64_t myAccountId = player->getAccountId();
            lock.unlock();

            // Look up target by name
            std::string targetName = req.target_name();
            uint64_t targetAccountId = 0;
            {
                std::string name;
                // Find target in connected sessions first
                bool found = false;
                for (auto& kv : m_sessionPlayers) {
                    if (kv.second && kv.second->getName() == targetName) {
                        targetAccountId = kv.second->getAccountId();
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    // Try database lookup
                    if (DDTDatabase::Instance().getAccountName(
                            std::stoull(targetName), name)) {
                        targetAccountId = std::stoull(targetName);
                    }
                }
            }

            ddt::GameMessage resp;
            auto* r = resp.mutable_friend_add_response();
            if (targetAccountId == 0 || targetAccountId == myAccountId) {
                r->set_ok(false);
                r->set_msg("player not found");
            } else if (DDTDatabase::Instance().addFriend(myAccountId, targetAccountId)) {
                r->set_ok(true);
                r->set_msg("OK");
                r->set_friend_id(targetAccountId);
                r->set_friend_name(targetName);
            } else {
                r->set_ok(false);
                r->set_msg("already friends or error");
            }

            std::string data;
            resp.SerializeToString(&data);
            player->sendMessage(data);
            break;
        }

        case GameMessage::kFriendListRequest: {
            sylar::RWMutex::ReadLock lock(m_mutex);
            auto it = m_sessionPlayers.find(session.get());
            if (it == m_sessionPlayers.end()) break;
            auto player = it->second;
            uint64_t myAccountId = player->getAccountId();
            lock.unlock();

            auto friends = DDTDatabase::Instance().getFriendList(myAccountId);

            ddt::GameMessage resp;
            auto* r = resp.mutable_friend_list_response();
            for (auto& f : friends) {
                auto* fi = r->add_friends();
                fi->set_id(f.account_id);
                fi->set_name(f.name);
                fi->set_level(f.level);
                fi->set_online(false);
                // Check online status from session players
                for (auto& kv : m_sessionPlayers) {
                    if (kv.second && kv.second->getAccountId() == f.account_id) {
                        fi->set_online(true);
                        break;
                    }
                }
            }

            std::string data;
            resp.SerializeToString(&data);
            player->sendMessage(data);
            break;
        }

        default:
            SYLAR_LOG_WARN(g_logger) << "Unknown message type: " << msg.payload_case();
            break;
    }

    return 0;
}

void DDTWSServlet::sendError(sylar::http::WSSession::ptr session,
                             int code, const std::string& msg) {
    ddt::GameMessage resp;
    auto* err = resp.mutable_error_notify();
    err->set_code(code);
    err->set_msg(msg);

    std::string data;
    resp.SerializeToString(&data);
    session->sendMessage(data, sylar::http::WSFrameHead::BIN_FRAME);
}

void DDTWSServlet::broadcastRoomListToAll() {
    auto rooms = RoomManager::Instance().getRoomList();

    ddt::GameMessage msg;
    auto* resp = msg.mutable_room_list_response();
    for (auto& ri : rooms) {
        *resp->add_rooms() = ri;
    }

    std::string data;
    msg.SerializeToString(&data);

    sylar::RWMutex::ReadLock lock(m_mutex);
    for (auto& kv : m_sessionPlayers) {
        if (kv.second) {
            kv.second->sendMessage(data);
        }
    }
}

} // namespace ddt
