#ifndef __SYLAR_RPC_RPC_PROVIDER_H__
#define __SYLAR_RPC_RPC_PROVIDER_H__

#include <google/protobuf/service.h>
#include <memory>
#include <unordered_map>

#include "sylar/net/tcp_server.h"
#include "sylar/net/socket_stream.h"
#include "sylar/rpc/etcd_client.h"

namespace sylar {
namespace rpc {

// RPC 服务提供者：注册 service、向 etcd 注册路由、监听端口并处理请求。
class RpcProvider : public sylar::TcpServer {
public:
    typedef std::shared_ptr<RpcProvider> ptr;

    RpcProvider();
    ~RpcProvider();

    void notifyService(google::protobuf::Service* service);
    void setEtcd(const std::string& endpoint, int ttl = 30);
    void setListen(uint16_t port);
    void setAdvertise(const std::string& host_port);
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
    EtcdRegistrar::ptr m_registrar;
    std::string m_etcdEndpoint = "http://127.0.0.1:2379";
    int m_leaseTtl = 30;
    uint16_t m_listenPort = 9000;
    std::string m_advertise = "127.0.0.1:9000";
};

}  // namespace rpc
}  // namespace sylar

#endif
