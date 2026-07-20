// RPC 服务端 Provider：注册服务到 etcd + 监听端口分发 RPC 请求。
#include "rpc_provider.h"
#include "rpcheader.pb.h"
#include "rpc_controller.h"
#include "sylar/core/log.h"
#include "sylar/core/endian.h"

#include <google/protobuf/descriptor.h>

namespace sylar {

static sylar::Logger::ptr g_rpclogger = SYLAR_LOG_NAME("rpc");

namespace rpc {

using namespace google::protobuf;

RpcProvider::RpcProvider() {
}

// EtcdRegistrar 析构会撤销全部注册并停止 KeepAlive
RpcProvider::~RpcProvider() {
}

void RpcProvider::setEtcd(const std::string& endpoint, int ttl) {
    m_etcdEndpoint = endpoint;
    m_leaseTtl = ttl;
}

void RpcProvider::setListen(uint16_t port) {
    m_listenPort = port;
    if (m_advertise == "127.0.0.1:9000") {
        m_advertise = std::string("127.0.0.1:") + std::to_string(port);
    }
}

void RpcProvider::setAdvertise(const std::string& host_port) {
    m_advertise = host_port;
}

// 登记一个 protobuf Service：枚举其全部 method 入服务表。
void RpcProvider::notifyService(Service* service) {
    const ServiceDescriptor* serviceDesc = service->GetDescriptor();
    std::string serviceName = serviceDesc->name();
    int methodCnt = serviceDesc->method_count();

    ServiceInfo info;
    info.service = service;
    for (int i = 0; i < methodCnt; ++i) {
        const MethodDescriptor* methodDesc = serviceDesc->method(i);
        std::string methodName = methodDesc->name();
        info.methodMap.insert({methodName, methodDesc});
        SYLAR_LOG_INFO(g_rpclogger) << "register method: " << serviceName << "." << methodName;
    }
    m_serviceMap.insert({serviceName, info});
    SYLAR_LOG_INFO(g_rpclogger) << "publish service: " << serviceName << " methods=" << methodCnt;
}

// 启动服务：bind 端口 -> 注册服务到 etcd -> 进入 accept 循环。
void RpcProvider::run() {
    IPAddress::ptr addr = IPAddress::Create("0.0.0.0", m_listenPort);
    if (!bind(addr)) {
        SYLAR_LOG_FATAL(g_rpclogger) << "RPC bind failed on 0.0.0.0:" << m_listenPort;
        return;
    }

    // 注册服务到 etcd：键 "/{ServiceName}/{Method}"，值 "ip:port"，
    // 绑定租约（由 etcd::KeepAlive 自动续租，等价 ZK 临时节点）。
    m_registrar = std::make_shared<EtcdRegistrar>(m_etcdEndpoint);
    for (auto& service : m_serviceMap) {
        for (auto& method : service.second.methodMap) {
            std::string method_path = "/" + service.first + "/" + method.first;
            EtcdRegisterInfo info;
            info.key = method_path;
            info.value = m_advertise;
            info.ttl = m_leaseTtl;
            m_registrar->registerService(info);
        }
    }

    SYLAR_LOG_INFO(g_rpclogger) << "RpcProvider start service at 0.0.0.0:" << m_listenPort
                                << " advertise=" << m_advertise
                                << " (etcd=" << m_etcdEndpoint << ")";
    start();
}

// 处理单个客户端连接：读 header -> 解析 -> 反序列化请求 -> 调用业务方法 -> 发响应。
void RpcProvider::handleClient(sylar::Socket::ptr client) {
    SYLAR_LOG_INFO(g_rpclogger) << "RPC client connected: " << *client;

    sylar::SocketStream ss(client);
    while (true) {
        // 读 4 字节 header size
        uint32_t headerSize = 0;
        if (ss.readFixSize(&headerSize, sizeof(headerSize)) <= 0) {
            break;
        }
        headerSize = sylar::byteswapOnLittleEndian(headerSize);

        // 读 RPC header
        std::string rpcHeaderStr;
        rpcHeaderStr.resize(headerSize);
        if (ss.readFixSize(&rpcHeaderStr[0], headerSize) <= 0) {
            break;
        }

        // 解析 RPC header
        RpcHeader rpcHeader;
        std::string serviceName, methodName;
        uint32_t argsSize = 0;
        std::string traceId;   // §6 提取 traceId 供注入 controller
        if (rpcHeader.ParseFromString(rpcHeaderStr)) {
            serviceName = rpcHeader.service_name();
            methodName = rpcHeader.method_name();
            argsSize = rpcHeader.args_size();
            traceId = rpcHeader.trace_id();
        } else {
            SYLAR_LOG_ERROR(g_rpclogger) << "failed to parse rpc header";
            break;
        }

        if (traceId.empty()) {
            SYLAR_LOG_DEBUG(g_rpclogger) << "recv rpc: service=" << serviceName
                                         << " method=" << methodName << " args_size=" << argsSize;
        } else {
            SYLAR_LOG_INFO(g_rpclogger) << "[" << traceId << "] rpc in: service="
                                        << serviceName << " method=" << methodName
                                        << " args_size=" << argsSize;
        }

        // 读 args
        std::string argsStr;
        argsStr.resize(argsSize);
        if (argsSize > 0 && ss.readFixSize(&argsStr[0], argsSize) <= 0) {
            break;
        }

        // 查找 service 和 method
        auto it = m_serviceMap.find(serviceName);
        if (it == m_serviceMap.end()) {
            SYLAR_LOG_ERROR(g_rpclogger) << "service not found: " << serviceName;
            break;
        }
        auto mtIt = it->second.methodMap.find(methodName);
        if (mtIt == it->second.methodMap.end()) {
            SYLAR_LOG_ERROR(g_rpclogger) << "method not found: " << serviceName << "." << methodName;
            break;
        }

        Service* service = it->second.service;
        const MethodDescriptor* methodDesc = mtIt->second;

        // 反序列化请求
        Message* request = service->GetRequestPrototype(methodDesc).New();
        if (!request->ParseFromString(argsStr)) {
            SYLAR_LOG_ERROR(g_rpclogger) << "request parse error";
            delete request;
            break;
        }

        // 调用业务方法
        Message* response = service->GetResponsePrototype(methodDesc).New();
        Closure* done = NewCallback<RpcProvider, sylar::Socket::ptr, Message*>(
            this, &RpcProvider::sendResponse, client, response);
        // §6: 构造 RpcController 实例，把 traceId 注入，供 impl 端读取用于日志关联。
        // CallMethod 同步执行，done 内同步 sendResponse，返回后立即 delete。
        sylar::rpc::RpcController* ctrl = new sylar::rpc::RpcController();
        if (!traceId.empty()) {
            ctrl->SetTraceId(traceId);
        }
        service->CallMethod(methodDesc, ctrl, request, response, done);
        delete request;
        delete ctrl;

        // 一连接一请求：处理完即退出循环，由 TcpServer 回收连接。
        // (曾尝试 keep-alive 循环复用连接，但高频推送下出现 stack smashing，
        //  根因是连接复用时数据流错位风险；回退为短连接最稳妥。)
        break;
    }
}

// 同步发送响应：[4-byte size][response body]，发完即关连接。
void RpcProvider::sendResponse(sylar::Socket::ptr sock, Message* response) {
    std::string responseStr;
    if (response->SerializeToString(&responseStr)) {
        uint32_t size = sylar::byteswapOnLittleEndian((uint32_t)responseStr.size());
        std::string packet;
        packet.append(reinterpret_cast<char*>(&size), 4);
        packet.append(responseStr);
        sock->send(packet.c_str(), packet.size());
        SYLAR_LOG_INFO(g_rpclogger) << "rpc response sent, size=" << responseStr.size();
    } else {
        SYLAR_LOG_ERROR(g_rpclogger) << "failed to serialize response";
    }
    delete response;
    sock->close();
}

}   // namespace rpc
}   // namespace sylar
