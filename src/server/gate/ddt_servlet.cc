#include "ddt_servlet.h"
#include "ddt_room_manager.h"
#include "ddt_auth.h"
#include "ddt_chat_manager.h"
#include "ddt_database.h"
#include "ddt.pb.h"
#include "ddt_config.h"
#include "sylar/core/log.h"
#include "sylar/core/sys_util.h"
#include "sylar/scheduler/iomanager.h"

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
    
    // 修复：getAllSessions 返回的是临时组装的 map 右值，去掉 '&'
    auto sessions = m_sessions.getAllSessions();
    for (auto& kv : sessions) {
        if (kv.second) {
            kv.second->sendMessage(data);
        }
    }
    SYLAR_LOG_INFO(g_logger) << "Broadcast shutdown to " << sessions.size() << " clients";
}

int32_t DDTWSServlet::onConnect(sylar::http::HttpRequest::ptr header,
                                sylar::http::WSSession::ptr session) {
    SYLAR_LOG_INFO(g_logger) << "DDT onConnect " << session;
    m_sessions.updateHeartbeat(session.get());
    startHeartbeatTimer();
    return 0;
}

int32_t DDTWSServlet::onClose(sylar::http::HttpRequest::ptr header,
                              sylar::http::WSSession::ptr session) {
    SYLAR_LOG_INFO(g_logger) << "DDT onClose " << session;
    m_sessions.removeSession(session.get());
    broadcastRoomListToAll();
    return 0;
}

// ---- 主分发路由器 ----

int32_t DDTWSServlet::handle(sylar::http::HttpRequest::ptr header,
                             sylar::http::WSFrameMessage::ptr wsMsg,
                             sylar::http::WSSession::ptr session) {
    m_sessions.updateHeartbeat(session.get());

    ddt::GameMessage msg;
    if (!msg.ParseFromString(wsMsg->getData())) {
        SYLAR_LOG_WARN(g_logger) << "Failed to parse protobuf message";
        sendError(session, 400, "invalid protobuf");
        return 0;
    }
    
    Player::ptr player = m_sessions.getPlayer(session.get());
    
    switch (msg.payload_case()) {
        case GameMessage::kRegisterRequest:
            handleRegister(session, player, msg); break;
        case GameMessage::kLoginRequest:
            handleLogin(session, player, msg); break;
        case GameMessage::kJoinRoomRequest:
            handleJoinRoom(session, player, msg); break;
        case GameMessage::kRoomListRequest:
            handleRoomList(session, player); break;
        case GameMessage::kCreateRoomRequest:
            handleCreateRoom(session, player, msg); break;
        case GameMessage::kReadyRequest:
            handleReady(session, player, msg); break;
        case GameMessage::kLeaveRoomRequest:
            handleLeaveRoom(session, player); break;
        case GameMessage::kSwitchTeamRequest:
            handleSwitchTeam(session, player, msg); break;
        case GameMessage::kShootRequest:
            handleShoot(session, player, msg); break;
        case GameMessage::kMoveRequest:
            handleMove(session, player, msg); break;
        case GameMessage::kPassRequest:
            handlePass(session, player, msg); break;
        case GameMessage::kChatRequest:
            handleChat(session, player, msg); break;
        case GameMessage::kPrivateChatRequest:
            handlePrivateChat(session, player, msg); break;
        case GameMessage::kChatHistoryRequest:
            handleChatHistory(session, player, msg); break;
        case GameMessage::kFriendAddRequest:
            handleFriendAdd(session, player, msg); break;
        case GameMessage::kFriendListRequest:
            handleFriendList(session, player); break;
        case GameMessage::kHeartbeatRequest:
            handleHeartbeat(session); break;
        default:
            SYLAR_LOG_WARN(g_logger) << "Unknown message type: " << msg.payload_case();
            break;
    }
    return 0;
}

// ---- 各消息处理器 ----

