#ifndef __SYLAR_RPC_RPC_PROVIDER_H__
#define __SYLAR_RPC_RPC_PROVIDER_H__

#include <google/protobuf/service.h>
#include <memory>
#include <unordered_map>

#include "sylar/net/tcp_server.h"
#include "sylar/net/socket_stream.h"
#include "sylar/rpc/zk_client.h"

namespace sylar {
namespace rpc {

class RpcProvider : public sylar::TcpServer {
public:
    typedef std::shared_ptr<RpcProvider> ptr;
    RpcProvider();
    ~RpcProvider();

    void notifyService(google::protobuf::Service* service);
    void run();

protected:
    void handleClient(sylar::Socket::ptr client) override;

private:
    void sendResponse(sylar::Socket::ptr sock, google::protobuf::Message* response);

    using MethodMap = std::unordered_map<std::string, const google::protobuf::MethodDescriptor*>;

    struct ServiceInfo {
        google::protobuf::Service* service;
        MethodMap methodMap;
    };

    using ServiceMap = std::unordered_map<std::string, ServiceInfo>;
    ServiceMap m_serviceMap;
    ZKClient::ptr m_zkClient;
};

}
}

#endif
