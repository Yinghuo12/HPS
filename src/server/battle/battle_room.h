#ifndef __DDT_BATTLE_ROOM_H__
#define __DDT_BATTLE_ROOM_H__

#include <memory>
#include <string>
#include <vector>

#include "common.pb.h"   // proto 生成
#include "routing.h"
#include "service_base.h"
#include "terrain2d.h"
#include "sylar/core/thread.h"
#include "sylar/rpc/rpc_channel.h"
#include "sylar/scheduler/iomanager.h"
#include "sylar/scheduler/timewheel.h"

namespace ddt {

// 战斗内玩家(不持连接, 持路由句柄)
struct BattlePlayer {
    uint64_t accountId = 0;
    std::string name;
    TeamSide team = TEAM_RED;
    Gender gender = GENDER_NONE;   // 性别(决定战斗贴图)
    int weaponId = 1;               // 武器(1=ice_cream 2=projectile, 仅贴图不同)
    float x = 0, y = 0;
    int hp = 0, maxHp = 0;
    int angle = 45;
    int direction = 1;        // 1=右, -1=左
    uint64_t gatewayId = 0;
    bool alive = true;        // 是否存活(淘汰/离场后=false)
    int damageDealt = 0;      // 本局累计造成伤害(结算落库用)
};

// BattleRoom: 用一把 Mutex 保护房间状态, RPC 入口持锁后直接调 onXxx。
// 同一房间 RPC 入口争同一把 m_roomMutex 串行化, 各房间锁独立。
class BattleRoom : public std::enable_shared_from_this<BattleRoom> {
public:
    typedef std::shared_ptr<BattleRoom> ptr;

    // 首回合额外准备时间(ms): 客户端降落+开场巡视期间不计超时
    static constexpr uint64_t FIRST_TURN_READY_MS = 12000;
    // 射击结果后延迟切下一回合(ms): 让客户端播完弹道+爆炸再切
    static constexpr uint64_t SHOOT_RESULT_DELAY_MS = 2300;

    BattleRoom(uint32_t roomId, const ServiceConfig& cfg, PushFn push,
               sylar::TimeWheel* tw, BroadcastPushFn bpush,
               std::shared_ptr<sylar::rpc::RpcChannel> dataChannel);

    // ---- 配置房间(在 startGame 前调用) ----
    void addPlayer(uint64_t accountId, const std::string& name, TeamSide team,
                   uint64_t gatewayId, Gender gender = GENDER_NONE, int weaponId = 1);
    bool canStart() const;

    // 开局: 生成地形、按队放玩家、广播 RoomReadyNotify + 首回合 TurnStartNotify
    void startGame();

    // ---- 战斗操作(由 RPC 入口持锁后直接调用) ----
    void onShoot(uint64_t accountId, int angle, double force, bool isFly, int weaponId = 1);
    void onMove(uint64_t accountId, float deltaX);
    void onPass(uint64_t accountId);
    void onAimBegin(uint64_t accountId);   // 蓄力开始→重置回合计时
    void onTurnTimeout();
    void onPlayerLeave(uint64_t accountId);

    bool started() const {
        return m_started;
    }

    uint32_t roomId() const {
        return m_roomId;
    }

    void setMapName(const std::string& name) {
        m_mapName = name;
    }

    // 标记房间正在销毁: 置 m_destroying + 取消回合定时器(假设已持房间锁)。
    // 防孤儿 timer 在 erase 后触发 nextTurn→startTurnTimer 形成泄漏
    void markDestroying() {
        m_destroying = true;
        cancelTurnTimer();
    }

    // 房间状态锁: RPC 入口用此锁串行化同一房间的操作。
    // 用 Mutex 而非 Spinlock: onShoot 临界区含物理仿真(持锁可达毫秒级),
    // 阻塞时 futex 睡眠让出线程, 不烧 CPU。
    // 注: 锁内不可 yield(当前 broadcast 已异步化, 锁内无 yield)。
    typedef sylar::Mutex MutexType;
    MutexType m_roomMutex;

private:
    // 内部辅助(均假设已持锁, 调用方为 onXxx/startGame)
    BattlePlayer* getPlayer(uint64_t accountId);
    void broadcast(uint16_t msgId, const std::string& payload);
    void nextTurn();
    void scheduleNextTurn();   // 延迟 SHOOT_RESULT_DELAY_MS 后 nextTurn
    void checkGameOver();
    void saveGameRecordLocked(TeamSide winningTeam);   // 锁内快照 + 锁外异步 RPC 落 data
    void startTurnTimer();
    void cancelTurnTimer();
    void generateTerrain();   // 生成 2D 体素地形
    void fillPlayerState(const BattlePlayer& p, PlayerState* out);
    void fillAllPlayers(google::protobuf::RepeatedPtrField<PlayerState>* out);
    uint32_t indexOf(uint64_t accountId) const;
    int countAlive() const;

    uint32_t m_roomId;
    const ServiceConfig& m_cfg;
    PushFn m_push;
    BroadcastPushFn m_bpush;                            // 批量推送(房间广播用)
    sylar::TimeWheel* m_tw;                             // 全局共享时间轮
    std::shared_ptr<sylar::rpc::RpcChannel> m_data;     // data 服 channel(战绩落库用)
    std::string m_mapName = "rainbow";                  // 地图背景名

    std::vector<BattlePlayer> m_players;                // 多人对战(动态)
    bool m_started = false;
    uint64_t m_startTimeMs = 0;                         // startGame 时间戳(算对局 duration)

    Terrain2D m_terrain;   // 二维体素地形
    float m_wind = 0;
    uint32_t m_currentTurnIdx = 0;   // 0 或 1
    uint32_t m_turnNumber = 0;

    bool m_shootLocked = false;      // 本回合已射
    float m_moveUsed = 0;            // 本回合已移动距离
    bool m_nextTurnPending = false;  // 已有一个延迟 nextTurn 在等(防重入)

    bool m_destroying = false;       // 房间正在销毁(LeaveBattle erase 前置 true);
                                     // nextTurn/startTurnTimer/onTurnTimeout 据此短路

    std::shared_ptr<sylar::TimeWheel::Timer> m_turnTimer;   // 回合超时定时器
};

}  // namespace ddt

#endif
