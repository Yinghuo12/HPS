#ifndef __DDT_GATE_SERVER_H__
#define __DDT_GATE_SERVER_H__

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "msg_id.h"
#include "redis_pool.h"
#include "rpc.pb.h"
#include "service_base.h"
#include "sylar/core/thread.h"
#include "sylar/net/async_socket_stream.h"
#include "sylar/net/tcp_server.h"
#include "sylar/rpc/rpc_channel.h"
#include "sylar/rpc/rpc_channel_pool.h"
#include "sylar/rpc/rpc_controller.h"
#include "sylar/scheduler/iomanager.h"
#include "sylar/scheduler/timewheel.h"

namespace ddt {

// 仅发送的异步流(gate 不需要 doRecv, 自己在 handleClient 里读)
class GateSendStream : public sylar::AsyncSocketStream {
public:
    typedef std::shared_ptr<GateSendStream> ptr;
    GateSendStream(sylar::Socket::ptr sock)
        : AsyncSocketStream(sock, false) {
    }
    // gate 自己读帧, doRecv 不使用
    Ctx::ptr doRecv() override { return nullptr; }
};

// 客户端会话(每 TCP 连接一个)
struct ClientSession {
    typedef std::shared_ptr<ClientSession> ptr;
    sylar::Socket::ptr sock;
    GateSendStream::ptr stream;     // 异步发送流(替代手搓 sendQueue)
    uint64_t accountId = 0;
    std::string name;
    std::string token;
    Gender gender = GENDER_NONE;
    uint64_t lastRecvMs = 0;
    uint64_t gatewayId = 0;
};

// 客户端 TCP 入口。处理帧解析、token 校验、msg_id 分发, 并实现 PushService 供 battle/lobby 推送。
class GateServer : public sylar::TcpServer, public PushService {
public:
    typedef std::shared_ptr<GateServer> ptr;

    // tw: 由 gate_main 创建并 start 的共享时间轮, 心跳检查周期定时器注册其上(O(1)调度)。
    GateServer(const ServiceConfig& cfg, const std::string& etcdEndpoint,
               sylar::TimeWheel::ptr tw);
    ~GateServer();

    void startHeartbeatCheck();
    // 设置 Redis 地址(用于订阅世界聊天)。
    void setRedis(const std::string& host, int port) { m_redisHost = host; m_redisPort = port; }
    // 启动 Redis 订阅线程(chat:world), 由 gate_main 在 bind 后调一次。
    void startWorldChatSubscriber();
    // 设置 RPC 连接池(4 个下游服务统一走多路复用)。
    void setRpcPool(std::shared_ptr<sylar::rpc::RpcChannelPool> pool) { m_rpcPool = pool; }

protected:
    // TcpServer: 每连接一个光纤
    void handleClient(sylar::Socket::ptr client) override;

    // PushService 实现(供 lobby/battle 调用)
    void NotifyClient(::google::protobuf::RpcController*,
                      const ::ddt::NotifyReq* req, ::ddt::ResultResp* resp,
                      ::google::protobuf::Closure* done) override;
    void NotifyClients(::google::protobuf::RpcController*,
                       const ::ddt::NotifyManyReq* req, ::ddt::ResultResp* resp,
                       ::google::protobuf::Closure* done) override;
    void NotifyAllOnline(::google::protobuf::RpcController*,
                         const ::ddt::NotifyReq* req, ::ddt::ResultResp* resp,
                         ::google::protobuf::Closure* done) override;

private:
    // 发一条帧消息给指定 session(入队, 按需启动发送协程)
    void sendToSession(ClientSession::ptr s, uint16_t msgId, const std::string& payload);
    void sendError(ClientSession::ptr s, int code, const std::string& msg);

    // §6 构造带 traceId 的 RPC controller(22 个 onXxx handler 统一入口)。
    // traceId 格式: g<gatewayId>-a<accountId>-m<msgId>-<seq>。
    // 中间服务经 RpcHeader.trace_id 继续透传(login/lobby/battle/data)。
    std::shared_ptr<sylar::rpc::RpcController> newCtrl(ClientSession::ptr s, uint16_t msgId);
    std::atomic<uint64_t> m_rpcSeq{0};

