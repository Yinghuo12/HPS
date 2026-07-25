#include "battle_room.h"

#include <algorithm>
#include <cmath>

#include "ddt_physics.h"
#include "gate.pb.h"
#include "msg_id.h"
#include "rpc.pb.h"
#include "sylar/core/log.h"
#include "sylar/rpc/rpc_controller.h"
#include "sylar/scheduler/iomanager.h"
#include "sylar/scheduler/timer.h"

namespace ddt {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("rpc");

// 构造战斗房间
BattleRoom::BattleRoom(uint32_t roomId, const ServiceConfig& cfg, PushFn push,
                       sylar::TimeWheel* tw, BroadcastPushFn bpush,
                       std::shared_ptr<sylar::rpc::RpcChannel> dataChannel)
    : m_roomId(roomId)
    , m_cfg(cfg)
    , m_push(push)
    , m_bpush(bpush)
    , m_tw(tw)
    , m_data(dataChannel) {
}

// 添加参战玩家
void BattleRoom::addPlayer(uint64_t accountId, const std::string& name, TeamSide team,
                           uint64_t gatewayId, Gender gender, int weaponId) {
    BattlePlayer p;
    p.accountId = accountId;
    p.name = name;
    p.team = team;
    p.gender = gender;
    p.weaponId = weaponId;
    p.gatewayId = gatewayId;
    p.hp = m_cfg.start_hp;
    p.maxHp = m_cfg.start_hp;
    p.angle = 45;
    p.direction = (team == TEAM_RED) ? 1 : -1;
    p.alive = true;
    p.damageDealt = 0;   // 初始化累计伤害
    m_players.push_back(p);
}

// 是否满足开局条件
bool BattleRoom::canStart() const {
    return (int)m_players.size() >= m_cfg.min_players && !m_started;
}

// 开局: 所有玩家放出生点 + 广播 RoomReadyNotify
void BattleRoom::startGame() {
    if (m_started || (int)m_players.size() < m_cfg.min_players) {
        return;
    }

    m_started = true;
    m_turnNumber = 0;
    m_startTimeMs = sylar::GetCurrentMS();   // 记开局时间戳(算 duration)
    generateTerrain();
    m_wind = PhysicsEngine::generateWind();
    m_shootLocked = false;
    m_moveUsed = 0;

    // 按队放玩家: 红队围绕 red_spawn_x 间隔分布, 蓝队围绕 blue_spawn_x 间隔分布
    int redIdx = 0, blueIdx = 0;
    float spacing = 60.0f;   // 同队玩家间距
    for (auto& p : m_players) {
        int baseX = (p.team == TEAM_RED) ? (int)m_cfg.red_spawn_x : (int)m_cfg.blue_spawn_x;
        int offset = (p.team == TEAM_RED) ? redIdx++ : blueIdx++;
        int sx = baseX + (int)(offset * spacing);
        sx = std::max(0, std::min(sx, m_terrain.width() - 1));
        p.x = (float)sx;
        p.y = m_terrain.columnHeight(sx);
        p.hp = p.maxHp = m_cfg.start_hp;
        p.angle = 45;
        p.direction = (p.team == TEAM_RED) ? 1 : -1;
        p.alive = true;
    }

    // 广播 RoomReadyNotify(含物理参数 + 全量玩家 + 地图名)
    RoomReadyNotify rrn;
    rrn.set_room_id(m_roomId);
    rrn.set_wind(m_wind);
    rrn.set_map_name(m_mapName);
    if (!m_players.empty()) {
        rrn.set_first_turn_id(m_players[0].accountId);
    }
    fillAllPlayers(rrn.mutable_players());
    auto* pp = rrn.mutable_physics_params();
    pp->set_air_factor(m_cfg.air_factor);
    pp->set_wind_factor(m_cfg.wind_factor);
    pp->set_gravity_factor(m_cfg.gravity_factor);
    pp->set_force_factor(m_cfg.force_factor);
    rrn.set_max_move_per_turn((float)m_cfg.max_move_per_turn);
    // 下发 2D 体素位图: 客户端用其做精确碰撞/挖坑/贴地表
    rrn.set_terrain_bitmap(m_terrain.bitmap());
    rrn.set_terrain_w(m_terrain.width());
    rrn.set_terrain_h(m_terrain.height());
    std::string rrnPayload;
    rrn.SerializeToString(&rrnPayload);
    broadcast(MSG_ROOM_READY_NOTIFY, rrnPayload);

    SYLAR_LOG_INFO(g_logger) << "room " << m_roomId << " startGame: " << m_players.size()
        << " players, wind=" << m_wind;

    m_currentTurnIdx = 0;
    nextTurn();
}

// 查找玩家在 m_players 中的下标(找不到返回 UINT32_MAX)
uint32_t BattleRoom::indexOf(uint64_t accountId) const {
    for (size_t i = 0; i < m_players.size(); ++i) {
        if (m_players[i].accountId == accountId) {
            return (uint32_t)i;
        }
    }
    return UINT32_MAX;
}

// 取玩家指针(未找到返回 nullptr)
BattlePlayer* BattleRoom::getPlayer(uint64_t accountId) {
    uint32_t i = indexOf(accountId);
    return (i == UINT32_MAX) ? nullptr : &m_players[i];
}

// 统计存活玩家数
int BattleRoom::countAlive() const {
    int n = 0;
    for (const auto& p : m_players) {
        if (p.alive && p.hp > 0) {
            ++n;
        }
    }
    return n;
}

// 填充单个玩家状态到 proto
void BattleRoom::fillPlayerState(const BattlePlayer& p, PlayerState* out) {
    out->set_account_id(p.accountId);
    out->set_name(p.name);
    out->set_x(p.x);
    out->set_y(p.y);
    out->set_hp(p.hp);
    out->set_max_hp(p.maxHp);
    out->set_angle(p.angle);
    out->set_direction(p.direction);
    out->set_team(p.team);
    out->set_gender(p.gender);
}

// 填充全部玩家状态到 proto
void BattleRoom::fillAllPlayers(google::protobuf::RepeatedPtrField<PlayerState>* out) {
    for (const auto& p : m_players) {
        fillPlayerState(p, out->Add());
    }
}

// 异步广播: 投递到独立协程执行, 避免撑爆当前 RPC 协程栈
void BattleRoom::broadcast(uint16_t msgId, const std::string& payload) {
    // 关键消息(射击结果/回合切换/游戏结束)同步推送, 不走 schedule:
    // 移动期间高频 MoveNotify 的 schedule 协程会积压在 IOManager 队列里,
    // 若 ShootResult 也 schedule, 会排在 MoveNotify 后面 → 射击动画延迟启动。
    bool urgent = (msgId == MSG_SHOOT_RESULT_NOTIFY
                 || msgId == MSG_TURN_START_NOTIFY
                 || msgId == MSG_GAME_OVER_NOTIFY);
    // 优先用批量推送闭包(1 次 RPC 推多人)
    if (m_bpush) {
        std::vector<uint64_t> ids;
        ids.reserve(m_players.size());
        for (const auto& p : m_players) {
            ids.push_back(p.accountId);
        }
        auto fn = m_bpush;
        if (urgent) {
            fn(ids, msgId, payload);   // 同步: 立即发出, 不入队
        } else {
            sylar::IOManager::GetThis()->schedule(
                [fn, ids, msgId, payload]() { fn(ids, msgId, payload); });
        }
        return;
    }
    // 兜底: 无批量闭包时退回逐人单推(也异步化)
    if (!m_push) {
        return;
    }
    for (const auto& p : m_players) {
        RoutingHandle h(p.accountId, p.gatewayId);
        auto fn = m_push;
        if (urgent) {
            fn(h, msgId, payload);   // 同步
        } else {
            sylar::IOManager::GetThis()->schedule(
                [fn, h, msgId, payload]() { fn(h, msgId, payload); });
        }
    }
}

// 生成 2D 体素地形(地面较低, 留大天空给弹道飞行)
void BattleRoom::generateTerrain() {
    float baseH = (float)m_cfg.spawn_y * 0.22f;   // 地面基础高度(较低)
    int th = (int)(baseH * 1.6f) + 40;             // 地形 Y 维度: 覆盖最高地表并留余量
    m_terrain.generate(m_cfg.world_length, th, baseH);
}

// 射击处理: 算弹道 + AOE 伤害 + 挖坑 + 广播结果
void BattleRoom::onShoot(uint64_t accountId, int angle, double force, bool isFly, int weaponId) {
    if (!m_started) {
        return;
    }
    if (m_shootLocked) {
        return;
    }
    if (indexOf(accountId) != m_currentTurnIdx) {
        return;
    }
    m_shootLocked = true;

    BattlePlayer* shooter = getPlayer(accountId);
    if (!shooter) {
        return;
    }

    // 角度/力度范围校验(防作弊)
    if (angle < m_cfg.min_angle || angle > m_cfg.max_angle) {
        angle = std::max(m_cfg.min_angle, std::min(m_cfg.max_angle, angle));
    }
    if (force < m_cfg.min_force) {
        force = m_cfg.min_force;
    }
    if (force > m_cfg.max_force) {
        force = m_cfg.max_force;
    }
    shooter->angle = angle;

    if (isFly) {
        // 纸飞机: 算飞行弹道(视觉), 落地后传送到落点, 无伤害
        ddt::PhysicsParams pp;
        pp.air_factor = m_cfg.air_factor;
        pp.wind_factor = m_cfg.wind_factor;
        pp.gravity_factor = m_cfg.gravity_factor;
        pp.force_factor = m_cfg.force_factor;
        // 角度: 叠加坡度(与普通弹一致, 支持反抛)
        float slopeDegFly = 0.0f;
        {
            int sixFly = (int)shooter->x;
            if (sixFly >= 3 && sixFly < m_terrain.width() - 3) {
                slopeDegFly = ddt::PhysicsEngine::getSlopeAngle2D((float)sixFly, m_terrain);
            }
        }
        int physAngle = (shooter->direction < 0)
            ? (int)(180 - angle + slopeDegFly)
            : (int)(angle + slopeDegFly);
        // 起点: 炮口; 坑底时抬高到坑沿上方(防穿墙)
        float originX = shooter->x;
        double originY = shooter->y + 60.0;
        int sixFlyFloor = (int)shooter->x;
        if (sixFlyFloor >= 0 && sixFlyFloor < m_terrain.width()) {
            double surfaceY = m_terrain.columnHeight(sixFlyFloor);
            if (originY < surfaceY + 10.0) {
                originY = surfaceY + 10.0;
            }
        }
        auto res = ddt::PhysicsEngine::computeHitPoint2D(
            originX, originY, physAngle, force, m_wind, pp, m_terrain,
            m_cfg.world_length, (int)(m_cfg.spawn_y * 1.2), m_cfg.physics_dt);
        // 仅当真正命中地形(未出界)才传送; 出界则玩家留在原地
        if (!res.hit_offscreen) {
            shooter->x = res.hit_x;
            shooter->y = res.hit_y;
        }
        // 起点=发射前炮口位置, 命中=落点, isFly=true → 客户端播完飞行轨迹后传送
        ShootResultNotify n;
        n.set_shooter_id(shooter->accountId);
        n.set_angle(angle);
        n.set_force(force);
        n.set_wind(m_wind);
        n.set_hit_x(res.hit_x);
        n.set_hit_y(res.hit_y);
        n.set_start_x(originX);
        n.set_start_y(originY);
        n.set_direction(shooter->direction);
        n.set_is_fly(true);
        n.set_weapon_id(weaponId);
        fillAllPlayers(n.mutable_updated_players());
        std::string payload;
        n.SerializeToString(&payload);
        broadcast(MSG_SHOOT_RESULT_NOTIFY, payload);
        cancelTurnTimer();
        checkGameOver();
        if (m_started) {
            scheduleNextTurn();   // 延迟切回合: 让客户端播完弹道再切
        }
        return;
    }

    // 计算弹道(起点: 炮口 = 脚底+60, Y 向上)
    // 角度: 叠加玩家所在地形坡度(与客户端炮管视觉一致), 支持反抛。
    // 朝右: base+slope; 朝左: 180-base-slope
    float slopeDeg = 0.0f;
    {
        int six = (int)shooter->x;
        if (six >= 3 && six < m_terrain.width() - 3) {
            slopeDeg = ddt::PhysicsEngine::getSlopeAngle2D((float)six, m_terrain);
        }
    }
    int physAngleShoot = (shooter->direction < 0)
        ? (int)(180 - angle + slopeDeg)
        : (int)(angle + slopeDeg);
    // 弹道起点: 炮口 = 脚底+60; 玩家在坑底时抬高到坑沿上方, 防穿墙
    double originY = shooter->y + 60.0;
    int sixFloor = (int)shooter->x;
    if (sixFloor >= 0 && sixFloor < m_terrain.width()) {
        double surfaceY = m_terrain.columnHeight(sixFloor);
        if (originY < surfaceY + 10.0) {
            originY = surfaceY + 10.0;
        }
    }
    ddt::PhysicsParams pp;
    pp.air_factor = m_cfg.air_factor;
    pp.wind_factor = m_cfg.wind_factor;
    pp.gravity_factor = m_cfg.gravity_factor;
    pp.force_factor = m_cfg.force_factor;
    auto res = ddt::PhysicsEngine::computeHitPoint2D(
        shooter->x, originY, physAngleShoot, force, m_wind, pp, m_terrain,
        m_cfg.world_length, (int)(m_cfg.spawn_y * 1.2), m_cfg.physics_dt);

    // 爆炸 AOE 模型: 炮弹只与地形碰撞, 落地后在 blast_radius 内对所有玩家造成伤害
    // (按距离衰减), 同时挖坑(降低高度), 受影响玩家视觉下落由客户端处理
    bool hitPlayer = false;
    int damage = 0;
    uint64_t hitAccountId = 0;
    ShootResultNotify::DamageType dmgType = ShootResultNotify::NORMAL;

    if (res.hit_offscreen == false) {
        // ★calculateDamage AOE(挖坑前算!): 用玩家当前位置(爆炸前的脚位)算距离,
        // 否则挖坑后 y 下降距落点变远, 伤害会被错误衰减到 0
        for (auto& p : m_players) {
            if (!p.alive || p.hp <= 0) {
                continue;
            }
            int dmg = PhysicsEngine::calculateDamage(res.hit_x, res.hit_y, p.x, p.y,
                                                     m_cfg.base_damage, (float)m_cfg.blast_radius);
            if (dmg > 0) {
                int roll = rand() % 100;
                ShootResultNotify::DamageType dt = ShootResultNotify::NORMAL;
                int finalDmg = dmg;
                if (roll < 15) {
                    dt = ShootResultNotify::CRITICAL;
                    finalDmg = (int)(dmg * 1.5);
                } else if (roll < 30) {
                    dt = ShootResultNotify::BLOCK;
                    finalDmg = dmg / 2;
                }
                // 友军伤害: 自己和同队受 AOE 伤害但不致死(HP 最低 1); 敌方可致死。
                bool isFriendly = (p.accountId == shooter->accountId || p.team == shooter->team);
                if (isFriendly) {
                    p.hp = std::max(1, p.hp - finalDmg);
                } else {
                    p.hp = std::max(0, p.hp - finalDmg);
                }
                shooter->damageDealt += finalDmg;   // 累计射手本局伤害(结算落库用)
                // 记录第一个受影响玩家为主目标(协议单字段)
                if (!hitPlayer) {
                    hitPlayer = true;
                    hitAccountId = p.accountId;
                    damage = finalDmg;
                    dmgType = dt;
                }
            }
        }
        // 落点爆炸: 在 2D 体素地形上挖圆坑(只挖圆内格子)
        m_terrain.removeCircle(res.hit_x, res.hit_y, (float)m_cfg.blast_radius);
        // 爆炸挖坑后: 所有玩家贴到新地表(脚随坑底下降)
        for (auto& p : m_players) {
            if (!p.alive) {
                continue;
            }
            int pix = (int)p.x;
            if (pix >= 0 && pix < m_terrain.width()) {
                p.y = m_terrain.columnHeight(pix);
            }
            // 该列完全打穿(columnHeight==0): 掉出地图坠落死亡(不分敌友)。
            // 友军保护只限爆炸伤害(HP 保 1), 坠落照死。
            if (p.y <= 0.0f) {
                p.alive = false;
                p.hp = 0;
            }
        }
    }

    // 构造结果通知
    ShootResultNotify n;
    n.set_shooter_id(shooter->accountId);
    n.set_angle(angle);
    n.set_force(force);
    n.set_wind(m_wind);
    n.set_hit_x(res.hit_x);
    n.set_hit_y(res.hit_y);
    n.set_hit_player(hitPlayer);
    n.set_hit_account_id(hitAccountId);
    n.set_damage(damage);
    n.set_damage_type(dmgType);
    n.set_start_x(shooter->x);
    n.set_start_y(originY);   // 发实际弹道起点(炮口, 可能被抬高)
    n.set_direction(shooter->direction);
    n.set_is_fly(false);
    n.set_weapon_id(weaponId);
    fillAllPlayers(n.mutable_updated_players());

    std::string payload;
    n.SerializeToString(&payload);
    broadcast(MSG_SHOOT_RESULT_NOTIFY, payload);

    cancelTurnTimer();
    checkGameOver();
    if (m_started) {
        scheduleNextTurn();   // 延迟切回合: 让客户端播完弹道再切
    }
}

// 移动处理: 限距 + 重力爬坡限制 + 贴地表
void BattleRoom::onMove(uint64_t accountId, float deltaX) {
    if (!m_started || m_shootLocked) {
        return;
    }
    if (indexOf(accountId) != m_currentTurnIdx) {
        return;
    }
    BattlePlayer* p = getPlayer(accountId);
    if (!p) {
        return;
    }
    float remain = (float)m_cfg.max_move_per_turn - m_moveUsed;
    if (remain <= 0) {
        return;
    }
    // 限制移动距离在本回合剩余额度内(正负方向都限)
    float actual = deltaX;
    if (actual > remain) {
        actual = remain;
    }
    if (actual < -remain) {
        actual = -remain;
    }
    // 重力限制: 目标位置地面比当前位置高超过 80(陡坡/高墙)则爬不上去
    float newX = p->x + actual;
    int curIx = (int)p->x;
    int newIx = (int)newX;
    if (curIx >= 0 && curIx < m_terrain.width() && newIx >= 0 && newIx < m_terrain.width()) {
        float heightDiff = m_terrain.columnHeight(newIx) - m_terrain.columnHeight(curIx);
        if (heightDiff > 80.0f) {
            // 太陡爬不上去, 不移动(回退)
            return;
        }
    }
    p->x = newX;
    m_moveUsed += std::fabs(actual);
    // 贴到新地表
    int pix = (int)p->x;
    if (pix >= 0 && pix < m_terrain.width()) {
        p->y = m_terrain.columnHeight(pix);
    }
    // 移动方向决定面朝向(向右 dir=1, 向左 dir=-1)
    if (actual > 0.001f) {
        p->direction = 1;
    } else if (actual < -0.001f) {
        p->direction = -1;
    }

    // 该列完全打穿(columnHeight==0): 玩家走上去坠落死亡(允许自杀)
    bool fellToDeath = (p->y <= 0.0f);
    if (fellToDeath) {
        p->alive = false;
        p->hp = 0;
    }

    MoveNotify n;
    n.set_account_id(p->accountId);
    n.set_new_x(p->x);
    std::string payload;
    n.SerializeToString(&payload);
    broadcast(MSG_MOVE_NOTIFY, payload);

    // 坠落死亡: 客户端用本地 heightMap 算 groundY<=0 触发坠落动画,
    // 服务端同步判死亡 + 检查游戏结束(可能只剩对方一人 → GameOver)
    if (fellToDeath) {
        cancelTurnTimer();
        checkGameOver();
        if (m_started) {
            scheduleNextTurn();   // 延迟切回合: 让客户端播完坠落动画再切
        }
    }
}

// 跳过回合
void BattleRoom::onPass(uint64_t accountId) {
    if (!m_started) {
        return;
    }
    if (indexOf(accountId) != m_currentTurnIdx) {
        return;
    }
    cancelTurnTimer();
    nextTurn();
}

// 蓄力开始: 重置回合计时器(给蓄力预留完整时间)
void BattleRoom::onAimBegin(uint64_t accountId) {
    if (!m_started || m_shootLocked) {
        return;
    }
    if (indexOf(accountId) != m_currentTurnIdx) {
        return;
    }
    cancelTurnTimer();
    startTurnTimer();
}

// 回合超时: 自动跳过
void BattleRoom::onTurnTimeout() {
    if (m_destroying || !m_started) {
        return;
    }
    SYLAR_LOG_INFO(g_logger) << "room " << m_roomId << " turn timeout, auto-pass";
    nextTurn();
}

// 玩家离开: 标记淘汰 + 通知房内 + 检查游戏结束
void BattleRoom::onPlayerLeave(uint64_t accountId) {
    uint32_t i = indexOf(accountId);
    if (i == UINT32_MAX) {
        return;
    }
    // 标记为淘汰(不 erase, 保持索引稳定)
    m_players[i].alive = false;
    m_players[i].hp = 0;
    // 通知所有人有人离开
    OpponentLeftNotify n;
    n.set_account_id(accountId);
    std::string payload;
    n.SerializeToString(&payload);
    broadcast(MSG_OPPONENT_LEFT_NOTIFY, payload);
    cancelTurnTimer();
    // 检查是否游戏结束(剩余存活≤1)
    checkGameOver();
    if (m_started) {
        nextTurn();
    }
}

// 延迟切回合: 保证客户端先播完弹道+爆炸动画, 再收到 TurnStartNotify
void BattleRoom::scheduleNextTurn() {
    if (m_destroying || m_nextTurnPending) {
        return;
    }
    m_nextTurnPending = true;
    auto self = shared_from_this();
    m_tw->addTimer(SHOOT_RESULT_DELAY_MS, [self]() {
        BattleRoom::MutexType::Lock lk(self->m_roomMutex);
        if (self->m_destroying) {
            return;
        }
        self->m_nextTurnPending = false;
        if (self->m_started) {
            self->nextTurn();
        }
    }, false);
}

// 切到下一回合(轮转 + 跳过已淘汰)
void BattleRoom::nextTurn() {
    if (m_destroying || m_players.empty()) {
        return;
    }
    size_t attempts = 0;
    do {
        m_currentTurnIdx = (m_currentTurnIdx + 1) % m_players.size();
        ++attempts;
    } while (!m_players[m_currentTurnIdx].alive && attempts <= m_players.size());

    m_turnNumber++;
    m_shootLocked = false;
    m_moveUsed = 0;
    m_wind = PhysicsEngine::generateWind();

    TurnStartNotify n;
    n.set_turn_account_id(m_players[m_currentTurnIdx].accountId);
    n.set_wind(m_wind);
    n.set_turn_number(m_turnNumber);
    fillAllPlayers(n.mutable_players());
    std::string payload;
    n.SerializeToString(&payload);
    broadcast(MSG_TURN_START_NOTIFY, payload);

    startTurnTimer();
}

// 检查游戏结束: 存活≤1 则结束 + 广播 GameOver + 落库战绩
void BattleRoom::checkGameOver() {
    if (!m_started) {
        return;   // 防重入: 游戏已结束不再处理
    }
    // 多人胜负: 存活≤1 则结束
    int alive = countAlive();
    if (alive <= 1) {
        GameOverNotify n;
        n.set_reason("battle over");
        TeamSide winningTeam = TEAM_RED;   // 默认
        bool foundWinner = false;
        // 找唯一存活者
        for (const auto& p : m_players) {
            if (p.alive && p.hp > 0) {
                n.set_winner_account_id(p.accountId);
                n.set_winning_team(p.team);
                winningTeam = p.team;
                foundWinner = true;
                break;
            }
        }
        // 全员阵亡(同时坠落/同归于尽): 算当前出手方胜利。
        // 出手方击杀了对手(或打穿了地形致其坠落), 自己也死了但判赢。
        if (!foundWinner && !m_players.empty()) {
            const auto& shooter = m_players[m_currentTurnIdx];
            winningTeam = shooter.team;
            n.set_winner_account_id(shooter.accountId);
            n.set_winning_team(shooter.team);
        }
        std::string payload;
        n.SerializeToString(&payload);
        broadcast(MSG_GAME_OVER_NOTIFY, payload);
        m_started = false;
        cancelTurnTimer();

        saveGameRecordLocked(winningTeam);
    }
}

// 战绩落库: 锁内快照 + 锁外异步 RPC 调 data.SaveGameRecord
void BattleRoom::saveGameRecordLocked(TeamSide winningTeam) {
    if (!m_data) {
        return;   // 未注入 data channel, 跳过
    }
    auto self = shared_from_this();
    uint64_t nowMs = sylar::GetCurrentMS();
    int duration = (int)((nowMs - m_startTimeMs) / 1000);   // 秒

    // 锁内快照(checkGameOver 已持锁, 此处构造 req 不再上锁)
    ddt::GameRecordReq req;
    req.set_winning_team(winningTeam);
    req.set_duration(duration);
    for (const auto& p : m_players) {
        auto* s = req.add_players();
        s->set_account_id(p.accountId);
        s->set_team(p.team);
        s->set_is_winner(p.alive && p.hp > 0 && p.team == winningTeam);
        s->set_damage_dealt(p.damageDealt);
    }

    // 锁外 schedule: RPC 调用链深, 必须在独立协程上执行避免栈溢出
    sylar::IOManager::GetThis()->schedule([self, req]() {
        try {
            ddt::DataService::Stub stub(self->m_data.get());
            sylar::rpc::RpcController ctrl;
            ddt::ResultResp resp;
            stub.SaveGameRecord(&ctrl, &req, &resp, nullptr);
            if (ctrl.Failed()) {
                SYLAR_LOG_WARN(g_logger) << "battle: save game record fail room=" << self->m_roomId
                    << " err=" << ctrl.ErrorText();
            } else {
                SYLAR_LOG_INFO(g_logger) << "battle: game record saved room=" << self->m_roomId
                    << " players=" << req.players_size();
            }
        } catch (const std::exception& e) {
            SYLAR_LOG_WARN(g_logger) << "battle: save game record exception: " << e.what();
        }
    });
}

// 注册回合超时定时器
void BattleRoom::startTurnTimer() {
    if (m_destroying) {
        return;   // 房间销毁中: 不再注册新 timer(防孤儿 timer 泄漏)
    }
    auto self = shared_from_this();   // shared_ptr 保活: timer 回调期间 room 不被析构
    // 首回合(m_turnNumber==1): 客户端要做降落+开场巡视(约 8~10s),
    // 故首回合额外给开局准备缓冲(FIRST_TURN_READY_MS), 之后用标准 turn_timeout
    uint64_t ms = (uint64_t)m_cfg.turn_timeout * 1000;
    if (m_turnNumber == 1) {
        ms += FIRST_TURN_READY_MS;
    }
    // TimeWheel 回调跑在独立协程上, 直接持锁调 onTurnTimeout
    m_turnTimer = m_tw->addTimer(ms, [self]() {
        BattleRoom::MutexType::Lock lk(self->m_roomMutex);
        self->onTurnTimeout();
    }, false);
}

// 取消回合定时器
void BattleRoom::cancelTurnTimer() {
    if (m_turnTimer) {
        m_tw->cancel(m_turnTimer);
        m_turnTimer.reset();
    }
}

}  // namespace ddt
