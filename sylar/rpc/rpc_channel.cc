// RPC 客户端 Channel：实现 protobuf RpcChannel 接口。
// 有 pool: 基于 RpcStream(AsyncSocketStream) 长连接多路复用, request_id 匹配响应。
// 无 pool: 短连接模式(每次新建 socket+connect+close), 向后兼容。
#include "rpc_channel.h"
#include "rpc_stream.h"
#include "rpcheader.pb.h"
#include "rpc_controller.h"
#include "sylar/core/log.h"
#include "sylar/net/socket.h"
#include "sylar/net/socket_stream.h"
#include "sylar/net/address.h"
#include "sylar/core/endian.h"
#include "sylar/rpc/etcd_client.h"
#include "sylar/rpc/rpc_channel_pool.h"
#include "sylar/scheduler/iomanager.h"

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>

namespace sylar {

static sylar::Logger::ptr g_rpclogger = SYLAR_LOG_NAME("rpc");

namespace rpc {

using namespace google::protobuf;

RpcChannel::RpcChannel(const std::string& etcdEndpoint)
    : m_etcdEndpoint(etcdEndpoint) {
}

RpcChannel::RpcChannel(const std::string& etcdEndpoint, RpcChannelPool* pool)
    : m_etcdEndpoint(etcdEndpoint)
    , m_pool(pool) {
}

RpcChannel::~RpcChannel() {
}

// 构建 RPC 请求包: [4B header_size][RpcHeader][args]
static std::string buildRequestPacket(
        const std::string& serviceName, const std::string& methodName,
        const std::string& argsStr, const std::string& traceId, uint32_t requestId) {
    sylar::rpc::RpcHeader rpcHeader;
    rpcHeader.set_service_name(serviceName);
    rpcHeader.set_method_name(methodName);
    rpcHeader.set_args_size((uint32_t)argsStr.size());
    if(!traceId.empty()) {
        rpcHeader.set_trace_id(traceId);
    }
    rpcHeader.set_request_id(requestId);

    std::string rpcHeaderStr;
    rpcHeader.SerializeToString(&rpcHeaderStr);

    uint32_t headerSize = (uint32_t)rpcHeaderStr.size();
    uint32_t netHeaderSize = sylar::byteswapOnLittleEndian(headerSize);

    std::string packet;
    packet.append(reinterpret_cast<char*>(&netHeaderSize), 4);
    packet.append(rpcHeaderStr);
    packet.append(argsStr);
    return packet;
}

// 服务发现: 从 pool 缓存或 etcd 查址
static bool discoverService(RpcChannelPool* pool, const std::string& etcdEndpoint,
                           const std::string& method_path, std::string& ip, uint16_t& port) {
    if(pool && pool->getDiscovery(method_path, ip, port)) {
        return true;
    }
    EtcdClient cli(etcdEndpoint);
    EtcdClient::KV kv;
    if(cli.get(method_path, kv) && !kv.value.empty()) {
        auto idx = kv.value.find(':');
        if(idx != std::string::npos) {
            ip = kv.value.substr(0, idx);
            port = (uint16_t)atoi(kv.value.substr(idx + 1).c_str());
            if(pool) {
                pool->putDiscovery(method_path, ip, port);
            }
            return true;
        }
    }
    return false;
}

// 短连接模式: 直接 send + readFixSize(向后兼容)
static void callMethodShort(
        const MethodDescriptor* method, RpcController* controller,
        const Message* request, Message* response,
        const std::string& etcdEndpoint, const std::string& packet,
        const std::string& serviceName, const std::string& methodName) {
    std::string method_path = "/" + serviceName + "/" + methodName;
    std::string ip;
    uint16_t port = 0;
    if(!discoverService(nullptr, etcdEndpoint, method_path, ip, port)) {
        if(controller) {
            controller->SetFailed(method_path + " not found in etcd");
        }
        return;
    }

    SYLAR_LOG_INFO(g_rpclogger) << "rpc connecting to " << ip << ":" << port;

    IPAddress::ptr addr = IPAddress::Create(ip.c_str(), port);
    Socket::ptr sock = Socket::CreateTCP(addr);
    if(!sock->connect(addr)) {
        if(controller) {
            controller->SetFailed("connect failed to " + ip + ":" + std::to_string(port));
        }
        return;
    }

    sock->send(packet.c_str(), packet.size());

    // 读响应: [4B total_size][4B request_id][body]
    sylar::SocketStream ss(sock);
    uint32_t totalSize = 0;
    if(ss.readFixSize(&totalSize, sizeof(totalSize)) <= 0) {
        if(controller) {
            controller->SetFailed("read response size failed");
        }
        sock->close();
        return;
    }
    totalSize = sylar::byteswapOnLittleEndian(totalSize);
    if(totalSize > 64u * 1024 * 1024) {
        if(controller) {
            controller->SetFailed("response size abnormal");
        }
        sock->close();
        return;
    }

    // 读 request_id(短连接忽略, 只取 body)
    uint32_t respReqId = 0;
    if(totalSize >= sizeof(uint32_t)) {
        ss.readFixSize(&respReqId, sizeof(respReqId));
        respReqId = sylar::byteswapOnLittleEndian(respReqId);
        totalSize -= sizeof(uint32_t);
    }

    std::string respStr;
    if(totalSize > 0) {
        respStr.resize(totalSize);
        if(ss.readFixSize(&respStr[0], totalSize) <= 0) {
            if(controller) {
                controller->SetFailed("read response body failed");
            }
            sock->close();
            return;
        }
    }

    if(!response->ParseFromString(respStr)) {
        if(controller) {
            controller->SetFailed("parse response failed");
        }
    }
    sock->close();
    SYLAR_LOG_INFO(g_rpclogger) << "rpc call completed: " << serviceName << "." << methodName;
}

// 多路复用模式: 通过 RpcStream(AsyncSocketStream) 发送 + request_id 匹配
static void callMethodMultiplexed(
        const MethodDescriptor* method, RpcController* controller,
        const Message* request, Message* response,
        RpcChannelPool* pool, const std::string& packet,
        const std::string& serviceName, const std::string& methodName,
        const std::string& etcdEndpoint, uint32_t requestId) {
    std::string method_path = "/" + serviceName + "/" + methodName;
    std::string ip;
    uint16_t port = 0;
    if(!discoverService(pool, etcdEndpoint, method_path, ip, port)) {
        if(controller) {
            controller->SetFailed(method_path + " not found in etcd");
        }
        return;
    }

    // 从连接池获取或创建 RpcStream
    auto stream = pool->acquireStream(ip, port);
    if(!stream) {
        if(controller) {
            controller->SetFailed("connect failed to " + ip + ":" + std::to_string(port));
        }
        return;
    }

    // 构造请求 Ctx
    auto ctx = std::make_shared<RpcStream::RpcCtx>();
    ctx->sn = requestId;
    ctx->response = response;
    ctx->fiber = Fiber::GetThis();
    ctx->scheduler = IOManager::GetThis();

    // 构造发送数据(请求包)
    struct SendCtxImpl : public AsyncSocketStream::SendCtx {
        std::string data;
        bool doSend(AsyncSocketStream::ptr s) override {
            int64_t rt = s->write(data.data(), data.size());
            return rt > 0;
        }
    };
    auto sendCtx = std::make_shared<SendCtxImpl>();
    sendCtx->data = packet;

    // 注册 Ctx 等待响应
    stream->addCtx(ctx);
    stream->enqueue(sendCtx);

    // 协程级阻塞: 等 doRead 收到匹配 request_id 的响应后 doRsp 唤醒
    Fiber::YieldToHold();

    // 恢复后检查结果
    if(ctx->result == AsyncSocketStream::TIMEOUT) {
        if(controller) {
            controller->SetFailed("rpc timeout");
        }
    } else if(ctx->result == AsyncSocketStream::IO_ERROR) {
        if(controller) {
            controller->SetFailed("io error");
        }
    }

    pool->releaseStream(ip, port, stream);
    SYLAR_LOG_INFO(g_rpclogger) << "rpc call completed(mux): " << serviceName << "." << methodName;
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

    // 序列化请求
    std::string argsStr;
    if(!request->SerializeToString(&argsStr)) {
        if(controller) {
            controller->SetFailed("serialize request failed");
        }
        return;
    }

    // 提取 traceId
    std::string traceId;
    auto* sctrl = dynamic_cast<RpcController*>(controller);
    if(sctrl && !sctrl->TraceId().empty()) {
        traceId = sctrl->TraceId();
    }

    // 生成 request_id
    static std::atomic<uint32_t> s_nextReqId{1};
    uint32_t requestId = s_nextReqId.fetch_add(1);

    // 构建请求包
    std::string packet = buildRequestPacket(serviceName, methodName, argsStr, traceId, requestId);

    SYLAR_LOG_DEBUG(g_rpclogger) << "rpc call: service=" << serviceName
        << " method=" << methodName << " reqId=" << requestId;

    if(m_pool) {
        // 多路复用模式
        callMethodMultiplexed(method, sctrl, request, response,
            m_pool, packet, serviceName, methodName, m_etcdEndpoint, requestId);
    } else {
        // 短连接模式(向后兼容)
        callMethodShort(method, sctrl, request, response,
            m_etcdEndpoint, packet, serviceName, methodName);
    }
}

} // namespace rpc
} // namespace sylar
