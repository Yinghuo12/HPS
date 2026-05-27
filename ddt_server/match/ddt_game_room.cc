#include "ddt_game_room.h"
#include "ddt_physics.h"
#include "ddt_config.h"
#include "ddt.pb.h"
#include "sylar/log.h"
#include "sylar/iomanager.h"
#include <cstdlib>
#include <cmath>

namespace ddt {

static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

GameRoom::GameRoom(uint32_t room_id, const std::string& name, int maxPlayers)
    : m_roomId(room_id), m_roomName(name.empty() ? "Room " + std::to_string(room_id) : name),
      m_maxPlayers(maxPlayers) {
    m_players.resize(m_maxPlayers);
}

bool GameRoom::addPlayer(Player::ptr player, ddt::TeamSide team) {
    if (m_gameStarted) return false;
    for (int i = 0; i < m_maxPlayers; i++) {
        if (!m_players[i]) {
            m_players[i] = player;
            m_playerCount++;
            m_playerTeam[player->getId()] = team;
            m_playerReady[player->getId()] = false;
            return true;
        }
    }
    return false;
}

bool GameRoom::hasPlayer(uint32_t player_id) const {
    for (int i = 0; i < m_maxPlayers; i++) {
        if (m_players[i] && m_players[i]->getId() == player_id) return true;
    }
    return false;
}

Player::ptr GameRoom::getPlayer(uint32_t player_id) const {
    for (int i = 0; i < m_maxPlayers; i++) {
        if (m_players[i] && m_players[i]->getId() == player_id) return m_players[i];
    }
    return nullptr;
}

Player::ptr GameRoom::getOtherPlayer(uint32_t my_id) const {
    for (int i = 0; i < m_maxPlayers; i++) {
        if (m_players[i] && m_players[i]->getId() != my_id) return m_players[i];
    }
    return nullptr;
}

ddt::TeamSide GameRoom::getPlayerTeam(uint32_t player_id) const {
    auto it = m_playerTeam.find(player_id);
    if (it != m_playerTeam.end()) return it->second;
    return ddt::TEAM_RED;
}

void GameRoom::removePlayer(uint32_t player_id) {
    for (int i = 0; i < m_maxPlayers; i++) {
        if (m_players[i] && m_players[i]->getId() == player_id) {
            if (m_gameStarted) {
                cancelTurnTimer();
                auto other = getOtherPlayer(player_id);
                if (other) {
                    ddt::GameMessage msg;
                    auto* nty = msg.mutable_opponent_left_notify();
                    nty->set_player_id(player_id);
                    std::string data;
                    msg.SerializeToString(&data);
                    other->sendMessage(data);
                }
            }
            m_players[i].reset();
            m_playerCount--;
            m_playerTeam.erase(player_id);
            m_playerReady.erase(player_id);
            m_gameStarted = false;
            return;
        }
    }
}

void GameRoom::setReady(uint32_t player_id, bool ready) {
    m_playerReady[player_id] = ready;

    ddt::GameMessage msg;
    auto* nty = msg.mutable_ready_notify();
    nty->set_player_id(player_id);
    nty->set_ready(ready);
    std::string data;
    msg.SerializeToString(&data);
    broadcastMessage(data);

    SYLAR_LOG_INFO(g_logger) << "Player " << player_id << " "
        << (ready ? "ready" : "unready") << " in room " << m_roomId;

    if (canStart()) {
        startGame();
    }
}

bool GameRoom::switchTeam(uint32_t player_id, ddt::TeamSide new_team) {
    if (m_gameStarted) return false;
    if (!hasPlayer(player_id)) return false;

    int targetCount = 0;
    for (auto& kv : m_playerTeam) {
        if (kv.second == new_team) targetCount++;
    }
    if (targetCount >= m_maxPlayers / 2) return false;

    m_playerTeam[player_id] = new_team;
    m_playerReady[player_id] = false;

    ddt::GameMessage msg;
    auto* notify = msg.mutable_room_update_notify();
    *notify->mutable_room_info() = getRoomInfo();
    std::string data;
    msg.SerializeToString(&data);
    broadcastMessage(data);

    SYLAR_LOG_INFO(g_logger) << "Player " << player_id
        << " switched to " << (new_team == TEAM_RED ? "RED" : "BLUE")
        << " in room " << m_roomId;
    return true;
}

bool GameRoom::canStart() const {
    if (m_playerCount < 2) return false;
    if (m_gameStarted) return false;

    // Check each team has at least one player
    bool hasRed = false, hasBlue = false;
    for (int i = 0; i < m_maxPlayers; i++) {
        if (!m_players[i]) continue;
        auto it = m_playerTeam.find(m_players[i]->getId());
        if (it != m_playerTeam.end()) {
            if (it->second == ddt::TEAM_RED) hasRed = true;
            else hasBlue = true;
        }
    }
    if (!hasRed || !hasBlue) return false;

    // Check all players are ready
    for (int i = 0; i < m_maxPlayers; i++) {
        if (!m_players[i]) continue;
        auto it = m_playerReady.find(m_players[i]->getId());
        if (it == m_playerReady.end() || !it->second) return false;
    }
    return true;
}

void GameRoom::startGame() {
    if (m_playerCount < 2) return;

    auto& cfg = GameConfig::Instance();

    m_gameStarted = true;
    m_turnNumber = 0;
    m_wind = PhysicsEngine::generateWind();
    m_playerMoveUsed.clear();

    // Generate server-side heightmap
    generateHeightMap();

    // Collect active players into slots for the 1v1 game
    Player::ptr p1, p2;
    int slot = 0;
    for (int i = 0; i < m_maxPlayers && slot < 2; i++) {
        if (m_players[i]) {
            if (slot == 0) p1 = m_players[i];
            else p2 = m_players[i];
            slot++;
        }
    }
    if (!p1 || !p2) { m_gameStarted = false; return; }

    // Set positions based on team
    auto team1 = m_playerTeam[p1->getId()];
    if (team1 == ddt::TEAM_RED) {
        p1->setPosition(cfg.red_spawn_x, cfg.spawn_y);
        p1->setDirection(1);
        p2->setPosition(cfg.blue_spawn_x, cfg.spawn_y);
        p2->setDirection(0);
    } else {
        p2->setPosition(cfg.red_spawn_x, cfg.spawn_y);
        p2->setDirection(1);
        p1->setPosition(cfg.blue_spawn_x, cfg.spawn_y);
        p1->setDirection(0);
    }
    p1->setHP(cfg.start_hp);
    p1->setAngle(45);
    p2->setHP(cfg.start_hp);
    p2->setAngle(45);

    m_currentTurnIndex = 0;

    // Send RoomReadyNotify
    ddt::GameMessage msg;
    auto* ready = msg.mutable_room_ready_notify();
    ready->set_room_id(m_roomId);
    ready->set_wind(m_wind);
    ready->set_first_turn_id(p1->getId());

    auto* rp1 = ready->mutable_player1();
    rp1->set_id(p1->getId());
    rp1->set_name(p1->getName());
    rp1->set_x(p1->getX());
    rp1->set_y(p1->getY());
    rp1->set_hp(p1->getHP());
    rp1->set_max_hp(100);
    rp1->set_angle(p1->getAngle());
    rp1->set_direction(p1->getDirection());

    auto* rp2 = ready->mutable_player2();
    rp2->set_id(p2->getId());
    rp2->set_name(p2->getName());
    rp2->set_x(p2->getX());
    rp2->set_y(p2->getY());
    rp2->set_hp(p2->getHP());
    rp2->set_max_hp(100);
    rp2->set_angle(p2->getAngle());
    rp2->set_direction(p2->getDirection());

    std::string data;
    msg.SerializeToString(&data);
    broadcastMessage(data);

    SYLAR_LOG_INFO(g_logger) << "Room " << m_roomId << " game started";

    m_turnNumber = 1;
    ddt::GameMessage turnMsg;
    auto* turn = turnMsg.mutable_turn_start_notify();
    turn->set_turn_player_id(p1->getId());
    turn->set_wind(m_wind);
    turn->set_turn_number(m_turnNumber);

    std::string turnData;
    turnMsg.SerializeToString(&turnData);
    broadcastMessage(turnData);

    startTurnTimer();
}

void GameRoom::handleShoot(uint32_t player_id, int angle, double force) {
    if (!m_gameStarted) return;

    auto& cfg = GameConfig::Instance();

    // Anti-cheat: validate angle and force
    if (angle < cfg.min_angle || angle > cfg.max_angle) return;
    if (force < cfg.min_force || force > cfg.max_force) return;

    cancelTurnTimer();

    // Find current turn player
    Player::ptr current = nullptr;
    int activeIdx = 0;
    for (int i = 0; i < m_maxPlayers; i++) {
        if (m_players[i]) {
            if (activeIdx == (int)m_currentTurnIndex) {
                current = m_players[i];
                break;
            }
            activeIdx++;
        }
    }
    if (!current || current->getId() != player_id) {
        SYLAR_LOG_WARN(g_logger) << "Not player's turn: " << player_id;
        return;
    }

    auto shooter = getPlayer(player_id);
    if (!shooter) return;
    shooter->setAngle(angle);

    auto result = PhysicsEngine::computeTrajectory(
        shooter->getX(), shooter->getY(), angle, force, m_wind,
        m_heightMap, cfg.world_length, cfg.world_width, cfg.physics_dt);

    // Apply explosion to server heightmap
    if (!result.hit_offscreen) {
        applyExplosion(result.hit_x, result.hit_y, cfg.terrain_explode_radius);
    }

    // Find first active player as opponent for hit check (1v1 logic)
    Player::ptr opponent = nullptr;
    for (int i = 0; i < m_maxPlayers; i++) {
        if (m_players[i] && m_players[i]->getId() != player_id) {
            opponent = m_players[i];
            break;
        }
    }

    bool hit = false;
    uint32_t hit_id = 0;
    int damage = 0;
    float hit_x = result.hit_x;
    float hit_y = result.hit_y;
    ddt::ShootResultNotify::DamageType damageType = ddt::ShootResultNotify::NORMAL;

    if (opponent && !result.hit_offscreen) {
        hit = PhysicsEngine::checkHit(hit_x, hit_y,
            opponent->getX() + (float)cfg.player_hitbox,
            opponent->getY() + (float)cfg.player_hitbox,
            (float)cfg.hit_radius);
        if (hit) {
            hit_id = opponent->getId();
            damage = PhysicsEngine::calculateDamage(hit_x, hit_y,
                opponent->getX() + (float)cfg.player_hitbox,
                opponent->getY() + (float)cfg.player_hitbox,
                cfg.base_damage, (float)cfg.blast_radius);

            // DDT-style damage variants
            int roll = std::rand() % 100;
            if (roll < 33) {
                damage = (int)(damage * 1.5);
                damageType = ddt::ShootResultNotify::CRITICAL;
            } else if (roll < 53) {
                damage = damage / 2;
                if (damage < 1) damage = 1;
                damageType = ddt::ShootResultNotify::BLOCK;
            }

            opponent->addHP(-damage);
        }
    }

    // Get the two active players for the notify
    Player::ptr gp1 = nullptr, gp2 = nullptr;
    for (int i = 0; i < m_maxPlayers; i++) {
        if (m_players[i]) {
            if (!gp1) gp1 = m_players[i];
            else if (!gp2) gp2 = m_players[i];
        }
    }

    ddt::GameMessage msg;
    auto* notify = msg.mutable_shoot_result_notify();
    notify->set_shooter_id(player_id);
    notify->set_angle(angle);
    notify->set_force(force);
    notify->set_wind(m_wind);
    notify->set_hit_x(hit_x);
    notify->set_hit_y(hit_y);
    notify->set_hit_player(hit);
    notify->set_hit_player_id(hit_id);
    notify->set_damage(damage);
    notify->set_damage_type(damageType);

    for (auto& pt : result.points) {
        auto* p = notify->add_points();
        p->set_x(pt.x());
        p->set_y(pt.y());
        p->set_t(pt.t());
    }

    if (gp1) {
        auto* up1 = notify->mutable_updated_player1();
        up1->set_id(gp1->getId());
        up1->set_name(gp1->getName());
        up1->set_x(gp1->getX());
        up1->set_y(gp1->getY());
        up1->set_hp(gp1->getHP());
        up1->set_max_hp(100);
        up1->set_angle(gp1->getAngle());
        up1->set_direction(gp1->getDirection());
    }
    if (gp2) {
        auto* up2 = notify->mutable_updated_player2();
        up2->set_id(gp2->getId());
        up2->set_name(gp2->getName());
        up2->set_x(gp2->getX());
        up2->set_y(gp2->getY());
        up2->set_hp(gp2->getHP());
        up2->set_max_hp(100);
        up2->set_angle(gp2->getAngle());
        up2->set_direction(gp2->getDirection());
    }

    std::string data;
    msg.SerializeToString(&data);
    broadcastMessage(data);

    SYLAR_LOG_INFO(g_logger) << "Room " << m_roomId
        << " player " << player_id << " shot: angle=" << angle
        << " force=" << force << " damage=" << damage;

    if (opponent && opponent->getHP() <= 0) {
        checkGameOver();
        return;
    }

    nextTurn();
}

void GameRoom::handleMove(uint32_t player_id, float delta_x) {
    if (!m_gameStarted) return;

    auto& cfg = GameConfig::Instance();

    // Anti-cheat: single move distance check
    if (std::abs(delta_x) > cfg.move_speed) return;

    // Anti-cheat: cumulative per-turn distance check
    float& used = m_playerMoveUsed[player_id];
    if (used + std::abs(delta_x) > cfg.max_move_per_turn) return;

    auto player = getPlayer(player_id);
    if (!player) return;

    // Verify turn
    int activeIdx = 0;
    bool isMyTurn = false;
    for (int i = 0; i < m_maxPlayers; i++) {
        if (m_players[i]) {
            if (activeIdx == (int)m_currentTurnIndex && m_players[i]->getId() == player_id)
                isMyTurn = true;
            activeIdx++;
        }
    }
    if (!isMyTurn) return;

    float new_x = player->getX() + delta_x;
    if (new_x < 0) new_x = 0;
    if (new_x > cfg.move_boundary) new_x = cfg.move_boundary;
    player->setPosition(new_x, player->getY());

    used += std::abs(delta_x);

    ddt::GameMessage msg;
    auto* notify = msg.mutable_move_notify();
    notify->set_player_id(player_id);
    notify->set_new_x(new_x);

    std::string data;
    msg.SerializeToString(&data);
    broadcastMessage(data);
}

void GameRoom::nextTurn() {
    int activeCount = 0;
    for (int i = 0; i < m_maxPlayers; i++) {
        if (m_players[i]) activeCount++;
    }
    if (activeCount == 0) return;

    m_currentTurnIndex = (m_currentTurnIndex + 1) % activeCount;
    m_turnNumber++;
    m_wind = PhysicsEngine::generateWind();
    m_playerMoveUsed.clear();

    // Find current turn player
    Player::ptr current = nullptr;
    int activeIdx = 0;
    for (int i = 0; i < m_maxPlayers; i++) {
        if (m_players[i]) {
            if (activeIdx == (int)m_currentTurnIndex) {
                current = m_players[i];
                break;
            }
            activeIdx++;
        }
    }
    if (!current) return;

    ddt::GameMessage msg;
    auto* turn = msg.mutable_turn_start_notify();
    turn->set_turn_player_id(current->getId());
    turn->set_wind(m_wind);
    turn->set_turn_number(m_turnNumber);

    std::string data;
    msg.SerializeToString(&data);
    broadcastMessage(data);

    startTurnTimer();
}

void GameRoom::startTurnTimer() {
    cancelTurnTimer();
    auto self = shared_from_this();
    uint64_t timeoutMs = GameConfig::Instance().turn_timeout * 1000;
    m_turnTimer = sylar::IOManager::GetThis()->addTimer(timeoutMs,
        [self]() {
            if (!self->m_gameStarted) return;
            SYLAR_LOG_INFO(g_logger) << "Room " << self->m_roomId << " turn timeout";
            self->nextTurn();
        }, false);
}

void GameRoom::cancelTurnTimer() {
    if (m_turnTimer) {
        m_turnTimer->cancel();
        m_turnTimer.reset();
    }
}

void GameRoom::checkGameOver() {
    uint32_t winner_id = 0;
    for (int i = 0; i < m_maxPlayers; i++) {
        if (m_players[i] && m_players[i]->getHP() > 0) {
            winner_id = m_players[i]->getId();
            break;
        }
    }

    ddt::GameMessage msg;
    auto* over = msg.mutable_game_over_notify();
    over->set_winner_id(winner_id);
    over->set_reason("opponent_hp_zero");

    std::string data;
    msg.SerializeToString(&data);
    broadcastMessage(data);

    m_gameStarted = false;
    SYLAR_LOG_INFO(g_logger) << "Room " << m_roomId << " game over, winner=" << winner_id;
}

void GameRoom::broadcastMessage(const std::string& data) {
    for (int i = 0; i < m_maxPlayers; i++) {
        if (m_players[i]) {
            m_players[i]->sendMessage(data);
        }
    }
}

void GameRoom::broadcastToRoom(const std::string& data) {
    broadcastMessage(data);
}

ddt::RoomInfo GameRoom::getRoomInfo() const {
    ddt::RoomInfo info;
    info.set_room_id(m_roomId);
    info.set_room_name(m_roomName);
    info.set_player_count(m_playerCount);
    info.set_max_players(m_maxPlayers);
    info.set_game_started(m_gameStarted);

    for (int i = 0; i < m_maxPlayers; i++) {
        if (m_players[i]) {
            auto* slot = info.add_players();
            slot->set_player_id(m_players[i]->getId());
            slot->set_player_name(m_players[i]->getName());
            auto tit = m_playerTeam.find(m_players[i]->getId());
            slot->set_team(tit != m_playerTeam.end() ? tit->second : ddt::TEAM_RED);
            auto rit = m_playerReady.find(m_players[i]->getId());
            slot->set_ready(rit != m_playerReady.end() ? rit->second : false);
        }
    }
    return info;
}

void GameRoom::generateHeightMap() {
    auto& cfg = GameConfig::Instance();
    int worldLen = cfg.world_length;

    m_heightMap.resize(worldLen, (float)cfg.terrain_base_height);

    for (int x = 0; x < worldLen; x++) {
        float baseH = (float)cfg.terrain_base_height;

        // Parabolic valley (same formula as client terrain.cc)
        float dx = (float)x - worldLen * 0.5f;
        float a = (float)cfg.terrain_valley_amplitude * (float)cfg.terrain_valley_scale;
        float sag = a * a - dx * dx;
        if (sag > 0) {
            baseH = (float)cfg.terrain_valley_base * (float)cfg.terrain_valley_base_scale - std::sqrt(sag);
        }

        // Rolling hills
        baseH += 25.0f * std::sin(x * 0.008f) + 12.0f * std::sin(x * 0.023f);

        // Platform bumps
        if (x > 400 && x < 600) baseH -= 40.0f;
        if (x > 1400 && x < 1600) baseH -= 60.0f;
        if (x > 2400 && x < 2600) baseH -= 40.0f;

        m_heightMap[x] = std::max((float)cfg.terrain_min_height,
                                  std::min((float)cfg.terrain_max_height, baseH));
    }

    SYLAR_LOG_INFO(g_logger) << "Room " << m_roomId << " heightmap generated ("
        << worldLen << " columns)";
}

void GameRoom::applyExplosion(float cx, float cy, float radius) {
    int r = static_cast<int>(radius) + 1;
    int x0 = std::max(0, static_cast<int>(cx - r));
    int x1 = std::min(static_cast<int>(m_heightMap.size()) - 1, static_cast<int>(cx + r));

    float r2 = radius * radius;
    for (int x = x0; x <= x1; x++) {
        float dx = (float)x - cx;
        // At this x, the explosion circle extends from cy - sqrt(r^2 - dx^2) to cy + sqrt(r^2 - dx^2)
        float dx2 = r2 - dx * dx;
        if (dx2 <= 0) continue;
        float halfW = std::sqrt(dx2);
        float topEdge = cy - halfW;
        float bottomEdge = cy + halfW;

        // If the current height is below the top edge of the explosion,
        // we need to dig out the terrain within the circle
        if (m_heightMap[x] < bottomEdge) {
            // Terrain exists within explosion zone
            if (m_heightMap[x] < topEdge) {
                // Explosion fully covers the terrain at this x
                // Push height below the bottom of the explosion (make a hole)
                m_heightMap[x] = bottomEdge + 1.0f;
            } else {
                // Terrain is partially in the explosion zone
                // Only remove the part inside the circle
                // The terrain above the explosion remains
                m_heightMap[x] = bottomEdge + 1.0f;
            }
        }
    }
}

} // namespace ddt