    // ---- msg_id handlers ----
    void onLogin(ClientSession::ptr s, const std::string& body);
    void onRegister(ClientSession::ptr s, const std::string& body);
    void onLogout(ClientSession::ptr s, const std::string& body);
    void onSetGender(ClientSession::ptr s, const std::string& body);
    void onHeartbeat(ClientSession::ptr s, const std::string& body);
    void onRoomList(ClientSession::ptr s, const std::string& body);
    void onCreateRoom(ClientSession::ptr s, const std::string& body);
    void onJoinRoom(ClientSession::ptr s, const std::string& body);
    void onLeaveRoom(ClientSession::ptr s, const std::string& body);
    void onReady(ClientSession::ptr s, const std::string& body);
    void onSwitchTeam(ClientSession::ptr s, const std::string& body);
    void onSwitchWeapon(ClientSession::ptr s, const std::string& body);
    void onChat(ClientSession::ptr s, const std::string& body);
    void onChatHistory(ClientSession::ptr s, const std::string& body);
    void onPrivateChat(ClientSession::ptr s, const std::string& body);
    void onFriendAdd(ClientSession::ptr s, const std::string& body);
    void onFriendList(ClientSession::ptr s, const std::string& body);
    void onShoot(ClientSession::ptr s, const std::string& body);
    void onMove(ClientSession::ptr s, const std::string& body);
    void onPass(ClientSession::ptr s, const std::string& body);
    void onAimBegin(ClientSession::ptr s, const std::string& body);   // 蓄力开始→重置回合计时

    // session 管理
    void addSession(sylar::Socket::ptr sock, ClientSession::ptr s);
    void delSession(sylar::Socket::ptr sock);
    ClientSession::ptr sessionByAccount(uint64_t accountId);
    ClientSession::ptr sessionBySock(sylar::Socket::ptr sock);
    // 顶号: 给旧 session 发 KICK_NOTIFY 后关闭其连接(新登录时调)
    void kickExistingSession(uint64_t accountId);

    // §15 注册式分发: 取代原 handleClient 的 21 个 switch-case。
    // 已登录(需 accountId != 0)的 handler 注册到 m_handlers;
    // 预登录(无需鉴权, 如 LOGIN/REGISTER/HEARTBEAT)的 handler 直接在 handleClient 用 static map 查。
    // 新增 msg_id 仅加一行注册。
    typedef std::function<void(ClientSession::ptr, const std::string&)> Handler;
    void registerHandlers();
    std::unordered_map<uint16_t, Handler> m_handlers;
    std::unordered_set<uint16_t> m_preAuthMsgs = {
        MSG_LOGIN, MSG_REGISTER, MSG_HEARTBEAT, MSG_LOGOUT, MSG_SET_GENDER
    };

    // 转发到 lobby/battle/login 的便捷
    std::shared_ptr<sylar::rpc::RpcChannel> lobbyChannel();
    std::shared_ptr<sylar::rpc::RpcChannel> battleChannel();
    std::shared_ptr<sylar::rpc::RpcChannel> loginChannel();
    std::shared_ptr<sylar::rpc::RpcChannel> dataChannel();

    const ServiceConfig& m_cfg;
    std::string m_etcdEndpoint;
    uint64_t m_gatewayId;
    sylar::TimeWheel::ptr m_tw;   // 心跳检查用的共享时间轮(由 gate_main 注入)

    typedef sylar::RWMutex MutexType;
    MutexType m_sessionMutex;
    std::map<uint64_t /*accountId*/, ClientSession::ptr> m_accountToSession;
    std::map<sylar::Socket*, ClientSession::ptr> m_sockToSession;

    // RPC channel 池: 4 个下游服务(login/lobby/battle/data)统一走多路复用长连接。
    // 替代原 4 个独立短连接 channel(每次 RPC 建 TCP→send→close)。
    std::shared_ptr<sylar::rpc::RpcChannelPool> m_rpcPool;

    // Redis 订阅(世界聊天广播)。独立线程, 长期阻塞 redisGetReply。
    std::shared_ptr<Subscriber> m_subscriber;
    std::shared_ptr<std::thread> m_subThread;
    std::string m_redisHost;
    int m_redisPort = 6379;
};

} // namespace ddt

#endif
