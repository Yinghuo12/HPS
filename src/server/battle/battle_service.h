#ifndef __DDT_BATTLE_SERVICE_H__
#define __DDT_BATTLE_SERVICE_H__

#include <map>
#include <memory>

#include "battle_room.h"
#include "rpc.pb.h"
#include "service_base.h"
#include "sylar/core/thread.h"

namespace ddt {

// ============================================================
// BattleService: 管理战斗房间(每房一 Actor)。
// RPC 入口仅把消息 post 进房间 actor, 立即返回。
// 房间 actor 内串行处理, 通过 m_push 回 gate 推送。
// ============================================================
class BattleServiceImpl : public BattleService {
public:
    typedef std::shared_ptr<BattleServiceImpl> ptr;

    // tw: 由 battle_main 创建的全局共享时间轮, 所有房间的回合超时定时器注册其上。
    // bpush: 批量推送闭包(1次RPC推多人), 房间广播用。
    BattleServiceImpl(const ServiceConfig& cfg, PushFn push, sylar::TimeWheel::ptr tw, BroadcastPushFn bpush);
    ~BattleServiceImpl();

    // ---- RPC ----
    void EnterBattle(::google::protobuf::RpcController*, const EnterBattleReq*, EnterBattleResp*, ::google::protobuf::Closure*) override;
    void Shoot(::google::protobuf::RpcController*, const ShootRpcReq*, ResultResp*, ::google::protobuf::Closure*) override;
    void Move(::google::protobuf::RpcController*, const MoveRpcReq*, ResultResp*, ::google::protobuf::Closure*) override;
    void Pass(::google::protobuf::RpcController*, const PassRpcReq*, ResultResp*, ::google::protobuf::Closure*) override;
    void AimBegin(::google::protobuf::RpcController*, const PassRpcReq*, ResultResp*, ::google::protobuf::Closure*) override;
    void LeaveBattle(::google::protobuf::RpcController*, const LeaveBattleRpcReq*, ResultResp*, ::google::protobuf::Closure*) override;

private:
    // 找玩家所在房间(锁内)
    BattleRoom::ptr roomOfLocked(uint64_t accountId);

    const ServiceConfig& m_cfg;
    PushFn m_push;
    BroadcastPushFn m_bpush;      // 批量推送(房间广播用)
    sylar::TimeWheel::ptr m_tw;   // 全局共享时间轮(所有房间回合定时器注册其上)

    mutable sylar::RWMutex m_mutex;
    std::map<uint32_t, BattleRoom::ptr> m_rooms;
    std::map<uint64_t, uint32_t> m_accountToRoom;   // accountId -> roomId
    std::atomic<uint32_t> m_nextRoomId{1};
};

} // namespace ddt

#endif
