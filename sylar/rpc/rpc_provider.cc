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
        uint32_t reqId = rpcHeader.request_id();
        // 手动 Closure: 业务 impl 完成后调 done->Run() 发送响应。
        // (NewCallback 最多 2 参数, sendResponse 需要 3 参数)
        Socket::ptr clientSock = client;
        // 用 protobuf 的 NewCallback 配合静态中转函数
        struct ResponseClosure {
            RpcProvider* self;
            Socket::ptr sock;
            Message* response;
            uint32_t reqId;
            static void run(ResponseClosure* c) {
                c->self->sendResponse(c->sock, c->response, c->reqId);
                delete c;
            }
        };
        ResponseClosure* rc = new ResponseClosure{this, clientSock, response, reqId};
        Closure* done = google::protobuf::NewCallback(&ResponseClosure::run, rc);
        sylar::rpc::RpcController* ctrl = new sylar::rpc::RpcController();
        if(!traceId.empty()) {
            ctrl->SetTraceId(traceId);
        }
        service->CallMethod(methodDesc, ctrl, request, response, done);
        delete request;
        delete ctrl;

        // 长连接循环: 处理完不退出, 继续等下一个请求。
        // request_id 匹配解决了旧版数据流错位导致的 stack smashing。
    }
}

// 同步发送响应：[4-byte response_size][4-byte request_id][response body]，发完不关连接。
void RpcProvider::sendResponse(sylar::Socket::ptr sock, Message* response, uint32_t request_id) {
    std::string responseStr;
    if(response->SerializeToString(&responseStr)) {
        // 响应头: [4B size(不含自身) ][4B request_id][body]
        // size = sizeof(request_id) + body.size() = 4 + body.size()
        uint32_t totalSize = (uint32_t)(sizeof(uint32_t) + responseStr.size());
        uint32_t netSize = sylar::byteswapOnLittleEndian(totalSize);
        uint32_t netReqId = sylar::byteswapOnLittleEndian(request_id);
        std::string packet;
        packet.append(reinterpret_cast<char*>(&netSize), 4);
        packet.append(reinterpret_cast<char*>(&netReqId), 4);
        packet.append(responseStr);
        sock->send(packet.c_str(), packet.size());
    } else {
        SYLAR_LOG_ERROR(g_rpclogger) << "failed to serialize response";
    }
    delete response;
}

}   // namespace rpc
}   // namespace sylar
