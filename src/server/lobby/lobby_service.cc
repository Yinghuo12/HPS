#include "lobby_service.h"

#include <ctime>

#include "gate.pb.h"
#include "msg_id.h"
#include "sylar/core/log.h"
#include "sylar/rpc/rpc_controller.h"
#include "sylar/util/json_util.h"

namespace ddt {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("rpc");

RoomInfo LobbyRoom::toRoomInfo() const {
    RoomInfo ri;
    ri.set_room_id(roomId);
    ri.set_room_name(name);
    ri.set_player_count(seatCount);
    ri.set_max_players(LobbyRoom::kMaxSeats);
    ri.set_game_started(started);
    ri.set_mode(mode);
    ri.set_map_name(mapName);
    for(int i = 0; i < seatCount; ++i) {
        auto* s = ri.add_players();
        s->set_account_id(seats[i].accountId);
        s->set_name(seats[i].name);
        s->set_team(seats[i].team);
        s->set_ready(seats[i].ready);
        s->set_gender(seats[i].gender);
        s->set_weapon_id(seats[i].weaponId);
    }
    return ri;
}

LobbyServiceImpl::LobbyServiceImpl(const std::string& etcdEndpoint)
    : m_etcdEndpoint(etcdEndpoint) {
}

LobbyServiceImpl::~LobbyServiceImpl() {
}

std::shared_ptr<sylar::rpc::RpcChannel> LobbyServiceImpl::dataChannel() {
    std::lock_guard<std::mutex> lk(m_channelMutex);
    if(!m_dataChannel) {
        m_dataChannel = std::make_shared<sylar::rpc::RpcChannel>(m_etcdEndpoint);
    }
    return m_dataChannel;
}

std::shared_ptr<sylar::rpc::RpcChannel> LobbyServiceImpl::battleChannel() {
    std::lock_guard<std::mutex> lk(m_channelMutex);
    if(!m_battleChannel) {
        m_battleChannel = std::make_shared<sylar::rpc::RpcChannel>(m_etcdEndpoint);
    }
    return m_battleChannel;
}

LobbyRoom* LobbyServiceImpl::findRoomByAccountLocked(uint64_t accountId) {
    for(auto& kv : m_rooms) {
        for(int i = 0; i < kv.second->seatCount; ++i) {
            if(kv.second->seats[i].accountId == accountId) return kv.second.get();
        }
    }
    return nullptr;
}

void LobbyServiceImpl::broadcastRoomUpdate(uint32_t roomId) {
    sylar::RWMutex::ReadLock lk(m_mutex);
    auto it = m_rooms.find(roomId);
    if(it == m_rooms.end()) return;
    RoomInfo ri = it->second->toRoomInfo();
    lk.unlock();

    if(!m_push) return;
    // 包进 RoomUpdateNotify{ room_info = ri }(客户端用 RoomUpdateNotify.Parser 解析)
    RoomUpdateNotify notify;
    *notify.mutable_room_info() = ri;
    std::string payload;
    if(!notify.SerializeToString(&payload)) return;
    // 推给房内每个玩家
    lk.lock();
    auto it2 = m_rooms.find(roomId);
    if(it2 == m_rooms.end()) return;
    auto& room = it2->second;
    for(int i = 0; i < room->seatCount; ++i) {
        RoutingHandle h(room->seats[i].accountId, room->seats[i].gatewayId);
        lk.unlock();
        m_push(h, MSG_ROOM_UPDATE, payload);
        lk.lock();
    }
}

void LobbyServiceImpl::broadcastRoomListToAll() {
    // 构造全量房间列表, 调 gate 的 NotifyAllOnline 推给所有在线玩家。
    // 客户端大厅订阅 MSG_ROOM_LIST_NOTIFY, 收到即全量刷新(无需轮询)。
    if(!m_push) return;
    RoomListRpcResp resp;
    {
        sylar::RWMutex::ReadLock lk(m_mutex);
        for(auto& kv : m_rooms) {
            if(kv.second->seatCount > 0 && !kv.second->started) {
                *resp.add_rooms() = kv.second->toRoomInfo();
            }
        }
    }
    std::string payload;
    if(!resp.SerializeToString(&payload)) return;
    // 用注入的 pushAll 闭包(lobby_main 已接连接池)推给所有在线玩家,
    // 替代此处栈上新建短连接 channel(遗漏点, 每次房间变更都走短连接)。
    if(m_pushAll) m_pushAll(MSG_ROOM_LIST_NOTIFY, payload);
}

void LobbyServiceImpl::RoomList(::google::protobuf::RpcController*,
        const RoomListRpcReq*, RoomListRpcResp* resp, ::google::protobuf::Closure* done) {
    sylar::RWMutex::ReadLock lk(m_mutex);
    resp->set_result(SUCCESS);
    for(auto& kv : m_rooms) {
        if(kv.second->seatCount > 0 && !kv.second->started) {
            *resp->add_rooms() = kv.second->toRoomInfo();
        }
    }
    if(done) done->Run();
}

void LobbyServiceImpl::CreateRoom(::google::protobuf::RpcController*,
        const CreateRoomRpcReq* req, CreateRoomRpcResp* resp, ::google::protobuf::Closure* done) {
    uint32_t rid = m_nextRoomId.fetch_add(1);
    auto room = std::make_shared<LobbyRoom>();
    room->roomId = rid;
    room->name = req->room_name().empty() ? (req->name() + "'s room") : req->room_name();
    room->mode = req->mode().empty() ? "custom" : req->mode();
    room->mapName = req->map_name().empty() ? "rainbow" : req->map_name();
    room->seats[0].accountId = req->account_id();
    room->seats[0].name = req->name();
    room->seats[0].team = TEAM_RED;
    room->seats[0].ready = false;
    room->seats[0].gatewayId = 0;
    room->seats[0].gender = req->gender();
    room->seatCount = 1;
    {
        sylar::RWMutex::WriteLock lk(m_mutex);
        m_rooms[rid] = room;
    }
    resp->set_result(SUCCESS);
    resp->set_room_id(rid);
    SYLAR_LOG_INFO(g_logger) << "lobby: create room " << rid << " by " << req->name();
    // 补: 创建者也要收到房间快照(当前缺失) + 通知大厅所有人有新房间
    broadcastRoomUpdate(rid);
    broadcastRoomListToAll();
    if(done) done->Run();
}

void LobbyServiceImpl::JoinRoom(::google::protobuf::RpcController*,
        const JoinRoomRpcReq* req, JoinRoomRpcResp* resp, ::google::protobuf::Closure* done) {
    sylar::RWMutex::WriteLock lk(m_mutex);
    // 已在房间?
    if(findRoomByAccountLocked(req->account_id())) {
        resp->set_result(ALREADY);
        resp->set_msg("already in a room");
        if(done) done->Run();
        return;
    }
    auto it = m_rooms.find(req->room_id());
    if(it == m_rooms.end()) {
        resp->set_result(NOT_FOUND);
        resp->set_msg("room not found");
        if(done) done->Run();
        return;
    }
    auto& room = it->second;
    if(room->started || room->seatCount >= LobbyRoom::kMaxSeats) {
        resp->set_result(FULL);
        resp->set_msg("room full or started");
        if(done) done->Run();
        return;
    }
    // 统计两队现有人数, 目标队满则换到人少的队(每队上限 4)
    int redCnt = 0, blueCnt = 0;
    for(int i = 0; i < room->seatCount; ++i) {
        if(room->seats[i].team == TEAM_RED) ++redCnt; else ++blueCnt;
    }
    TeamSide team = req->team();
    const int kPerTeam = LobbyRoom::kMaxSeats / 2;
    if(team == TEAM_RED && redCnt >= kPerTeam) team = TEAM_BLUE;
    else if(team == TEAM_BLUE && blueCnt >= kPerTeam) team = TEAM_RED;

    auto& seat = room->seats[room->seatCount];
    seat.accountId = req->account_id();
    seat.name = req->name();
    seat.team = team;
    seat.ready = false;
    seat.gatewayId = 0;
    seat.gender = req->gender();
    room->seatCount++;
    uint32_t rid = room->roomId;
    lk.unlock();

    resp->set_result(SUCCESS);
    resp->set_room_id(rid);
    broadcastRoomUpdate(rid);
    broadcastRoomListToAll();
    if(done) done->Run();
}

void LobbyServiceImpl::LeaveRoom(::google::protobuf::RpcController*,
        const LeaveRoomRpcReq* req, LeaveRoomRpcResp* resp, ::google::protobuf::Closure* done) {
    uint32_t rid = 0;
    {
        sylar::RWMutex::WriteLock lk(m_mutex);
        auto room = findRoomByAccountLocked(req->account_id());
        if(!room) {
            resp->set_result(NOT_FOUND);
            resp->set_msg("not in any room");
            if(done) done->Run();
            return;
        }
        rid = room->roomId;
        // 移除该席位并压缩
        int idx = -1;
        for(int i = 0; i < room->seatCount; ++i) {
            if(room->seats[i].accountId == req->account_id()) { idx = i; break; }
        }
        if(idx >= 0) {
            for(int i = idx; i < room->seatCount - 1; ++i) room->seats[i] = room->seats[i + 1];
            room->seatCount--;
        }
        // 战斗结束后 LeaveRoom: 重置房间 started 状态 + 清 ready, 让房间重新可用
        if(room->started) {
            room->started = false;
            for(int i = 0; i < room->seatCount; ++i) room->seats[i].ready = false;
        }
        if(room->seatCount == 0) {
            m_rooms.erase(rid);
        }
    }
    resp->set_result(SUCCESS);
    broadcastRoomUpdate(rid);
    broadcastRoomListToAll();
    if(done) done->Run();
}

void LobbyServiceImpl::Ready(::google::protobuf::RpcController*,
        const ReadyRpcReq* req, ResultResp* resp, ::google::protobuf::Closure* done) {
    uint32_t rid = 0;
    {
        sylar::RWMutex::WriteLock lk(m_mutex);
        auto room = findRoomByAccountLocked(req->account_id());
        if(!room) {
            resp->set_result(NOT_FOUND);
            resp->set_msg("not in any room");
            if(done) done->Run();
            return;
        }
        rid = room->roomId;
        // 战斗结束回归: 房间仍处于 started 状态时, 玩家再次准备表示"开始新一局"。
        // 重置 started + 清所有人的 ready(包括本次), 进入新一轮准备流程。
        if(room->started) {
            room->started = false;
            for(int i = 0; i < room->seatCount; ++i) room->seats[i].ready = false;
        }
        for(int i = 0; i < room->seatCount; ++i) {
            if(room->seats[i].accountId == req->account_id()) {
                room->seats[i].ready = req->ready();
                break;
            }
        }
    }
    resp->set_result(SUCCESS);
    broadcastRoomUpdate(rid);
    broadcastRoomListToAll();
    tryStart(rid);   // 双方就绪则置 started
    if(done) done->Run();
}

void LobbyServiceImpl::SwitchTeam(::google::protobuf::RpcController*,
        const SwitchTeamRpcReq* req, ResultResp* resp, ::google::protobuf::Closure* done) {
    uint32_t rid = 0;
    {
        sylar::RWMutex::WriteLock lk(m_mutex);
        auto room = findRoomByAccountLocked(req->account_id());
        if(!room) {
            resp->set_result(NOT_FOUND);
            resp->set_msg("not in any room");
            if(done) done->Run();
            return;
        }
        rid = room->roomId;
        // 目标队伍已满(每队上限 4)则拒绝
        const int kPerTeam = LobbyRoom::kMaxSeats / 2;
        int targetCnt = 0;
        for(int i = 0; i < room->seatCount; ++i) {
            if(room->seats[i].accountId != req->account_id()
               && room->seats[i].team == req->team()) ++targetCnt;
        }
        if(targetCnt >= kPerTeam) {
            resp->set_result(FULL);
            resp->set_msg("target team full");
            if(done) done->Run();
            return;
        }
        for(int i = 0; i < room->seatCount; ++i) {
            if(room->seats[i].accountId == req->account_id()) {
                room->seats[i].team = req->team();
                room->seats[i].ready = false;   // 换队时取消准备(防同方带准备切到敌方直接开局)
                break;
            }
        }
    }
    resp->set_result(SUCCESS);
    broadcastRoomUpdate(rid);
    broadcastRoomListToAll();
    if(done) done->Run();
}

void LobbyServiceImpl::SwitchWeapon(::google::protobuf::RpcController*,
        const SwitchWeaponRpcReq* req, ResultResp* resp, ::google::protobuf::Closure* done) {
    uint32_t rid = 0;
    {
        sylar::RWMutex::WriteLock lk(m_mutex);
        auto room = findRoomByAccountLocked(req->account_id());
        if(!room) {
            resp->set_result(NOT_FOUND);
            resp->set_msg("not in any room");
            if(done) done->Run();
            return;
        }
        rid = room->roomId;
        for(int i = 0; i < room->seatCount; ++i) {
            if(room->seats[i].accountId == req->account_id()) {
                // 准备后不能换武器(必须取消准备)
                if(room->seats[i].ready) {
                    resp->set_result(FAIL);
                    resp->set_msg("ready locked, cancel ready first");
                    if(done) done->Run();
                    return;
                }
                room->seats[i].weaponId = req->weapon_id();
                break;
            }
        }
    }
    resp->set_result(SUCCESS);
    broadcastRoomUpdate(rid);
    if(done) done->Run();
}

void LobbyServiceImpl::tryStart(uint32_t roomId) {
    // 双方就绪则转交 battle 服: 锁内拷贝玩家快照 + 置 started, 锁外 RPC 调 EnterBattle。
    struct Snap { uint64_t accountId; std::string name; TeamSide team; uint64_t gatewayId; Gender gender; int weaponId; };
    std::vector<Snap> players;
    bool shouldStart = false;
    {
        sylar::RWMutex::WriteLock lk(m_mutex);
        auto it = m_rooms.find(roomId);
        if(it == m_rooms.end()) return;
        auto& room = it->second;
        // 多人战斗: 至少 2 人 + 红蓝各至少 1 人 + 全员准备, 则全量送战斗
        if(room->seatCount < 2 || room->started) return;
        int redReady = 0, blueReady = 0;
        bool allReady = true;
        for(int i = 0; i < room->seatCount; ++i) {
            if(!room->seats[i].ready) { allReady = false; break; }
            if(room->seats[i].team == TEAM_RED) ++redReady; else ++blueReady;
        }
        if(!allReady || redReady < 1 || blueReady < 1) return;
        room->started = true;   // 已转交, 不再对外可见
        for(int i = 0; i < room->seatCount; ++i) {
            players.push_back({room->seats[i].accountId, room->seats[i].name,
                               room->seats[i].team, room->seats[i].gatewayId,
                               room->seats[i].gender, room->seats[i].weaponId});
        }
        shouldStart = true;
    }
    if(!shouldStart) return;

    // 锁外: 构造 EnterBattleReq 调 battle 服
    auto ch = battleChannel();
    ddt::BattleService::Stub stub(ch.get());
    sylar::rpc::RpcController ctrl;
    EnterBattleReq req;
    for(const auto& p : players) {
        auto* bi = req.add_players();
        bi->set_account_id(p.accountId);
        bi->set_name(p.name);
        bi->set_team(p.team);
        bi->set_gateway_id(p.gatewayId);
        bi->set_gender(p.gender);
        bi->set_weapon_id(p.weaponId);
    }
    EnterBattleResp resp;
    stub.EnterBattle(&ctrl, &req, &resp, nullptr);
    if(ctrl.Failed() || resp.result() != SUCCESS) {
        SYLAR_LOG_ERROR(g_logger) << "lobby: EnterBattle fail room=" << roomId
            << " err=" << (ctrl.Failed() ? ctrl.ErrorText() : "battle rejected");
        // 回滚: 让房间重新可加入(标记未开始)
        sylar::RWMutex::WriteLock lk(m_mutex);
        auto it = m_rooms.find(roomId);
        if(it != m_rooms.end()) it->second->started = false;
        return;
    }
    SYLAR_LOG_INFO(g_logger) << "lobby: room " << roomId << " handed to battle room " << resp.room_id();
}

void LobbyServiceImpl::FriendAdd(::google::protobuf::RpcController*,
        const FriendAddRpcReq* req, FriendAddRpcResp* resp, ::google::protobuf::Closure* done) {
    auto ch = dataChannel();
    ddt::DataService::Stub dataStub(ch.get());
    sylar::rpc::RpcController ctrl;
    NameReq nreq;
    nreq.set_name(req->target_name());
    AccountRow arow;
    dataStub.GetAccountByName(&ctrl, &nreq, &arow, nullptr);
    if(ctrl.Failed() || arow.result() != SUCCESS) {
        resp->set_result(NOT_FOUND);
        resp->set_msg("target not found");
        if(done) done->Run();
        return;
    }
    sylar::rpc::RpcController ctrl2;
    AddFriendReq areq;
    areq.set_account_id(req->account_id());
    areq.set_friend_id(arow.account_id());
    ResultResp aresp;
    dataStub.AddFriend(&ctrl2, &areq, &aresp, nullptr);
    resp->set_result(ctrl2.Failed() || aresp.result() != SUCCESS ? FAIL : SUCCESS);
    resp->set_friend_id(arow.account_id());
    resp->set_friend_name(arow.name());
    if(done) done->Run();
}

void LobbyServiceImpl::FriendList(::google::protobuf::RpcController*,
        const IdReq* req, FriendListRpcResp* resp, ::google::protobuf::Closure* done) {
    auto ch = dataChannel();
    ddt::DataService::Stub dataStub(ch.get());
    sylar::rpc::RpcController ctrl;
    dataStub.GetFriendList(&ctrl, req, resp, nullptr);
    if(ctrl.Failed()) resp->set_result(FAIL);
    if(done) done->Run();
}

void LobbyServiceImpl::Chat(::google::protobuf::RpcController*,
        const ChatRpcReq* req, ResultResp* resp, ::google::protobuf::Closure* done) {
    // 持久化(WORLD/ROOM)
    if(req->channel() == CHANNEL_WORLD || req->channel() == CHANNEL_ROOM) {
        auto ch = dataChannel();
        ddt::DataService::Stub dataStub(ch.get());
        sylar::rpc::RpcController ctrl;
        SaveChatReq sreq;
        sreq.set_channel(req->channel());
        sreq.set_sender_id(req->account_id());
        sreq.set_sender_name(req->name());
        sreq.set_message(req->message());
        ResultResp sresp;
        dataStub.SaveChat(&ctrl, &sreq, &sresp, nullptr);
    }
    // 广播: WORLD -> 全体; ROOM/TEAM -> 房内
    if(m_push) {
        ChatNotify notify;
        notify.set_channel(req->channel());
        notify.set_sender_id(req->account_id());
        notify.set_sender_name(req->name());
        notify.set_message(req->message());
        notify.set_timestamp((uint64_t)time(nullptr));
        std::string payload;
        notify.SerializeToString(&payload);

        if(req->channel() == CHANNEL_WORLD) {
            // 世界频道: 广播给所有在线玩家(用 gate 的 NotifyAllOnline)
            if(m_pushAll) {
                m_pushAll(MSG_CHAT_NOTIFY, payload);
            } else if(m_push) {
                // 退化: 没有全在线广播时, 至少推给房内
                sylar::RWMutex::ReadLock lk(m_mutex);
                std::vector<RoutingHandle> targets;
                for(auto& kv : m_rooms) {
                    for(int i = 0; i < kv.second->seatCount; ++i) {
                        targets.emplace_back(kv.second->seats[i].accountId, kv.second->seats[i].gatewayId);
                    }
                }
                lk.unlock();
                for(auto& h : targets) m_push(h, MSG_CHAT_NOTIFY, payload);
            }
        } else {
            // ROOM: 推给同房所有人; TEAM: 只推给同房同队
            sylar::RWMutex::ReadLock lk(m_mutex);
            auto room = findRoomByAccountLocked(req->account_id());
            if(room) {
                // 找到发送者所在队伍(TEAM 频道用)
                TeamSide senderTeam = TEAM_RED;
                for(int i = 0; i < room->seatCount; ++i) {
                    if(room->seats[i].accountId == req->account_id()) { senderTeam = room->seats[i].team; break; }
                }
                std::vector<RoutingHandle> targets;
                for(int i = 0; i < room->seatCount; ++i) {
                    if(req->channel() == CHANNEL_TEAM && room->seats[i].team != senderTeam) continue;   // TEAM: 仅同队
                    targets.emplace_back(room->seats[i].accountId, room->seats[i].gatewayId);
                }
                lk.unlock();
                for(auto& h : targets) m_push(h, MSG_CHAT_NOTIFY, payload);
            }
        }
    }
    resp->set_result(SUCCESS);
    if(done) done->Run();
}

void LobbyServiceImpl::PrivateChat(::google::protobuf::RpcController*,
        const PrivateChatRpcReq* req, ResultResp* resp, ::google::protobuf::Closure* done) {
    auto ch = dataChannel();
    ddt::DataService::Stub dataStub(ch.get());
    sylar::rpc::RpcController ctrl;
    SaveChatReq sreq;
    sreq.set_channel(CHANNEL_PRIVATE);
    sreq.set_sender_id(req->account_id());
    sreq.set_sender_name(req->name());
    sreq.set_message(req->message());
    sreq.set_target_id(req->target_account_id());
    ResultResp sresp;
    dataStub.SaveChat(&ctrl, &sreq, &sresp, nullptr);

    // 推给对方 + 回声给自己
    if(m_push) {
        ChatNotify notify;
        notify.set_channel(CHANNEL_PRIVATE);
        notify.set_sender_id(req->account_id());
        notify.set_sender_name(req->name());
        notify.set_message(req->message());
        notify.set_timestamp((uint64_t)time(nullptr));
        std::string payload;
        notify.SerializeToString(&payload);
        m_push(RoutingHandle(req->target_account_id(), 0), MSG_CHAT_NOTIFY, payload);
        m_push(RoutingHandle(req->account_id(), 0), MSG_CHAT_NOTIFY, payload);
    }
    resp->set_result(SUCCESS);
    if(done) done->Run();
}

void LobbyServiceImpl::ChatHistory(::google::protobuf::RpcController*,
        const GetChatHistoryReq* req, ChatHistoryRespRpc* resp, ::google::protobuf::Closure* done) {
    auto ch = dataChannel();
    ddt::DataService::Stub dataStub(ch.get());
    sylar::rpc::RpcController ctrl;
    dataStub.GetChatHistory(&ctrl, req, resp, nullptr);
    if(ctrl.Failed()) resp->set_result(FAIL);
    if(done) done->Run();
}

} // namespace ddt
