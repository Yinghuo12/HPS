// RPC 客户端 Channel：实现 protobuf RpcChannel 接口，序列化请求、服务发现、收发响应。
#include "rpc_channel.h"
#include "rpcheader.pb.h"
#include "rpc_controller.h"
#include "sylar/core/log.h"
#include "sylar/net/socket.h"
#include "sylar/net/socket_stream.h"
#include "sylar/net/address.h"
#include "sylar/core/endian.h"
#include "sylar/rpc/etcd_client.h"
#include "sylar/rpc/rpc_channel_pool.h"

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

// RPC 调用入口：序列化请求 -> 服务发现 -> 取连接 -> 发送 -> 读响应 -> 反序列化。
void RpcChannel::CallMethod(
        const MethodDescriptor* method,
        ::google::protobuf::RpcController* controller,
        const Message* request,
        Message* response,
        Closure* done) {

    const ServiceDescriptor* serviceDesc = method->service();
    std::string serviceName = serviceDesc->name();
    std::string methodName = method->name();

    // 序列化请求参数
    std::string argsStr;
    if (!request->SerializeToString(&argsStr)) {
        if (controller) {
            controller->SetFailed("serialize request failed");
        }
        return;
    }

    // 构造 RPC header
    uint32_t argsSize = argsStr.size();
    sylar::rpc::RpcHeader rpcHeader;
    rpcHeader.set_service_name(serviceName);
    rpcHeader.set_method_name(methodName);
    rpcHeader.set_args_size(argsSize);
    // §6: 从 controller 透传 traceId（gate 端 SetTraceId 注入，中间服务继续传递）。
    // dynamic_cast 因为入参 controller 类型是 google 基类 RpcController。
    if (controller) {
        auto* sctrl = dynamic_cast<sylar::rpc::RpcController*>(controller);
        if (sctrl && !sctrl->TraceId().empty()) {
            rpcHeader.set_trace_id(sctrl->TraceId());
        }
    }

    std::string rpcHeaderStr;
    if (!rpcHeader.SerializeToString(&rpcHeaderStr)) {
        if (controller) {
            controller->SetFailed("serialize rpc header failed");
        }
        return;
    }

    uint32_t headerSize = rpcHeaderStr.size();

    // 拼接报文：[4-byte header_size][rpc_header][args]
    std::string packet;
    uint32_t netHeaderSize = sylar::byteswapOnLittleEndian(headerSize);
    packet.append(reinterpret_cast<char*>(&netHeaderSize), 4);
    packet.append(rpcHeaderStr);
    packet.append(argsStr);

    SYLAR_LOG_DEBUG(g_rpclogger) << "rpc call: service=" << serviceName
                                 << " method=" << methodName << " header_size=" << headerSize
                                 << " args_size=" << argsSize;

    // 服务发现：优先用连接池内置的 TTL 缓存（miss/过期才查 etcd），
    // 消除每次 RPC 都建 etcd gRPC 连接。
    std::string method_path = "/" + serviceName + "/" + methodName;
    std::string ip;
    uint16_t port = 0;
    bool found = false;
    if (m_pool && m_pool->getDiscovery(method_path, ip, port)) {
        found = true;
    } else {
        EtcdClient cli(m_etcdEndpoint);
        EtcdClient::KV kv;
        if (cli.get(method_path, kv) && !kv.value.empty()) {
            int idx = kv.value.find(':');
            if (idx != -1) {
                ip = kv.value.substr(0, idx);
                port = (uint16_t)atoi(kv.value.substr(idx + 1).c_str());
                found = true;
                if (m_pool) {
                    m_pool->putDiscovery(method_path, ip, port);
                }
            }
        }
    }
    if (!found) {
        if (controller) {
            controller->SetFailed(method_path + " not found in etcd");
        }
        return;
    }

    SYLAR_LOG_INFO(g_rpclogger) << "rpc connecting to " << ip << ":" << port;

    // 连接获取：有连接池则从池借（复用 keep-alive 连接）；
    // 无池则新建（短连接，向后兼容）。
    Socket::ptr sock;
    RpcChannelPool::Guard* guard = nullptr;
    if (m_pool) {
        sock = m_pool->acquire(ip, port);
        if (!sock) {
            if (controller) {
                controller->SetFailed("connect failed to " + ip + ":" + std::to_string(port));
            }
            return;
        }
        // RAII 归还：函数返回（含异常路径）自动归还连接到池
        guard = new RpcChannelPool::Guard(m_pool, ip, port, sock);
    } else {
        IPAddress::ptr addr = IPAddress::Create(ip.c_str(), port);
        sock = Socket::CreateTCP(addr);
        if (!sock->connect(addr)) {
            if (controller) {
                controller->SetFailed("connect failed to " + ip + ":" + std::to_string(port));
            }
            return;
        }
    }

    // 发送请求
    sock->send(packet.c_str(), packet.size());

    // 读响应：[4-byte size][response]
    sylar::SocketStream ss(sock);
    uint32_t respSize = 0;
    if (ss.readFixSize(&respSize, sizeof(respSize)) <= 0) {
        if (controller) {
            controller->SetFailed("read response size failed");
        }
        // 读失败：连接已坏，从池中淘汰（Guard release 后丢弃，不回池）
        if (guard) {
            guard->release();
            sock->close();
            delete guard;
        } else {
            sock->close();
        }
        return;
    }
    respSize = sylar::byteswapOnLittleEndian(respSize);

    // 边界保护：respSize 异常大说明连接数据错位（keep-alive 复用时残留/交错），
    // 直接判失败并淘汰该连接，避免 resize 巨值导致崩溃。
    if (respSize > 64u * 1024 * 1024) {
        SYLAR_LOG_ERROR(g_rpclogger) << "rpc response size abnormal: " << respSize
                                     << ", connection data misaligned, discarding";
        if (controller) {
            controller->SetFailed("response size abnormal");
        }
        if (guard) {
            guard->release();
            sock->close();
            delete guard;
        } else {
            sock->close();
        }
        return;
    }

    std::string respStr;
    respStr.resize(respSize);
    // BUG-5 修复：proto3 全默认值消息序列化为 0 字节（如 ResultResp{SUCCESS=0,msg=""}），
    // readFixSize(buf,0) 返回 0 会误判失败。0 字节响应是合法空消息，直接进反序列化。
    if (respSize > 0 && ss.readFixSize(&respStr[0], respSize) <= 0) {
        if (controller) {
            controller->SetFailed("read response body failed");
        }
        if (guard) {
            guard->release();
            sock->close();
            delete guard;
        } else {
            sock->close();
        }
        return;
    }

    // 反序列化响应
    if (!response->ParseFromString(respStr)) {
        if (controller) {
            controller->SetFailed("parse response failed");
        }
    }

    SYLAR_LOG_INFO(g_rpclogger) << "rpc call completed: " << serviceName << "." << methodName;
    // 连接复用：有池则归还（Guard 析构），无池则 close（短连接）
    if (guard) {
        delete guard;   // Guard 析构 → release（健康则回池）
    } else {
        sock->close();
    }
}

}   // namespace rpc
}   // namespace sylar
