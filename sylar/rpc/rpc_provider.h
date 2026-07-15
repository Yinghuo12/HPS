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

class RpcProvider : public sylar::TcpServer {
public:
    typedef std::shared_ptr<RpcProvider> ptr;
    RpcProvider();
    ~RpcProvider();

    void notifyService(google::protobuf::Service* service);
    // 设置 etcd 地址（默认 http://127.0.0.1:2379）与租约 TTL（秒）。
    void setEtcd(const std::string& endpoint, int ttl = 30);
    // 微服务: 设置监听端口 与 etcd 注册的对外可达地址("ip:port")。
    // 不调用则默认 0.0.0.0:9000 / 127.0.0.1:9000。
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

}
}

#endif
