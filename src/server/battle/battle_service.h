#ifndef __DDT_BATTLE_SERVICE_H__
#define __DDT_BATTLE_SERVICE_H__

#include <map>
#include <memory>

#include "battle_room.h"
#include "rpc.pb.h"
#include "service_base.h"
#include "sylar/core/thread.h"

namespace ddt {

// BattleService: 管理战斗房间。RPC 入口持房间锁后直接调 onXxx。
class BattleServiceImpl : public BattleService {
public:
    typedef std::shared_ptr<BattleServiceImpl> ptr;

    // tw: 全局共享时间轮, 所有房间的回合超时定时器注册其上。
    // bpush: 批量推送闭包(1 次 RPC 推多人), 房间广播用。
    // dataChannel: 战绩落库用 channel(checkGameOver 异步调 DataService.SaveGameRecord)。
    BattleServiceImpl(const ServiceConfig& cfg, PushFn push, sylar::TimeWheel::ptr tw,
                      BroadcastPushFn bpush, std::shared_ptr<sylar::rpc::RpcChannel> dataChannel);
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
    BroadcastPushFn m_bpush;                            // 批量推送(房间广播用)
    sylar::TimeWheel::ptr m_tw;                         // 全局共享时间轮
    std::shared_ptr<sylar::rpc::RpcChannel> m_data;     // data 服 channel(战绩落库)

    mutable sylar::RWMutex m_mutex;
    std::map<uint32_t, BattleRoom::ptr> m_rooms;
    std::map<uint64_t, uint32_t> m_accountToRoom;       // accountId -> roomId
    std::atomic<uint32_t> m_nextRoomId{1};
};

}  // namespace ddt

#endif