void DDTWSServlet::handleRegister(sylar::http::WSSession::ptr session,
                                   Player::ptr player, ddt::GameMessage& msg) {
    auto& req = msg.register_request();
    SYLAR_LOG_INFO(g_logger) << "Register: " << req.name();

    auto result = DDTAuthManager::Instance().handleRegister(req.name(), req.password());
    
    ddt::GameMessage resp;
    auto* r = resp.mutable_register_response();
    r->set_ok(result.ok);
    r->set_id(result.accountId);
    r->set_msg(result.msg);
    
    std::string data;
    resp.SerializeToString(&data);
    session->sendMessage(data, sylar::http::WSFrameHead::BIN_FRAME);
}

void DDTWSServlet::handleLogin(sylar::http::WSSession::ptr session,
                                Player::ptr player, ddt::GameMessage& msg) {
    auto& req = msg.login_request();
    SYLAR_LOG_INFO(g_logger) << "Login: " << req.name();

    if (!req.password().empty()) {
        auto result = DDTAuthManager::Instance().handleLogin(req.name(), req.password());
        SYLAR_LOG_INFO(g_logger) << "Login auth done: " << req.name() << " ok=" << result.ok;
    
        std::shared_ptr<Player> newPlayer;
        {
            // 踢掉同账号旧 session
            if (result.ok && result.accountId > 0) {
                m_sessions.kickExistingSession(result.accountId);
            }
    
            uint32_t id = m_sessions.getNextPlayerId();
            newPlayer = std::make_shared<Player>(id, req.name(), session);
            newPlayer->setAccountId(result.accountId);
            m_sessions.addSession(session, newPlayer);
    
            ddt::GameMessage resp;
            auto* loginResp = resp.mutable_login_response();
            loginResp->set_ok(result.ok);
            loginResp->set_id(id);
            loginResp->set_msg(result.msg);
            loginResp->set_token(result.token);
    
            std::string data;
            resp.SerializeToString(&data);
            newPlayer->sendMessage(data);
        }
    
        if (result.ok) {
            // 修复：按值接收 map
            auto sessions = m_sessions.getAllSessions();
            DDTChatManager::Instance().sendSystemMessage(
                req.name() + " joined the game", sessions);
        }
    } else {
        uint32_t id = m_sessions.getNextPlayerId();
        auto newPlayer = std::make_shared<Player>(id, req.name(), session);
        m_sessions.addSession(session, newPlayer);
    
        ddt::GameMessage resp;
        auto* loginResp = resp.mutable_login_response();
        loginResp->set_ok(true);
        loginResp->set_id(id);
        loginResp->set_msg("ok");
    
        std::string data;
        resp.SerializeToString(&data);
        newPlayer->sendMessage(data);
    }
}

void DDTWSServlet::handleJoinRoom(sylar::http::WSSession::ptr session,
                                   Player::ptr player, ddt::GameMessage& msg) {
    auto& req = msg.join_room_request();
    SYLAR_LOG_INFO(g_logger) << "JoinRoom: player looking for room_id=" << req.room_id() << " team=" << req.team();
    if (!player) return;

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
}

void DDTWSServlet::handleRoomList(sylar::http::WSSession::ptr session,
                                   Player::ptr player) {
    if (!player) return;
    auto rooms = RoomManager::Instance().getRoomList();

    ddt::GameMessage resp;
    auto* r = resp.mutable_room_list_response();
    for (auto& ri : rooms) {
        *r->add_rooms() = ri;
    }
    
    std::string data;
    resp.SerializeToString(&data);
    player->sendMessage(data);
}

void DDTWSServlet::handleCreateRoom(sylar::http::WSSession::ptr session,
                                     Player::ptr player, ddt::GameMessage& msg) {
    auto& req = msg.create_room_request();
    SYLAR_LOG_INFO(g_logger) << "CreateRoom: " << req.room_name();
    if (!player) return;

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
}

