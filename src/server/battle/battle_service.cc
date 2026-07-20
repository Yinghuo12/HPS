#include "battle_service.h"

#include "sylar/core/log.h"

namespace ddt {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("rpc");

// 构造战斗服务
BattleServiceImpl::BattleServiceImpl(const ServiceConfig& cfg, PushFn push,
                                     sylar::TimeWheel::ptr tw, BroadcastPushFn bpush,
                                     std::shared_ptr<sylar::rpc::RpcChannel> dataChannel)
    : m_cfg(cfg)
    , m_push(push)
    , m_bpush(bpush)
    , m_tw(tw)
    , m_data(dataChannel) {
}

BattleServiceImpl::~BattleServiceImpl() {
}

// 找玩家所在房间(假设已持锁)
BattleRoom::ptr BattleServiceImpl::roomOfLocked(uint64_t accountId) {
    auto it = m_accountToRoom.find(accountId);
    if (it == m_accountToRoom.end()) {
        return nullptr;
    }
    auto rit = m_rooms.find(it->second);
    return rit == m_rooms.end() ? nullptr : rit->second;
}

// 进入战斗: 创建房间 + 添加玩家 + 开局广播
void BattleServiceImpl::EnterBattle(::google::protobuf::RpcController*,
        const EnterBattleReq* req, EnterBattleResp* resp, ::google::protobuf::Closure* done) {
    if (req->players_size() < m_cfg.min_players) {
        resp->set_result(BAD_PARAM);
        resp->set_msg("not enough players");
        if (done) {
            done->Run();
        }
        return;
    }
    uint32_t rid = m_nextRoomId.fetch_add(1);
    auto room = std::make_shared<BattleRoom>(rid, m_cfg, m_push, m_tw.get(), m_bpush, m_data);
    for (const auto& p : req->players()) {
        room->addPlayer(p.account_id(), p.name(), p.team(), p.gateway_id(), p.gender(), p.weapon_id());
    }
    if (!room->canStart()) {
        resp->set_result(FAIL);
        resp->set_msg("cannot start battle");
        if (done) {
            done->Run();
        }
        return;
    }
    {
        sylar::RWMutex::WriteLock lk(m_mutex);
        m_rooms[rid] = room;
        for (const auto& p : req->players()) {
            m_accountToRoom[p.account_id()] = rid;
        }
    }
    // 持房间锁直接调 startGame(跑在当前 RPC 协程上, 独立栈)
    {
        BattleRoom::MutexType::Lock rlk(room->m_roomMutex);
        room->startGame();
    }
    resp->set_result(SUCCESS);
    resp->set_room_id(rid);
    SYLAR_LOG_INFO(g_logger) << "battle: enter battle room " << rid;
    if (done) {
        done->Run();
    }
}

// 射击
void BattleServiceImpl::Shoot(::google::protobuf::RpcController*,
        const ShootRpcReq* req, ResultResp* resp, ::google::protobuf::Closure* done) {
    BattleRoom::ptr room;
    {
        sylar::RWMutex::ReadLock lk(m_mutex);
        room = roomOfLocked(req->account_id());
    }
    if (!room) {
        resp->set_result(NOT_FOUND);
        resp->set_msg("not in battle");
        if (done) {
            done->Run();
        }
        return;
    }
    // 持房间锁直接调 onShoot
    {
        BattleRoom::MutexType::Lock rlk(room->m_roomMutex);
        room->onShoot(req->account_id(), req->angle(), req->force(), req->is_fly(), req->weapon_id());
    }
    resp->set_result(SUCCESS);
    if (done) {
        done->Run();
    }
}

// 移动
void BattleServiceImpl::Move(::google::protobuf::RpcController*,
        const MoveRpcReq* req, ResultResp* resp, ::google::protobuf::Closure* done) {
    BattleRoom::ptr room;
    {
        sylar::RWMutex::ReadLock lk(m_mutex);
        room = roomOfLocked(req->account_id());
    }
    if (!room) {
        resp->set_result(NOT_FOUND);
        resp->set_msg("not in battle");
        if (done) {
            done->Run();
        }
        return;
    }
    // 持房间锁直接调 onMove
    {
        BattleRoom::MutexType::Lock rlk(room->m_roomMutex);
        room->onMove(req->account_id(), req->delta_x());
    }
    resp->set_result(SUCCESS);
    if (done) {
        done->Run();
    }
}

// 跳过回合
void BattleServiceImpl::Pass(::google::protobuf::RpcController*,
        const PassRpcReq* req, ResultResp* resp, ::google::protobuf::Closure* done) {
    BattleRoom::ptr room;
    {
        sylar::RWMutex::ReadLock lk(m_mutex);
        room = roomOfLocked(req->account_id());
    }
    if (!room) {
        resp->set_result(NOT_FOUND);
        resp->set_msg("not in battle");
        if (done) {
            done->Run();
        }
        return;
    }
    // 持房间锁直接调 onPass
    {
        BattleRoom::MutexType::Lock rlk(room->m_roomMutex);
        room->onPass(req->account_id());
    }
    resp->set_result(SUCCESS);
    if (done) {
        done->Run();
    }
}

// 蓄力开始
void BattleServiceImpl::AimBegin(::google::protobuf::RpcController*,
        const PassRpcReq* req, ResultResp* resp, ::google::protobuf::Closure* done) {
    BattleRoom::ptr room;
    {
        sylar::RWMutex::ReadLock lk(m_mutex);
        room = roomOfLocked(req->account_id());
    }
    if (!room) {
        resp->set_result(NOT_FOUND);
        resp->set_msg("not in battle");
        if (done) {
            done->Run();
        }
        return;
    }
    // 持房间锁直接调 onAimBegin
    {
        BattleRoom::MutexType::Lock rlk(room->m_roomMutex);
        room->onAimBegin(req->account_id());
    }
    resp->set_result(SUCCESS);
    if (done) {
        done->Run();
    }
}

// 离开战斗: 标记淘汰 + 摘除索引 + 无人则销毁房间
void BattleServiceImpl::LeaveBattle(::google::protobuf::RpcController*,
        const LeaveBattleRpcReq* req, ResultResp* resp, ::google::protobuf::Closure* done) {
    BattleRoom::ptr room;
    uint32_t rid = 0;
    {
        sylar::RWMutex::ReadLock lk(m_mutex);
        room = roomOfLocked(req->account_id());
        if (room) {
            rid = room->roomId();
        }
    }
    if (!room) {
        resp->set_result(NOT_FOUND);
        resp->set_msg("not in battle");
        if (done) {
            done->Run();
        }
        return;
    }
    // 持房间锁直接调 onPlayerLeave
    {
        BattleRoom::MutexType::Lock rlk(room->m_roomMutex);
        room->onPlayerLeave(req->account_id());
    }
    // 摘除索引(房间锁外, 用 service 的全局锁)
    {
        sylar::RWMutex::WriteLock lk(m_mutex);
        m_accountToRoom.erase(req->account_id());
        // 房间内无人则销毁(共享指针引用归零, room 析构)
        bool any = false;
        for (const auto& kv : m_accountToRoom) {
            if (kv.second == rid) {
                any = true;
                break;
            }
        }
        if (!any) {
            // 销毁前: 持房间锁取消回合定时器 + 标记 destroying,
            // 防孤儿 timer 在 erase 后触发 nextTurn→startTurnTimer 形成泄漏
            {
                BattleRoom::MutexType::Lock rlk(room->m_roomMutex);
                room->markDestroying();
            }
            m_rooms.erase(rid);
        }
    }
    resp->set_result(SUCCESS);
    if (done) {
        done->Run();
    }
}

}  // namespace ddt
