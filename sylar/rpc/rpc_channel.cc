#include "rpc_channel.h"
#include "rpcheader.pb.h"
#include "sylar/log.h"
#include "sylar/socket.h"
#include "sylar/socket_stream.h"
#include "sylar/address.h"
#include "sylar/endian.h"
#include "sylar/zk_client.h"

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>

namespace sylar {

static sylar::Logger::ptr g_rpclogger = SYLAR_LOG_NAME("rpc");

namespace rpc {

using namespace google::protobuf;

RpcChannel::RpcChannel(const std::string& zkHost)
    : m_zkHost(zkHost) {
}

RpcChannel::~RpcChannel() {
}

void RpcChannel::CallMethod(
        const MethodDescriptor* method,
        ::google::protobuf::RpcController* controller,
        const Message* request,
        Message* response,
        Closure* done) {

    const ServiceDescriptor* serviceDesc = method->service();
    std::string serviceName = serviceDesc->name();
    std::string methodName = method->name();

    // Serialize request args
    std::string argsStr;
    if(!request->SerializeToString(&argsStr)) {
        if(controller) controller->SetFailed("serialize request failed");
        return;
    }

    // Build RPC header
    uint32_t argsSize = argsStr.size();
    sylar::rpc::RpcHeader rpcHeader;
    rpcHeader.set_service_name(serviceName);
    rpcHeader.set_method_name(methodName);
    rpcHeader.set_args_size(argsSize);

    std::string rpcHeaderStr;
    if(!rpcHeader.SerializeToString(&rpcHeaderStr)) {
        if(controller) controller->SetFailed("serialize rpc header failed");
        return;
    }

    uint32_t headerSize = rpcHeaderStr.size();

    // Build packet: [4-byte header_size][rpc_header][args]
    std::string packet;
    uint32_t netHeaderSize = sylar::byteswapOnLittleEndian(headerSize);
    packet.append(reinterpret_cast<char*>(&netHeaderSize), 4);
    packet.append(rpcHeaderStr);
    packet.append(argsStr);

    SYLAR_LOG_DEBUG(g_rpclogger) << "rpc call: service=" << serviceName
        << " method=" << methodName << " header_size=" << headerSize
        << " args_size=" << argsSize;

    // Service discovery via ZooKeeper
    ZKClient::ptr zkCli = std::make_shared<ZKClient>();
    zkCli->init(m_zkHost, 30000,
        [](int type, int stat, const std::string& path, ZKClient::ptr) {});

    std::string method_path = "/" + serviceName + "/" + methodName;
    std::string hostData;
    hostData.resize(64);
    int rt = zkCli->get(method_path, hostData, false);
    zkCli->close();

    if(rt != ZOK || hostData.empty()) {
        if(controller) controller->SetFailed(method_path + " not found in zookeeper");
        return;
    }

    int idx = hostData.find(':');
    if(idx == -1) {
        if(controller) controller->SetFailed("invalid service address: " + hostData);
        return;
    }

    std::string ip = hostData.substr(0, idx);
    uint16_t port = atoi(hostData.substr(idx + 1).c_str());

    SYLAR_LOG_INFO(g_rpclogger) << "rpc connecting to " << ip << ":" << port;

    // Connect to RPC server
    IPAddress::ptr addr = IPAddress::Create(ip.c_str(), port);
    Socket::ptr sock = Socket::CreateTCP(addr);
    if(!sock->connect(addr)) {
        if(controller) controller->SetFailed("connect failed to " + ip + ":" + std::to_string(port));
        return;
    }

    // Send request
    sock->send(packet.c_str(), packet.size());

    // Read response: [4-byte size][response]
    sylar::SocketStream ss(sock);
    uint32_t respSize = 0;
    if(ss.readFixSize(&respSize, sizeof(respSize)) <= 0) {
        if(controller) controller->SetFailed("read response size failed");
        sock->close();
        return;
    }
    respSize = sylar::byteswapOnLittleEndian(respSize);

    std::string respStr;
    respStr.resize(respSize);
    if(ss.readFixSize(&respStr[0], respSize) <= 0) {
        if(controller) controller->SetFailed("read response body failed");
        sock->close();
        return;
    }

    // Deserialize response
    if(!response->ParseFromString(respStr)) {
        if(controller) controller->SetFailed("parse response failed");
    }

    SYLAR_LOG_INFO(g_rpclogger) << "rpc call completed: " << serviceName << "." << methodName;
    sock->close();
}

}
}
