#ifndef __SYLAR_RPC_RPC_CHANNEL_H__
#define __SYLAR_RPC_RPC_CHANNEL_H__

#include <google/protobuf/service.h>
#include <memory>
#include <string>

#include "sylar/rpc/etcd_client.h"

namespace sylar {
namespace rpc {

class RpcChannelPool;  // 前置声明

// RPC 客户端 channel：负责序列化、服务发现、网络收发。
class RpcChannel : public google::protobuf::RpcChannel {
public:
    typedef std::shared_ptr<RpcChannel> ptr;

    // 无 pool：走原短连接逻辑（每次新建 etcd client + socket + close）。向后兼容。
    RpcChannel(const std::string& etcdEndpoint = "http://127.0.0.1:2379");
    // 有 pool：复用 socket 连接池 + 服务发现缓存。pool 生命周期由调用方管（裸指针）。
    RpcChannel(const std::string& etcdEndpoint, RpcChannelPool* pool);
    ~RpcChannel();

    void CallMethod(
        const google::protobuf::MethodDescriptor* method,
        google::protobuf::RpcController* controller,
        const google::protobuf::Message* request,
        google::protobuf::Message* response,
        google::protobuf::Closure* done) override;

private:
    std::string m_etcdEndpoint;
    RpcChannelPool* m_pool = nullptr;  // 可选连接池（空则走短连接）
};

}  // namespace rpc
}  // namespace sylar

#endif
