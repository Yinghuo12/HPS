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
    int   hp = 0, maxHp = 0;
    int   angle = 45;
    int   direction = 1;     // 1=右, -1=左
    uint64_t gatewayId = 0;
    bool  alive = true;      // 多人: 是否存活(淘汰/离场后=false)
};

// ============================================================
// BattleRoom
// 去掉了 Actor/mailbox 模型(原 Actor 把所有操作压在单个协程栈上,
// 加上 RPC 推送调用链导致栈溢出 stack smashing)。
// 现在用一把 Mutex 保护房间状态, RPC 入口(battle_service)持锁后直接调
// onXxx 方法。每个操作跑在独立的 RPC 协程上(独立栈), 彻底消除栈叠加。
//
// 串行化保证: 同一房间的 RPC 入口争同一把 m_roomMutex, 临界区内操作
// (算伤害/改坐标/组消息)是纯内存操作, 极快。回合制游戏天然串行,
// 持锁期间的其他操作等待是正确行为。
//
// 推送(broadcast)在锁内执行——如果推送 RPC 因网络 yield, 会阻塞同一
// 房间的其他操作, 但不阻塞其他房间(各房间锁独立)。且不栈溢出(独立栈)。
// ============================================================
class BattleRoom : public std::enable_shared_from_this<BattleRoom> {
public:
    typedef std::shared_ptr<BattleRoom> ptr;

    // 首回合额外准备时间(ms): 客户端降落动画 + 摄像机开场巡视期间服务端不计超时,
    // 避免巡视还没做完首回合就被跳过。加在 turn_timeout 之上。
    static constexpr uint64_t FIRST_TURN_READY_MS = 12000;
    // 射击结果(ShootResultNotify)后, 延迟多久才切下一回合(nextTurn→TurnStartNotify)。
    // 保证客户端先播完弹道+爆炸动画再收到回合切换 + 倒计时, 而非一发射就切。
    static constexpr uint64_t SHOOT_RESULT_DELAY_MS = 2300;

    BattleRoom(uint32_t roomId, const ServiceConfig& cfg, PushFn push, sylar::TimeWheel* tw, BroadcastPushFn bpush);

    // ---- 配置房间(在 startGame 前调用) ----
    void addPlayer(uint64_t accountId, const std::string& name, TeamSide team, uint64_t gatewayId, Gender gender = GENDER_NONE, int weaponId = 1);
    bool canStart() const;

    // ---- 开局: 生成地形、按队放玩家、置 m_started、广播 RoomReadyNotify + 首回合 TurnStartNotify。
    // 直接调用(在 RPC 入口持锁后调用), 不再 post 进 actor。
    void startGame();

    // ---- 战斗操作(由 RPC 入口持锁后直接调用) ----
    void onShoot(uint64_t accountId, int angle, double force, bool isFly, int weaponId = 1);
    void onMove(uint64_t accountId, float deltaX);
    void onPass(uint64_t accountId);
    void onAimBegin(uint64_t accountId);   // 蓄力开始→重置回合计时
    void onTurnTimeout();
    void onPlayerLeave(uint64_t accountId);

    bool started() const { return m_started; }
    uint32_t roomId() const { return m_roomId; }
    void setMapName(const std::string& name) { m_mapName = name; }

    // 标记房间正在销毁: 置 m_destroying + 取消回合定时器(假设已持房间锁)。
    // 由 battle_service 在 m_rooms.erase 前调用, 杜绝孤儿 timer 在 erase 后仍触发
    // nextTurn→startTurnTimer 形成泄漏永动机。
    void markDestroying() {
        m_destroying = true;
        cancelTurnTimer();
    }

    // 房间状态锁: RPC 入口(battle_service)用此锁串行化同一房间的操作。
    // 用 Mutex(pthread_mutex)而非 Spinlock: onShoot 临界区含物理仿真
    // (computeHitPoint2D ~1500 次循环 + removeCircle ~万级格子), 持锁可达毫秒级。
    // Spinlock 阻塞时烧 CPU(自旋), 换 Mutex 阻塞时 futex 睡眠让出线程, 不占 CPU。
    // 注: pthread_mutex 非协程感知, 锁内不可 yield(当前 broadcast 已异步化, 锁内无 yield)。
    typedef sylar::Mutex MutexType;
    MutexType m_roomMutex;

private:
    // 内部辅助(均假设已持锁, 调用方为 onXxx/startGame)
    BattlePlayer* getPlayer(uint64_t accountId);
    void broadcast(uint16_t msgId, const std::string& payload);
    void nextTurn();
    void scheduleNextTurn();   // 延迟 SHOOT_RESULT_DELAY_MS 后 nextTurn(让客户端播完弹道再切)
    void checkGameOver();
    void startTurnTimer();
    void cancelTurnTimer();
    void generateTerrain();   // 生成 2D 体素地形(替代旧 generateHeightMap)
    void fillPlayerState(const BattlePlayer& p, PlayerState* out);
    void fillAllPlayers(google::protobuf::RepeatedPtrField<PlayerState>* out);
    uint32_t indexOf(uint64_t accountId) const;
    int countAlive() const;

    uint32_t m_roomId;
    const ServiceConfig& m_cfg;
    PushFn m_push;
    BroadcastPushFn m_bpush;        // 批量推送(房间广播用, 1次RPC替代N次)
    sylar::TimeWheel* m_tw;         // 全局共享时间轮(回合定时器注册其上)
    std::string m_mapName = "rainbow";   // 地图背景名

    std::vector<BattlePlayer> m_players;   // 多人对战(动态)
    bool m_started = false;

    Terrain2D m_terrain;   // 二维体素地形(替代旧 1D heightMap)
    float m_wind = 0;
    uint32_t m_currentTurnIdx = 0;  // 0 或 1
    uint32_t m_turnNumber = 0;

    bool m_shootLocked = false;     // 本回合已射(同一玩家移动锁定)
    float m_moveUsed = 0;           // 本回合已移动距离
    bool m_nextTurnPending = false; // 已有一个延迟 nextTurn 在等(scheduleNextTurn 防重入)

    bool m_destroying = false;      // 房间正在销毁(LeaveBattle erase 前置 true);
                                    // nextTurn/startTurnTimer/onTurnTimeout 据此短路,
                                    // 杜绝孤儿 timer 在房间 erase 后仍注册新 timer 导致泄漏

    // 回合超时定时器(sylar Timer)
    std::shared_ptr<sylar::TimeWheel::Timer> m_turnTimer;
};

} // namespace ddt

#endif
