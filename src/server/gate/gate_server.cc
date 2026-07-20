#include "gate_server.h"

#include <chrono>

#include "frame.h"
#include "gate.pb.h"
#include "sylar/core/log.h"
#include "sylar/core/sys_util.h"
#include "sylar/net/socket_stream.h"
#include "sylar/rpc/rpc_channel.h"
#include "sylar/rpc/rpc_controller.h"
#include "sylar/scheduler/iomanager.h"

namespace ddt {

static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

GateServer::GateServer(const ServiceConfig& cfg, const std::string& etcdEndpoint,
                       sylar::TimeWheel::ptr tw)
    : TcpServer(sylar::IOManager::GetThis(), sylar::IOManager::GetThis())
    , m_cfg(cfg)
    , m_etcdEndpoint(etcdEndpoint)
    , m_gatewayId(1)
    , m_tw(tw) {
    registerHandlers();   // §15
}

GateServer::~GateServer() {
}

// §6 构造带 traceId 的 controller。22 个 onXxx handler 统一用此入口。
std::shared_ptr<sylar::rpc::RpcController> GateServer::newCtrl(
        ClientSession::ptr s, uint16_t msgId) {
    auto ctrl = std::make_shared<sylar::rpc::RpcController>();
    uint64_t seq = m_rpcSeq.fetch_add(1);
    std::string tid = "g" + std::to_string(m_gatewayId)
                    + "-a" + std::to_string(s ? s->accountId : 0)
                    + "-m" + std::to_string(msgId)
                    + "-" + std::to_string(seq);
    ctrl->SetTraceId(tid);
    return ctrl;
}

// §15 注册已登录 handler(预登录的 LOGIN/REGISTER/HEARTBEAT/LOGOUT/SET_GENDER
//   在 handleClient 内用 static map 查, 不在此注册)。
void GateServer::registerHandlers() {
    m_handlers[MSG_ROOM_LIST]     = [this](ClientSession::ptr s, const std::string& b){ onRoomList(s, b); };
    m_handlers[MSG_CREATE_ROOM]   = [this](ClientSession::ptr s, const std::string& b){ onCreateRoom(s, b); };
    m_handlers[MSG_JOIN_ROOM]     = [this](ClientSession::ptr s, const std::string& b){ onJoinRoom(s, b); };
    m_handlers[MSG_LEAVE_ROOM]    = [this](ClientSession::ptr s, const std::string& b){ onLeaveRoom(s, b); };
    m_handlers[MSG_READY]         = [this](ClientSession::ptr s, const std::string& b){ onReady(s, b); };
    m_handlers[MSG_SWITCH_TEAM]   = [this](ClientSession::ptr s, const std::string& b){ onSwitchTeam(s, b); };
    m_handlers[MSG_SWITCH_WEAPON] = [this](ClientSession::ptr s, const std::string& b){ onSwitchWeapon(s, b); };
    m_handlers[MSG_CHAT]          = [this](ClientSession::ptr s, const std::string& b){ onChat(s, b); };
    m_handlers[MSG_CHAT_HISTORY]  = [this](ClientSession::ptr s, const std::string& b){ onChatHistory(s, b); };
    m_handlers[MSG_PRIVATE_CHAT]  = [this](ClientSession::ptr s, const std::string& b){ onPrivateChat(s, b); };
    m_handlers[MSG_FRIEND_ADD]    = [this](ClientSession::ptr s, const std::string& b){ onFriendAdd(s, b); };
    m_handlers[MSG_FRIEND_LIST]   = [this](ClientSession::ptr s, const std::string& b){ onFriendList(s, b); };
    m_handlers[MSG_SHOOT]         = [this](ClientSession::ptr s, const std::string& b){ onShoot(s, b); };
    m_handlers[MSG_MOVE]          = [this](ClientSession::ptr s, const std::string& b){ onMove(s, b); };
    m_handlers[MSG_PASS]          = [this](ClientSession::ptr s, const std::string& b){ onPass(s, b); };
    m_handlers[MSG_AIM_BEGIN]     = [this](ClientSession::ptr s, const std::string& b){ onAimBegin(s, b); };
}

// ---- session 管理 ----
void GateServer::addSession(sylar::Socket::ptr sock, ClientSession::ptr s) {
    MutexType::WriteLock lk(m_sessionMutex);
    m_sockToSession[sock.get()] = s;
}

void GateServer::delSession(sylar::Socket::ptr sock) {
    ClientSession::ptr s;
    {
        MutexType::WriteLock lk(m_sessionMutex);
        auto it = m_sockToSession.find(sock.get());
        if(it != m_sockToSession.end()) {
            s = it->second;
            m_sockToSession.erase(it);
        }
    }
    if(s && s->accountId) {
        {
            MutexType::WriteLock lk(m_sessionMutex);
            // 安全删除: 只在 map 里存的就是当前 session 时才删
            // (避免顶号场景下旧连接断开误删新连接的索引)
            auto it = m_accountToSession.find(s->accountId);
            if(it != m_accountToSession.end() && it->second == s) {
                m_accountToSession.erase(it);
            }
        }
        // 断线清理异步化: LeaveRoom + LeaveBattle 两个 RPC 投到独立协程执行,
        // 不阻塞 handleClient 协程(原同步执行卡住断线协程占 1MB 栈, 大规模掉线 OOM)。
        // 索引已在上面锁内删完, 这里只做 RPC 通知; s/self 按值捕获保活。
        auto self = std::static_pointer_cast<GateServer>(shared_from_this());
        auto sess = s;
        sylar::IOManager::GetThis()->schedule([self, sess]() {
            uint64_t accountId = sess->accountId;
            // 通知 lobby 把该玩家踢出房间(否则房间永不销毁)
            try {
                auto ch = self->lobbyChannel();
                ddt::LobbyService::Stub stub(ch.get());
                auto ctrl = self->newCtrl(sess, MSG_LEAVE_ROOM);
                LeaveRoomRpcReq req;
                req.set_account_id(accountId);
                LeaveRoomRpcResp resp;
                stub.LeaveRoom(ctrl.get(), &req, &resp, nullptr);
                if(ctrl->Failed()) {
                    SYLAR_LOG_WARN(g_logger) << "gate: disconnect cleanup LeaveRoom fail account=" << accountId
                        << " err=" << ctrl->ErrorText();
                }
            } catch(const std::exception& e) {
                SYLAR_LOG_WARN(g_logger) << "gate: disconnect cleanup LeaveRoom exception: " << e.what();
            }
            // 如果在战斗中, 也通知 battle LeaveBattle
            try {
                auto bch = self->battleChannel();
                ddt::BattleService::Stub bstub(bch.get());
                auto bctrl = self->newCtrl(sess, MSG_LEAVE_ROOM);
                LeaveBattleRpcReq breq;
                breq.set_account_id(accountId);
                ResultResp bresp;
                bstub.LeaveBattle(bctrl.get(), &breq, &bresp, nullptr);
            } catch(const std::exception& e) {
                SYLAR_LOG_WARN(g_logger) << "gate: disconnect cleanup LeaveBattle exception: " << e.what();
            }
            // 上报下线到 Redis(经 data 服务 SREM online:players)
            try {
                auto dch = self->dataChannel();
                ddt::DataService::Stub dstub(dch.get());
                sylar::rpc::RpcController dctrl;
                ddt::IdReq dreq;
                dreq.set_account_id(accountId);
                ddt::ResultResp dresp;
                dstub.SetOffline(&dctrl, &dreq, &dresp, nullptr);
            } catch(const std::exception& e) {
                SYLAR_LOG_WARN(g_logger) << "gate: disconnect cleanup SetOffline exception: " << e.what();
            }
        });
    }
}

ClientSession::ptr GateServer::sessionByAccount(uint64_t accountId) {
    MutexType::ReadLock lk(m_sessionMutex);
    auto it = m_accountToSession.find(accountId);
    return it == m_accountToSession.end() ? nullptr : it->second;
}

void GateServer::kickExistingSession(uint64_t accountId) {
    ClientSession::ptr old;
    {
        MutexType::WriteLock lk(m_sessionMutex);
        auto it = m_accountToSession.find(accountId);
        if(it != m_accountToSession.end()) {
            old = it->second;
            m_accountToSession.erase(it);   // 先摘索引, 再踢
        }
    }
    if(old) {
        // 发顶号通知后强制关闭旧连接
        KickNotify n;
        n.set_code(409);
        n.set_msg("账号在别处登录");
        std::string payload;
        n.SerializeToString(&payload);
        sendToSession(old, MSG_KICK_NOTIFY, payload);
        if(old->sock) old->sock->close();
        SYLAR_LOG_WARN(g_logger) << "gate: kick old session account=" << accountId;
    }
}

ClientSession::ptr GateServer::sessionBySock(sylar::Socket::ptr sock) {
    MutexType::ReadLock lk(m_sessionMutex);
    auto it = m_sockToSession.find(sock.get());
    return it == m_sockToSession.end() ? nullptr : it->second;
}

void GateServer::sendToSession(ClientSession::ptr s, uint16_t msgId, const std::string& payload) {
    if(!s || !s->sock) return;
    std::string pkt = Frame::encode(msgId, payload);
    // 入队: 同一 fd 的所有发送(handleClient 回包 + PushService 推送)都汇入此队列,
    // 由按需启动的 drainAndSend 单协程串行消费, 杜绝并发 send。
    bool needStart = false;
    {
        sylar::Spinlock::Lock lk(s->sendMutex);
        s->sendQueue.push_back(std::move(pkt));
        if(!s->sendBusy) {
            s->sendBusy = true;   // 标记已有发送协程, 后续入队不再重复启动
            needStart = true;
        }
    }
    if(needStart) {
        // 按需调度一个发送协程(发空即退, 非常驻)。
        // 以值捕获 shared_ptr, 保证 drainAndSend 运行期间 session 不被析构。
        sylar::IOManager::GetThis()->schedule([s]() {
            GateServer::drainAndSend(s);
        });
    }
}

// 串行消费 session 发送队列。
// 任意时刻同一 fd 至多一个本协程在跑(sendBusy 去重), 故 hook 的 do_io(WRITE)
// 即便因 EAGAIN 挂起, 也不会有第二个协程对同 fd addEvent(WRITE), sylar ASSERT 不会再触发。
void GateServer::drainAndSend(ClientSession::ptr s) {
    while(true) {
        std::string pkt;
        {
            sylar::Spinlock::Lock lk(s->sendMutex);
            if(s->sendQueue.empty()) {
                s->sendBusy = false;   // 发空, 允许下次 sendToSession 重新启动本协程
                return;
            }
            pkt = std::move(s->sendQueue.front());
            s->sendQueue.pop_front();
        }
        // send 在锁外执行(hook 的 do_io 可能 yield, 锁内不可持有太久)。
        // 若连接已断/出错返回 <=0: readFixSize 会清理 session, 这里直接退出即可。
        int64_t rt = s->sock->send(pkt.data(), pkt.size());
        if(rt <= 0) return;
    }
}

void GateServer::sendError(ClientSession::ptr s, int code, const std::string& msg) {
    ErrorNotify n;
    n.set_code(code);
    n.set_msg(msg);
    std::string payload;
    n.SerializeToString(&payload);
    sendToSession(s, MSG_ERROR, payload);
}

// ---- RPC channel ----
// 短连接模式(无连接池): 每次 CallMethod 新建 socket+connect+close。
// 连接池在 sylar hook 模型下存在 fd 复用竞态, 已回退。
std::shared_ptr<sylar::rpc::RpcChannel> GateServer::lobbyChannel() {
    std::lock_guard<std::mutex> lk(m_chanMutex);
    if(!m_lobbyChan) m_lobbyChan = std::make_shared<sylar::rpc::RpcChannel>(m_etcdEndpoint);
    return m_lobbyChan;
}

std::shared_ptr<sylar::rpc::RpcChannel> GateServer::battleChannel() {
    std::lock_guard<std::mutex> lk(m_chanMutex);
    if(!m_battleChan) m_battleChan = std::make_shared<sylar::rpc::RpcChannel>(m_etcdEndpoint);
    return m_battleChan;
}

std::shared_ptr<sylar::rpc::RpcChannel> GateServer::loginChannel() {
    std::lock_guard<std::mutex> lk(m_chanMutex);
    if(!m_loginChan) m_loginChan = std::make_shared<sylar::rpc::RpcChannel>(m_etcdEndpoint);
    return m_loginChan;
}

std::shared_ptr<sylar::rpc::RpcChannel> GateServer::dataChannel() {
    std::lock_guard<std::mutex> lk(m_chanMutex);
    if(!m_dataChan) m_dataChan = std::make_shared<sylar::rpc::RpcChannel>(m_etcdEndpoint);
    return m_dataChan;
}

// ---- 主连接处理: 帧读取 + 分发 ----
void GateServer::handleClient(sylar::Socket::ptr client) {
    SYLAR_LOG_INFO(g_logger) << "gate: client connected " << client;
    auto sess = std::make_shared<ClientSession>();
    sess->sock = client;
    sess->lastRecvMs = sylar::GetCurrentMS();
    addSession(client, sess);

    sylar::SocketStream ss(client);
    while(true) {
        // 读 4 字节 length
        uint32_t lengthNet = 0;
        if(ss.readFixSize(&lengthNet, 4) <= 0) break;
        uint32_t length = ((lengthNet & 0xFF) << 24) | ((lengthNet & 0xFF00) << 8)
                        | ((lengthNet & 0xFF0000) >> 8) | ((lengthNet & 0xFF000000) >> 24);
        if(length < 2 || length > 16 * 1024 * 1024) {
            SYLAR_LOG_WARN(g_logger) << "gate: bad frame len " << length;
            break;
        }
        // 读 body(length 字节)
        std::string body(length, '\0');
        if(ss.readFixSize(&body[0], length) <= 0) break;

        uint16_t msgId = 0;
        std::string payload;
        if(!Frame::decode(body, msgId, payload)) break;

        sess->lastRecvMs = sylar::GetCurrentMS();

        // 分发
        try {
            // §15 注册式分发: 预登录白名单 + 已登录 handler 表。
            if(m_preAuthMsgs.count(msgId)) {
                // 预登录 handler(无需鉴权), static const 表避免每次构造
                static const std::unordered_map<uint16_t, Handler> kPreAuth = {
                    {MSG_LOGIN,      [this](ClientSession::ptr s, const std::string& b){ onLogin(s, b); }},
                    {MSG_REGISTER,   [this](ClientSession::ptr s, const std::string& b){ onRegister(s, b); }},
                    {MSG_HEARTBEAT,  [this](ClientSession::ptr s, const std::string& b){ onHeartbeat(s, b); }},
                    {MSG_LOGOUT,     [this](ClientSession::ptr s, const std::string& b){ onLogout(s, b); }},
                    {MSG_SET_GENDER, [this](ClientSession::ptr s, const std::string& b){ onSetGender(s, b); }},
                };
                auto it = kPreAuth.find(msgId);
                if(it != kPreAuth.end()) {
                    it->second(sess, payload);
                } else {
                    sendError(sess, 401, "not logged in");
                }
            } else {
                // 其余要求 accountId != 0
                if(sess->accountId == 0) {
                    sendError(sess, 401, "not logged in");
                } else {
                    auto it = m_handlers.find(msgId);
                    if(it != m_handlers.end()) {
                        it->second(sess, payload);
                    } else {
                        SYLAR_LOG_WARN(g_logger) << "gate: unknown msg_id=" << msgId;
                    }
                }
            }
        } catch(const std::exception& e) {
            SYLAR_LOG_ERROR(g_logger) << "gate: handle msg_id=" << msgId << " exception: " << e.what();
        }
    }
    SYLAR_LOG_INFO(g_logger) << "gate: client disconnected " << client;
    delSession(client);
}

// ---- 登录/注册 ----
void GateServer::onLogin(ClientSession::ptr s, const std::string& body) {
    LoginReq req;
    if(!req.ParseFromString(body)) {
        sendError(s, 400, "bad login req");
        return;
    }
    auto ch = loginChannel();
    ddt::LoginService::Stub stub(ch.get());

    // 两种登录方式:
    // 1. token 登录(req.token 非空, 重连场景): 直接 ValidateToken
    // 2. 账号密码直登(req.token 为空, 首次登录): 先调 Login(name,pwd) 换 token, 再 ValidateToken
    std::string token = req.token();
    if(token.empty() && !req.name().empty()) {
        auto ctrl0 = newCtrl(s, MSG_LOGIN);
        LoginRpcReq lreq;
        lreq.set_name(req.name());
        lreq.set_password(req.password());
        LoginRpcResp lresp;
        stub.Login(ctrl0.get(), &lreq, &lresp, nullptr);
        if(ctrl0->Failed() || lresp.result() != SUCCESS) {
            LoginResp resp;
            resp.set_result(AUTH_FAIL);
            resp.set_msg(ctrl0->Failed() ? ctrl0->ErrorText() :
                         (lresp.msg().empty() ? "login fail" : lresp.msg()));
            std::string payload;
            resp.SerializeToString(&payload);
            sendToSession(s, MSG_LOGIN_RESP, payload);
            return;
        }
        token = lresp.token();
    }

    auto ctrl = newCtrl(s, MSG_LOGIN);
    ValidateTokenReq vreq;
    vreq.set_token(token);
    ValidateTokenResp vresp;
    stub.ValidateToken(ctrl.get(), &vreq, &vresp, nullptr);
    LoginResp resp;
    if(ctrl->Failed() || vresp.result() != SUCCESS) {
        resp.set_result(AUTH_FAIL);
        resp.set_msg(ctrl->Failed() ? ctrl->ErrorText() : "invalid token");
    } else {
        s->accountId = vresp.account_id();
        s->name = vresp.name();
        s->token = token;
        s->gatewayId = m_gatewayId;
        // 顶号: 同账号新登录时踢掉旧连接(发 KICK_NOTIFY + close)
        kickExistingSession(s->accountId);
        {
            MutexType::WriteLock lk(m_sessionMutex);
            m_accountToSession[s->accountId] = s;
        }
        resp.set_result(SUCCESS);
        resp.set_account_id(s->accountId);
        resp.set_name(s->name);
        // 查 gender(决定客户端是否弹角色选择界面)
        auto dch = dataChannel();
        ddt::DataService::Stub dstub(dch.get());
        auto dctrl = newCtrl(s, MSG_LOGIN);
        ddt::IdReq dreq;
        dreq.set_account_id(s->accountId);
        ddt::AccountRow arow;
        dstub.GetAccountById(dctrl.get(), &dreq, &arow, nullptr);
        if(!dctrl->Failed() && arow.result() == SUCCESS) {
            s->gender = arow.gender();
            resp.set_gender(arow.gender());
        }
        SYLAR_LOG_INFO(g_logger) << "[" << ctrl->TraceId() << "] gate: login ok account=" << s->accountId
            << " name=" << s->name << " gender=" << (int)s->gender;
        // 上报在线状态到 Redis(经 data 服务 SADD online:players)。
        // 异步: 不阻塞 LOGIN_RESP 响应; 失败仅 warn 不影响登录。
        auto self = std::static_pointer_cast<GateServer>(shared_from_this());
        uint64_t accId = s->accountId;
        sylar::IOManager::GetThis()->schedule([self, accId]() {
            try {
                auto ch = self->dataChannel();
                ddt::DataService::Stub stub(ch.get());
                sylar::rpc::RpcController ctrl;
                ddt::IdReq req;
                req.set_account_id(accId);
                ddt::ResultResp resp;
                stub.SetOnline(&ctrl, &req, &resp, nullptr);
            } catch(const std::exception& e) {
                SYLAR_LOG_WARN(g_logger) << "gate: SetOnline fail account=" << accId << " err=" << e.what();
            }
        });
    }
    std::string payload;
    resp.SerializeToString(&payload);
    sendToSession(s, MSG_LOGIN_RESP, payload);
}

void GateServer::onRegister(ClientSession::ptr s, const std::string& body) {
    RegisterReq req;
    if(!req.ParseFromString(body)) {
        sendError(s, 400, "bad register req");
        return;
    }
    auto ch = loginChannel();
    ddt::LoginService::Stub stub(ch.get());
    auto ctrl = newCtrl(s, MSG_REGISTER);
    RegisterRpcReq rreq;
    rreq.set_name(req.name());
    rreq.set_password(req.password());
    RegisterRpcResp rresp;
    stub.Register(ctrl.get(), &rreq, &rresp, nullptr);
    RegisterResp resp;
    resp.set_result(rresp.result());
    resp.set_msg(rresp.msg());
    if(rresp.result() == SUCCESS) resp.set_account_id(rresp.account_id());
    std::string payload;
    resp.SerializeToString(&payload);
    sendToSession(s, MSG_REGISTER_RESP, payload);
}

void GateServer::onHeartbeat(ClientSession::ptr s, const std::string& body) {
    HeartbeatResp resp;
    resp.set_server_time((uint64_t)time(nullptr));
    std::string payload;
    resp.SerializeToString(&payload);
    sendToSession(s, MSG_HEARTBEAT_RESP, payload);
}

void GateServer::onSetGender(ClientSession::ptr s, const std::string& body) {
    SetGenderReq req;
    if(!req.ParseFromString(body)) {
        sendError(s, 400, "bad req");
        return;
    }
    auto dch = dataChannel();
    ddt::DataService::Stub dstub(dch.get());
    auto ctrl = newCtrl(s, MSG_SET_GENDER);
    ddt::UpdateGenderReq dreq;
    dreq.set_account_id(s->accountId);
    dreq.set_gender(req.gender());
    ddt::ResultResp dresp;
    dstub.UpdateGender(ctrl.get(), &dreq, &dresp, nullptr);
    SetGenderResp resp;
    if(ctrl->Failed() || dresp.result() != SUCCESS) {
        resp.set_result(FAIL);
        resp.set_msg(ctrl->Failed() ? ctrl->ErrorText() : dresp.msg());
    } else {
        resp.set_result(SUCCESS);
        s->gender = req.gender();   // 更新 session 缓存
    }
    std::string payload;
    resp.SerializeToString(&payload);
    sendToSession(s, MSG_SET_GENDER_RESP, payload);
}

void GateServer::onLogout(ClientSession::ptr s, const std::string& body) {
    // 退出登录: 删 Redis token + 通知 lobby 离开房间 + 通知 battle 离开战斗 + 清本地 session
    LogoutResp resp;
    resp.set_result(SUCCESS);
    std::string payload;
    resp.SerializeToString(&payload);
    sendToSession(s, MSG_LOGOUT_RESP, payload);

    if(s->accountId) {
        // 删 Redis token (调 data 服务)
        if(!s->token.empty()) {
            auto dch = std::make_shared<sylar::rpc::RpcChannel>(m_etcdEndpoint);
            ddt::DataService::Stub dstub(dch.get());
            auto dctrl = newCtrl(s, MSG_LOGOUT);
            TokenReq dreq;
            dreq.set_token(s->token);
            ResultResp dresp;
            dstub.DeleteToken(dctrl.get(), &dreq, &dresp, nullptr);
        }
        // 通知 lobby 离开房间 + battle 离开战斗
        {
            auto ch = lobbyChannel();
            ddt::LobbyService::Stub stub(ch.get());
            auto ctrl = newCtrl(s, MSG_LOGOUT);
            LeaveRoomRpcReq req;
            req.set_account_id(s->accountId);
            LeaveRoomRpcResp lresp;
            stub.LeaveRoom(ctrl.get(), &req, &lresp, nullptr);
        }
        {
            auto bch = battleChannel();
            ddt::BattleService::Stub bstub(bch.get());
            auto bctrl = newCtrl(s, MSG_LOGOUT);
            LeaveBattleRpcReq breq;
            breq.set_account_id(s->accountId);
            ResultResp bresp;
            bstub.LeaveBattle(bctrl.get(), &breq, &bresp, nullptr);
        }
        // 清 accountToSession(同样用安全删除)
        {
            MutexType::WriteLock lk(m_sessionMutex);
            auto it = m_accountToSession.find(s->accountId);
            if(it != m_accountToSession.end() && it->second == s) {
                m_accountToSession.erase(it);
            }
        }
        s->accountId = 0;
    }
    SYLAR_LOG_INFO(g_logger) << "gate: logout, closing session";
    if(s->sock) s->sock->close();
}

// ---- 房间/大厅(转发 lobby) ----
void GateServer::onRoomList(ClientSession::ptr s, const std::string& body) {
    auto ch = lobbyChannel();
    ddt::LobbyService::Stub stub(ch.get());
    auto ctrl = newCtrl(s, MSG_ROOM_LIST);
    RoomListRpcReq req;
    RoomListRpcResp resp;
    stub.RoomList(ctrl.get(), &req, &resp, nullptr);
    RoomListResp out;
    out.set_result(resp.result());
    for(const auto& r : resp.rooms()) *out.add_rooms() = r;
    std::string payload;
    out.SerializeToString(&payload);
    sendToSession(s, MSG_ROOM_LIST_RESP, payload);
}

void GateServer::onCreateRoom(ClientSession::ptr s, const std::string& body) {
    CreateRoomReq req;
    if(!req.ParseFromString(body)) {
        sendError(s, 400, "bad req");
        return;
    }
    auto ch = lobbyChannel();
    ddt::LobbyService::Stub stub(ch.get());
    auto ctrl = newCtrl(s, MSG_CREATE_ROOM);
    CreateRoomRpcReq rreq;
    rreq.set_account_id(s->accountId);
    rreq.set_name(s->name);
    rreq.set_room_name(req.room_name());
    rreq.set_mode(req.mode());
    rreq.set_map_name(req.map_name());
    rreq.set_gender(s->gender);
    CreateRoomRpcResp rresp;
    stub.CreateRoom(ctrl.get(), &rreq, &rresp, nullptr);
    CreateRoomResp out;
    out.set_result(rresp.result());
    out.set_room_id(rresp.room_id());
    out.set_msg(rresp.msg());
    std::string payload;
    out.SerializeToString(&payload);
    sendToSession(s, MSG_CREATE_ROOM_RESP, payload);
}

void GateServer::onJoinRoom(ClientSession::ptr s, const std::string& body) {
    JoinRoomReq req;
    if(!req.ParseFromString(body)) {
        sendError(s, 400, "bad req");
        return;
    }
    auto ch = lobbyChannel();
    ddt::LobbyService::Stub stub(ch.get());
    auto ctrl = newCtrl(s, MSG_JOIN_ROOM);
    JoinRoomRpcReq rreq;
    rreq.set_account_id(s->accountId);
    rreq.set_name(s->name);
    rreq.set_room_id(req.room_id());
    rreq.set_team(req.team());
    rreq.set_gender(s->gender);
    JoinRoomRpcResp rresp;
    stub.JoinRoom(ctrl.get(), &rreq, &rresp, nullptr);
    JoinRoomResp out;
    out.set_result(rresp.result());
    out.set_room_id(rresp.room_id());
    out.set_msg(rresp.msg());
    std::string payload;
    out.SerializeToString(&payload);
    sendToSession(s, MSG_JOIN_ROOM_RESP, payload);
}

void GateServer::onLeaveRoom(ClientSession::ptr s, const std::string&) {
    auto ch = lobbyChannel();
    ddt::LobbyService::Stub stub(ch.get());
    auto ctrl = newCtrl(s, MSG_LEAVE_ROOM);
    LeaveRoomRpcReq rreq;
    rreq.set_account_id(s->accountId);
    LeaveRoomRpcResp rresp;
    stub.LeaveRoom(ctrl.get(), &rreq, &rresp, nullptr);
    LeaveRoomResp out;
    out.set_result(rresp.result());
    out.set_msg(rresp.msg());
    std::string payload;
    out.SerializeToString(&payload);
    sendToSession(s, MSG_LEAVE_ROOM_RESP, payload);
}

void GateServer::onReady(ClientSession::ptr s, const std::string& body) {
    ReadyReq req;
    if(!req.ParseFromString(body)) {
        sendError(s, 400, "bad req");
        return;
    }
    auto ch = lobbyChannel();
    ddt::LobbyService::Stub stub(ch.get());
    auto ctrl = newCtrl(s, MSG_READY);
    ReadyRpcReq rreq;
    rreq.set_account_id(s->accountId);
    rreq.set_ready(req.ready());
    ResultResp rresp;
    stub.Ready(ctrl.get(), &rreq, &rresp, nullptr);
    if(rresp.result() != SUCCESS) sendError(s, 500, rresp.msg());
}

void GateServer::onSwitchTeam(ClientSession::ptr s, const std::string& body) {
    SwitchTeamReq req;
    if(!req.ParseFromString(body)) {
        sendError(s, 400, "bad req");
        return;
    }
    auto ch = lobbyChannel();
    ddt::LobbyService::Stub stub(ch.get());
    auto ctrl = newCtrl(s, MSG_SWITCH_TEAM);
    SwitchTeamRpcReq rreq;
    rreq.set_account_id(s->accountId);
    rreq.set_team(req.team());
    ResultResp rresp;
    stub.SwitchTeam(ctrl.get(), &rreq, &rresp, nullptr);
    SwitchTeamResp out;
    out.set_result(rresp.result());
    out.set_team(req.team());
    out.set_msg(rresp.msg());
    std::string payload;
    out.SerializeToString(&payload);
    sendToSession(s, MSG_SWITCH_TEAM_RESP, payload);
}

void GateServer::onSwitchWeapon(ClientSession::ptr s, const std::string& body) {
    SwitchWeaponReq req;
    if(!req.ParseFromString(body)) {
        sendError(s, 400, "bad req");
        return;
    }
    auto ch = lobbyChannel();
    ddt::LobbyService::Stub stub(ch.get());
    auto ctrl = newCtrl(s, MSG_SWITCH_WEAPON);
    SwitchWeaponRpcReq rreq;
    rreq.set_account_id(s->accountId);
    rreq.set_weapon_id(req.weapon_id());
    ResultResp rresp;
    stub.SwitchWeapon(ctrl.get(), &rreq, &rresp, nullptr);
    SwitchWeaponResp out;
    out.set_result(rresp.result());
    out.set_weapon_id(req.weapon_id());
    out.set_msg(rresp.msg());
    std::string payload;
    out.SerializeToString(&payload);
    sendToSession(s, MSG_SWITCH_WEAPON_RESP, payload);
}

// ---- 社交 ----
void GateServer::onChat(ClientSession::ptr s, const std::string& body) {
    ChatReq req;
    if(!req.ParseFromString(body)) {
        sendError(s, 400, "bad req");
        return;
    }
    auto ch = lobbyChannel();
    ddt::LobbyService::Stub stub(ch.get());
    auto ctrl = newCtrl(s, MSG_CHAT);
    ChatRpcReq rreq;
    rreq.set_account_id(s->accountId);
    rreq.set_name(s->name);
    rreq.set_channel(req.channel());
    rreq.set_message(req.message());
    ResultResp rresp;
    stub.Chat(ctrl.get(), &rreq, &rresp, nullptr);
    if(rresp.result() != SUCCESS) sendError(s, 500, "chat fail");
}

void GateServer::onChatHistory(ClientSession::ptr s, const std::string& body) {
    ChatHistoryReq req;
    if(!req.ParseFromString(body)) {
        sendError(s, 400, "bad req");
        return;
    }
    auto ch = lobbyChannel();
    ddt::LobbyService::Stub stub(ch.get());
    auto ctrl = newCtrl(s, MSG_CHAT_HISTORY);
    GetChatHistoryReq rreq;
    rreq.set_channel(req.channel());
    rreq.set_count(req.count());
    ChatHistoryRespRpc rresp;
    stub.ChatHistory(ctrl.get(), &rreq, &rresp, nullptr);
    ChatHistoryResp out;
    out.set_channel(req.channel());
    for(const auto& e : rresp.entries()) {
        auto* m = out.add_messages();
        m->set_channel(req.channel());
        m->set_sender_id(e.sender_id());
        m->set_sender_name(e.sender_name());
        m->set_message(e.message());
        m->set_timestamp(e.timestamp());
    }
    std::string payload;
    out.SerializeToString(&payload);
    sendToSession(s, MSG_CHAT_HISTORY_RESP, payload);
}

void GateServer::onPrivateChat(ClientSession::ptr s, const std::string& body) {
    PrivateChatReq req;
    if(!req.ParseFromString(body)) {
        sendError(s, 400, "bad req");
        return;
    }
    auto ch = lobbyChannel();
    ddt::LobbyService::Stub stub(ch.get());
    auto ctrl = newCtrl(s, MSG_PRIVATE_CHAT);
    PrivateChatRpcReq rreq;
    rreq.set_account_id(s->accountId);
    rreq.set_name(s->name);
    rreq.set_target_account_id(req.target_account_id());
    rreq.set_message(req.message());
    ResultResp rresp;
    stub.PrivateChat(ctrl.get(), &rreq, &rresp, nullptr);
}

void GateServer::onFriendAdd(ClientSession::ptr s, const std::string& body) {
    FriendAddReq req;
    if(!req.ParseFromString(body)) {
        sendError(s, 400, "bad req");
        return;
    }
    auto ch = lobbyChannel();
    ddt::LobbyService::Stub stub(ch.get());
    auto ctrl = newCtrl(s, MSG_FRIEND_ADD);
    FriendAddRpcReq rreq;
    rreq.set_account_id(s->accountId);
    rreq.set_target_name(req.target_name());
    FriendAddRpcResp rresp;
    stub.FriendAdd(ctrl.get(), &rreq, &rresp, nullptr);
    FriendAddResp out;
    out.set_result(rresp.result());
    out.set_msg(rresp.msg());
    out.set_friend_id(rresp.friend_id());
    out.set_friend_name(rresp.friend_name());
    std::string payload;
    out.SerializeToString(&payload);
    sendToSession(s, MSG_FRIEND_ADD_RESP, payload);
}

void GateServer::onFriendList(ClientSession::ptr s, const std::string&) {
    auto ch = lobbyChannel();
    ddt::LobbyService::Stub stub(ch.get());
    auto ctrl = newCtrl(s, MSG_FRIEND_LIST);
    IdReq rreq;
    rreq.set_account_id(s->accountId);
    FriendListRpcResp rresp;
    stub.FriendList(ctrl.get(), &rreq, &rresp, nullptr);
    FriendListResp out;
    for(const auto& f : rresp.friends()) *out.add_friends() = f;
    std::string payload;
    out.SerializeToString(&payload);
    sendToSession(s, MSG_FRIEND_LIST_RESP, payload);
}

// ---- 战斗(转发 battle) ----
void GateServer::onShoot(ClientSession::ptr s, const std::string& body) {
    ShootReq req;
    if(!req.ParseFromString(body)) {
        sendError(s, 400, "bad req");
        return;
    }
    auto ch = battleChannel();
    ddt::BattleService::Stub stub(ch.get());
    auto ctrl = newCtrl(s, MSG_SHOOT);
    ShootRpcReq rreq;
    rreq.set_account_id(s->accountId);
    rreq.set_angle(req.angle());
    rreq.set_force(req.force());
    rreq.set_is_fly(req.is_fly());
    rreq.set_weapon_id(req.weapon_id());
    ResultResp rresp;
    stub.Shoot(ctrl.get(), &rreq, &rresp, nullptr);
}

void GateServer::onMove(ClientSession::ptr s, const std::string& body) {
    MoveReq req;
    if(!req.ParseFromString(body)) {
        sendError(s, 400, "bad req");
        return;
    }
    auto ch = battleChannel();
    ddt::BattleService::Stub stub(ch.get());
    auto ctrl = newCtrl(s, MSG_MOVE);
    MoveRpcReq rreq;
    rreq.set_account_id(s->accountId);
    rreq.set_delta_x(req.delta_x());
    ResultResp rresp;
    stub.Move(ctrl.get(), &rreq, &rresp, nullptr);
}

void GateServer::onPass(ClientSession::ptr s, const std::string&) {
    auto ch = battleChannel();
    ddt::BattleService::Stub stub(ch.get());
    auto ctrl = newCtrl(s, MSG_PASS);
    PassRpcReq rreq;
    rreq.set_account_id(s->accountId);
    ResultResp rresp;
    stub.Pass(ctrl.get(), &rreq, &rresp, nullptr);
}

void GateServer::onAimBegin(ClientSession::ptr s, const std::string&) {
    // 蓄力开始: 转发给 battle, 重置回合计时(给蓄力预留时间)。复用 PassRpcReq(只需 account_id)。
    auto ch = battleChannel();
    ddt::BattleService::Stub stub(ch.get());
    auto ctrl = newCtrl(s, MSG_AIM_BEGIN);
    PassRpcReq rreq;
    rreq.set_account_id(s->accountId);
    ResultResp rresp;
    stub.AimBegin(ctrl.get(), &rreq, &rresp, nullptr);
}

// ---- PushService: 供 lobby/battle 回调推送 ----
void GateServer::NotifyClient(::google::protobuf::RpcController*,
        const NotifyReq* req, ResultResp* resp, ::google::protobuf::Closure* done) {
    auto s = sessionByAccount(req->account_id());
    if(s) {
        sendToSession(s, (uint16_t)req->msg_id(), req->payload());
        resp->set_result(SUCCESS);
    } else {
        resp->set_result(NOT_FOUND);
        resp->set_msg("session not found");
    }
    if(done) done->Run();
}

void GateServer::NotifyClients(::google::protobuf::RpcController*,
        const NotifyManyReq* req, ResultResp* resp, ::google::protobuf::Closure* done) {
    for(uint64_t aid : req->account_ids()) {
        auto s = sessionByAccount(aid);
        if(s) sendToSession(s, (uint16_t)req->msg_id(), req->payload());
    }
    resp->set_result(SUCCESS);
    if(done) done->Run();
}

void GateServer::NotifyAllOnline(::google::protobuf::RpcController*,
        const NotifyReq* req, ResultResp* resp, ::google::protobuf::Closure* done) {
    // 遍历所有在线 session 推送(用于大厅全量房间列表广播)
    MutexType::ReadLock lk(m_sessionMutex);
    auto snapshot = m_accountToSession;   // 拷贝快照, 锁外发送避免长持锁
    lk.unlock();
    for(auto& kv : snapshot) {
        if(kv.second) sendToSession(kv.second, (uint16_t)req->msg_id(), req->payload());
    }
    resp->set_result(SUCCESS);
    if(done) done->Run();
}

// ---- 心跳检查 ----
void GateServer::startHeartbeatCheck() {
    auto self = std::static_pointer_cast<GateServer>(shared_from_this());
    uint64_t interval = m_cfg.heartbeat_check_interval;
    uint64_t timeout = m_cfg.heartbeat_timeout;
    // 分片心跳检查: 每 tick 只扫描 1/SHARDS 的 session(按计数取模), 把 O(n) 摊到 SHARDS 个周期。
    // SHARDS=4: 4×10s=40s 扫完全量 < timeout=45s, 不会漏超时; 读锁持有时间降到 1/4。
    // (原实现每 tick 全量扫描 m_sockToSession, 大量在线时读锁持锁久, 阻塞 delSession 写锁。)
    static constexpr int SHARDS = 4;
    // tick 用 shared_ptr 按值捕获进循环定时器闭包: startHeartbeatCheck 返回后局部变量
    // 会销毁, 若用 &tick 引用捕获, 定时器回调触发时 tick 已悬空 → 段错误。
    auto tick = std::make_shared<int>(0);
    self->m_tw->addTimer(interval * 1000, [self, timeout, tick]() {
        int shard = (*tick)++ % SHARDS;
        std::vector<ClientSession::ptr> dead;
        uint64_t now = sylar::GetCurrentMS();
        {
            MutexType::ReadLock lk(self->m_sessionMutex);
            int idx = 0;
            for(auto& kv : self->m_sockToSession) {
                if((idx++ % SHARDS) == shard) {   // 只检查本分片的 session
                    if(now - kv.second->lastRecvMs > timeout * 1000) dead.push_back(kv.second);
                }
            }
        }
        for(auto& s : dead) {
            SYLAR_LOG_INFO(g_logger) << "gate: heartbeat timeout, close account=" << s->accountId;
            if(s->sock) s->sock->close();
        }
    }, true);   // 循环定时器
}

// ---- Redis 订阅: 世界聊天广播 ----
// data 服务 PUBLISH chat:world <ChatNotify payload>, gate 订阅后调 NotifyAllOnline
// 把消息推给本地所有在线玩家。替代原 lobby → gate.PushService.NotifyAllOnline 的 RPC 调用。
// 多 gate 实例时天然分发: 每个 gate 各自订阅, 只推本地 session。
void GateServer::startWorldChatSubscriber() {
    if(m_redisHost.empty()) {
        return;
    }
    m_subscriber = std::make_shared<Subscriber>(m_redisHost, m_redisPort);
    // 关键: 必须捕获 IOManager 指针。订阅线程是裸 std::thread, 不是 IOManager worker,
    // 它的 IOManager::GetThis() 返回 nullptr → sendToSession 内部调 schedule 会解引用 nullptr → SIGSEGV。
    // 用捕获的 iom 指针让订阅线程能正确投递协程到 gate 的 IOManager。
    auto iom = sylar::IOManager::GetThis();
    auto self = std::static_pointer_cast<GateServer>(shared_from_this());
    m_subThread = std::make_shared<std::thread>([self, iom]() {
        while(true) {
            if(!self->m_subscriber->subscribe("chat:world", [self, iom](const std::string& payload) {
                // 收到消息: 投递到 gate 的 IOManager 上执行(而非本裸线程),
                // 这样 sendToSession 内部的 IOManager::GetThis() 才有效。
                iom->schedule([self, payload]() {
                    // 解析 ChatNotify 并广播给本地所有在线 session
                    ddt::ChatNotify n;
                    if(!n.ParseFromString(payload)) {
                        return;
                    }
                    sylar::RWMutex::ReadLock lk(self->m_sessionMutex);
                    auto snapshot = self->m_accountToSession;
                    lk.unlock();
                    for(auto& kv : snapshot) {
                        if(kv.second) {
                            self->sendToSession(kv.second, MSG_CHAT_NOTIFY, payload);
                        }
                    }
                });
            })) {
                sleep(2);
                continue;
            }
            SYLAR_LOG_INFO(g_logger) << "gate: subscribed chat:world";
            self->m_subscriber->loop();
            SYLAR_LOG_WARN(g_logger) << "gate: chat:world subscription lost, reconnecting in 2s";
            sleep(2);
        }
    });
    m_subThread->detach();
}

} // namespace ddt
