#ifndef __DDT_SERVLET_H__
#define __DDT_SERVLET_H__

#include "sylar/http/ws_servlet.h"
#include <map>
#include "sylar/thread.h"
#include "ddt_player.h"

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
    void kickSession(sylar::http::WSSession* session);

    sylar::RWMutex m_mutex;
    std::map<sylar::http::WSSession*, Player::ptr> m_sessionPlayers;
    std::map<uint64_t, sylar::http::WSSession*> m_accountSessions;  // accountId -> session
    uint32_t m_nextPlayerId = 1;
};

} // namespace ddt

#endif
