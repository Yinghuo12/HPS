#ifndef __DDT_SERVLET_H__
#define __DDT_SERVLET_H__

#include "sylar/http/ws_servlet.h"
#include "ddt_session_mgr.h"
#include "sylar/scheduler/timer.h"

namespace ddt { class GameMessage; }

namespace ddt {

class DDTWSServlet : public sylar::http::WSServlet {
public:
    typedef std::shared_ptr<DDTWSServlet> ptr;
    DDTWSServlet();

    int32_t onConnect(sylar::http::HttpRequest::ptr header,
                      sylar::http::WSSession::ptr session) override;
    int32_t onClose(sylar::http::HttpRequest::ptr header,
                    sylar::http::WSSession::ptr session) override;
    void broadcastShutdown();
    int32_t handle(sylar::http::HttpRequest::ptr header,
                   sylar::http::WSFrameMessage::ptr msg,
                   sylar::http::WSSession::ptr session) override;

private:
    void sendError(sylar::http::WSSession::ptr session, int code, const std::string& msg);
    void broadcastRoomListToAll();
    void startHeartbeatTimer();
    void checkHeartbeat();

    // 各消息类型的独立处理器
    void handleRegister(sylar::http::WSSession::ptr session, Player::ptr player, ddt::GameMessage& msg);
    void handleLogin(sylar::http::WSSession::ptr session, Player::ptr player, ddt::GameMessage& msg);
    void handleJoinRoom(sylar::http::WSSession::ptr session, Player::ptr player, ddt::GameMessage& msg);
    void handleRoomList(sylar::http::WSSession::ptr session, Player::ptr player);
    void handleCreateRoom(sylar::http::WSSession::ptr session, Player::ptr player, ddt::GameMessage& msg);
    void handleReady(sylar::http::WSSession::ptr session, Player::ptr player, ddt::GameMessage& msg);
    void handleLeaveRoom(sylar::http::WSSession::ptr session, Player::ptr player);
    void handleSwitchTeam(sylar::http::WSSession::ptr session, Player::ptr player, ddt::GameMessage& msg);
    void handleShoot(sylar::http::WSSession::ptr session, Player::ptr player, ddt::GameMessage& msg);
    void handleMove(sylar::http::WSSession::ptr session, Player::ptr player, ddt::GameMessage& msg);
    void handlePass(sylar::http::WSSession::ptr session, Player::ptr player, ddt::GameMessage& msg);
    void handleChat(sylar::http::WSSession::ptr session, Player::ptr player, ddt::GameMessage& msg);
    void handlePrivateChat(sylar::http::WSSession::ptr session, Player::ptr player, ddt::GameMessage& msg);
    void handleChatHistory(sylar::http::WSSession::ptr session, Player::ptr player, ddt::GameMessage& msg);
    void handleFriendAdd(sylar::http::WSSession::ptr session, Player::ptr player, ddt::GameMessage& msg);
    void handleFriendList(sylar::http::WSSession::ptr session, Player::ptr player);
    void handleHeartbeat(sylar::http::WSSession::ptr session);

    SessionManager m_sessions;  // session 生命周期管理（从 servlet 解耦）
    sylar::Timer::ptr m_heartbeatTimer;
};

} // namespace ddt

#endif
