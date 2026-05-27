#ifndef __SYLAR_RPC_RPC_CHANNEL_H__
#define __SYLAR_RPC_RPC_CHANNEL_H__

#include <google/protobuf/service.h>
#include <memory>
#include <string>

namespace sylar {
namespace rpc {

class RpcChannel : public google::protobuf::RpcChannel {
public:
    typedef std::shared_ptr<RpcChannel> ptr;

    RpcChannel(const std::string& zkHost = "127.0.0.1:2181");
    ~RpcChannel();

    void CallMethod(const google::protobuf::MethodDescriptor* method,
        google::protobuf::RpcController* controller,
        const google::protobuf::Message* request,
        google::protobuf::Message* response,
        google::protobuf::Closure* done) override;

private:
    std::string m_zkHost;
};

}
}

#endif