void DDTWSServlet::handleReady(sylar::http::WSSession::ptr session,
                                Player::ptr player, ddt::GameMessage& msg) {
    if (!player) return;
    auto room = RoomManager::Instance().findRoomByPlayer(player->getId());
    if (room) {
        const bool ready_val = msg.ready_request().ready();
        // [BugFix] broadcastRoomListToAll 放入 Actor 任务内，确保读取房间状态时无竞争
        auto servlet = this;
        room->post([room, player, ready_val, servlet]() {
            room->setReady(player->getId(), ready_val);
            servlet->broadcastRoomListToAll();
        });
    }
}

void DDTWSServlet::handleLeaveRoom(sylar::http::WSSession::ptr session,
                                    Player::ptr player) {
    if (!player) return;
    RoomManager::Instance().leaveRoom(player->getId());
    // leaveRoom 现在通过 Actor 异步执行 removePlayer，此处广播可能略滞后，
    // 但 lobby 刷新机制保证最终一致。仍保留广播以避免 lobby 显示长时间不更新。
    broadcastRoomListToAll();
}

void DDTWSServlet::handleSwitchTeam(sylar::http::WSSession::ptr session,
                                     Player::ptr player, ddt::GameMessage& msg) {
    if (!player) return;
    auto room = RoomManager::Instance().findRoomByPlayer(player->getId());

    ddt::GameMessage resp;
    auto* r = resp.mutable_switch_team_response();
    if (room && room->switchTeam(player->getId(), msg.switch_team_request().team())) {
        r->set_ok(true);
        r->set_msg("ok");
        r->set_team(msg.switch_team_request().team());
    } else {
        r->set_ok(false);
        r->set_msg("cannot switch team");
    }

    std::string data;
    resp.SerializeToString(&data);
    player->sendMessage(data);
    // switchTeam 在 Actor 线程直接执行（已持 Actor 锁），此处广播安全
    broadcastRoomListToAll();
}

void DDTWSServlet::handleShoot(sylar::http::WSSession::ptr session,
                                Player::ptr player, ddt::GameMessage& msg) {
    if (!player) return;
    auto room = RoomManager::Instance().findRoomByPlayer(player->getId());
    if (room) {
        uint32_t pid = player->getId();
        int angle = msg.shoot_request().angle();
        double force = msg.shoot_request().force();
        bool is_fly = msg.shoot_request().is_fly(); // 解析出纸飞机标志
        room->post([room, pid, angle, force, is_fly]() {
            room->handleShoot(pid, angle, force, is_fly);  // 传给逻辑层
        });
    }
}

void DDTWSServlet::handleMove(sylar::http::WSSession::ptr session,
                               Player::ptr player, ddt::GameMessage& msg) {
    if (!player) return;
    auto room = RoomManager::Instance().findRoomByPlayer(player->getId());
    if (room) {
        uint32_t pid = player->getId();
        float dx = msg.move_request().delta_x();
        room->post([room, pid, dx]() {
            room->handleMove(pid, dx);
        });
    }
}

void DDTWSServlet::handlePass(sylar::http::WSSession::ptr session, Player::ptr player, ddt::GameMessage& msg) {
    if (!player) return;
    auto room = RoomManager::Instance().findRoomByPlayer(player->getId());
    if (room) {
        uint32_t pid = player->getId();
        room->post([room, pid]() {
            room->handlePass(pid);
        });
    }
}

void DDTWSServlet::handleChat(sylar::http::WSSession::ptr session,
                               Player::ptr player, ddt::GameMessage& msg) {
    if (!player) return;
    // 修复：去掉 '&'
    auto sessions = m_sessions.getAllSessions();
    DDTChatManager::Instance().handleChatRequest(
        player->getAccountId(), player->getName(),
        player->getId(), msg.chat_request(), session, sessions);
}

void DDTWSServlet::handlePrivateChat(sylar::http::WSSession::ptr session,
                                      Player::ptr player, ddt::GameMessage& msg) {
    if (!player) return;
    // 修复：去掉 '&'
    auto sessions = m_sessions.getAllSessions();
    DDTChatManager::Instance().handlePrivateChat(
        player->getAccountId(), player->getName(),
        player->getId(), msg.private_chat_request(), session, sessions);
}

