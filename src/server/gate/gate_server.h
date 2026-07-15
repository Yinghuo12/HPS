#ifndef __DDT_GATE_SERVER_H__
#define __DDT_GATE_SERVER_H__

#include <atomic>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>

#include "msg_id.h"
#include "rpc.pb.h"
#include "service_base.h"
#include "sylar/core/thread.h"
#include "sylar/net/tcp_server.h"
#include "sylar/rpc/rpc_channel.h"
#include "sylar/scheduler/iomanager.h"
#include "sylar/scheduler/timewheel.h"   // TimeWheel(心跳定时器迁移)

namespace ddt {

// 客户端会话(每 TCP 连接一个)
struct ClientSession {
    typedef std::shared_ptr<ClientSession> ptr;
    sylar::Socket::ptr sock;
    uint64_t accountId = 0;        // 0=未登录
    std::string name;
    std::string token;             // 登录 token(登出时删 Redis 用)
    Gender gender = GENDER_NONE;   // 0=未选择 1=男 2=女
    uint64_t lastRecvMs = 0;
    uint64_t gatewayId = 0;        // 本 gate 实例 ID(预留多 gate)

    // 发送队列: 保证同一 fd 同一时刻只有一个协程在 send。
    // 根因: handleClient 协程与 PushService RPC 协程(多个)并发向同一 sock 发数据时,
    // 若某次 send 遇 EAGAIN, hook 的 do_io 会 addEvent(fd, WRITE) 挂起当前协程;
    // 第二个协程对同 fd 再 send 也会 addEvent(WRITE), 触发 sylar 的
    // SYLAR_ASSERT(!(fd_ctx->events & event))(iomanager.cc:119) → abort() → gate 进程崩溃。
    // 入队后由按需启动的 drainAndSend 协程串行消费, 从根本上消除该竞态。
    sylar::Spinlock sendMutex;            // 仅保护 sendQueue/sendBusy(锁内纯内存操作, 不 yield)
    std::deque<std::string> sendQueue;    // 已组帧的完整包([4B len][2B msgid][pb])
    bool sendBusy = false;                // 是否有发送协程正在消费队列(去重启动)
};

// ============================================================
// GateServer: 客户端 TCP 入口。
// - 接受连接, 帧解析([4B len][2B msgid][pb])
// - 首包 LOGIN 校验 token(调 LoginService), 绑定 accountId
// - 按 msg_id 分发: 登录/注册 -> login; 房间/聊天/好友 -> lobby; 战斗 -> battle
// - 实现 PushService(供 battle/lobby 回调推送)
// ============================================================
class GateServer : public sylar::TcpServer, public PushService {
public:
    typedef std::shared_ptr<GateServer> ptr;

    // tw: 由 gate_main 创建并 start 的共享时间轮, 心跳检查周期定时器注册其上(O(1)调度)。
    GateServer(const ServiceConfig& cfg, const std::string& etcdEndpoint,
               sylar::TimeWheel::ptr tw);
    ~GateServer();

    void startHeartbeatCheck();

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
    // 串行消费 session 发送队列: 发空即退(非常驻)。
    // 保证同一 fd 至多一个协程在 do_io(WRITE), 消除并发 send 触发的 sylar ASSERT。
    static void drainAndSend(ClientSession::ptr s);
    void sendError(ClientSession::ptr s, int code, const std::string& msg);

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

    // RPC channel 缓存
    std::mutex m_chanMutex;
    std::shared_ptr<sylar::rpc::RpcChannel> m_lobbyChan;
    std::shared_ptr<sylar::rpc::RpcChannel> m_battleChan;
    std::shared_ptr<sylar::rpc::RpcChannel> m_loginChan;
    std::shared_ptr<sylar::rpc::RpcChannel> m_dataChan;
};

} // namespace ddt

#endif
