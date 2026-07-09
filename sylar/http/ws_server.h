#ifndef __SYLAR_HTTP_WS_SERVER_H__
#define __SYLAR_HTTP_WS_SERVER_H__

#include "sylar/net/tcp_server.h"
#include "sylar/http/ws_session.h"
#include "sylar/http/ws_servlet.h"

namespace sylar {
namespace http {

class WSServer : public TcpServer {
public:
    typedef std::shared_ptr<WSServer> ptr;

    WSServer(sylar::IOManager* worker = sylar::IOManager::GetThis(), sylar::IOManager* accept_worker = sylar::IOManager::GetThis());
    WSServletDispatch::ptr getWSServletDispatch() const { return m_dispatch;}
    void setWSServletDispatch(WSServletDispatch::ptr v) { m_dispatch = v;}

protected:
    virtual void handleClient(Socket::ptr client) override;
    
protected:
    WSServletDispatch::ptr m_dispatch;
};

}
}

#endif