void DDTWSServlet::handleChatHistory(sylar::http::WSSession::ptr session,
                                      Player::ptr player, ddt::GameMessage& msg) {
    if (!player) return;
    auto history = DDTChatManager::Instance().getHistory(
        msg.chat_history_request().channel(), msg.chat_history_request().count());

    ddt::GameMessage resp;
    auto* histResp = resp.mutable_chat_history_response();
    histResp->set_channel(msg.chat_history_request().channel());
    for (auto& n : history) {
        *histResp->add_messages() = n;
    }
    
    std::string data;
    resp.SerializeToString(&data);
    player->sendMessage(data);
}

void DDTWSServlet::handleFriendAdd(sylar::http::WSSession::ptr session,
                                    Player::ptr player, ddt::GameMessage& msg) {
    if (!player) return;
    uint64_t myAccountId = player->getAccountId();

    std::string targetName = msg.friend_add_request().target_name();
    uint64_t targetAccountId = 0;
    {
        // 修复：去掉 '&'
        auto sessions = m_sessions.getAllSessions();
        for (auto& kv : sessions) {
            if (kv.second && kv.second->getName() == targetName) {
                targetAccountId = kv.second->getAccountId();
                break;
            }
        }
        if (targetAccountId == 0) {
            std::string name;
            if (DDTDatabase::Instance().getAccountName(std::stoull(targetName), name)) {
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
}

void DDTWSServlet::handleFriendList(sylar::http::WSSession::ptr session,
                                     Player::ptr player) {
    if (!player) return;
    uint64_t myAccountId = player->getAccountId();

    auto friends = DDTDatabase::Instance().getFriendList(myAccountId);
    
    ddt::GameMessage resp;
    auto* r = resp.mutable_friend_list_response();
    for (auto& f : friends) {
        auto* fi = r->add_friends();
        fi->set_id(f.account_id);
        fi->set_name(f.name);
        fi->set_level(f.level);
        fi->set_online(false);
        
        // 修复：去掉 '&'
        auto sessions = m_sessions.getAllSessions();
        for (auto& kv : sessions) {
            if (kv.second && kv.second->getAccountId() == f.account_id) {
                fi->set_online(true);
                break;
            }
        }
    }
    
    std::string data;
    resp.SerializeToString(&data);
    player->sendMessage(data);
}

void DDTWSServlet::handleHeartbeat(sylar::http::WSSession::ptr session) {
    ddt::GameMessage resp;
    auto* r = resp.mutable_heartbeat_response();
    r->set_server_time(sylar::GetCurrentMS());
    std::string data;
    resp.SerializeToString(&data);
    session->sendMessage(data, sylar::http::WSFrameHead::BIN_FRAME);
}

// ---- 工具方法 ----

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
    
    // 修复：去掉 '&'
    auto sessions = m_sessions.getAllSessions();
    for (auto& kv : sessions) {
        if (kv.second) {
            kv.second->sendMessage(data);
        }
    }
}

void DDTWSServlet::startHeartbeatTimer() {
    if (m_heartbeatTimer) return;
    auto& cfg = GameConfig::Instance();
    uint64_t intervalMs = cfg.heartbeat_check_interval * 1000;
    m_heartbeatTimer = sylar::IOManager::GetThis()->addTimer(intervalMs,
        [this]() { checkHeartbeat(); }, true);
    SYLAR_LOG_INFO(g_logger) << "Heartbeat timer started, interval="
        << cfg.heartbeat_check_interval << "s, timeout="
        << cfg.heartbeat_timeout << "s";
}

void DDTWSServlet::checkHeartbeat() {
    auto& cfg = GameConfig::Instance();
    m_sessions.cleanExpiredSessions(cfg.heartbeat_timeout * 1000);
}

} // namespace ddt