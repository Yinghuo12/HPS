#include "game_network.h"
#include "game.h"
#include "network_client.h"
#include "ddt.pb.h"
#include "common/ddt_physics.h"
#include <iostream>

#define C_RED     "\033[31m"
#define C_GREEN   "\033[32m"
#define C_YELLOW  "\033[33m"
#define C_CYAN    "\033[36m"
#define C_BOLD    "\033[1m"
#define C_RESET   "\033[0m"

GameNetwork::GameNetwork(Game& game) : m_game(game) {}

void GameNetwork::processMessages() {
    while (ddt::NetworkClient::Instance().hasMessages()) {
        auto msg = ddt::NetworkClient::Instance().pollMessage();
        if (!msg) break;

        switch (msg->payload_case()) {
            case ddt::GameMessage::kLoginResponse: {
                auto& resp = msg->login_response();
                if (resp.ok()) {
                    m_game.MyId = resp.id();
                    m_game.m_loggedIn = true;
                    m_game.State = GAME_MENU;
                    m_game.StatusText = "SELECT OR CREATE A ROOM";
                    std::cout << C_GREEN << "< Login OK! ID=" << m_game.MyId << C_RESET << std::endl;
                    ddt::NetworkClient::Instance().sendRoomList();
                } else {
                    std::cout << C_RED << "< Login failed: " << resp.msg() << C_RESET << std::endl;
                    m_game.StatusText = "LOGIN FAILED: " + resp.msg();
                }
                break;
            }
            case ddt::GameMessage::kJoinRoomResponse: {
                auto& resp = msg->join_room_response();
                if (resp.ok()) {
                    m_game.CurrentRoomId = resp.room_id();
                    m_game.m_myReady = false;
                    std::cout << C_GREEN << "< Joined room #" << resp.room_id()
                              << ", waiting..." << C_RESET << std::endl;
                    m_game.State = GAME_WAITING;
                    m_game.StatusText = "WAITING IN ROOM...";
                    ddt::NetworkClient::Instance().sendRoomList();
                } else {
                    std::cout << C_RED << "< Join failed: " << resp.msg() << C_RESET << std::endl;
                }
                break;
            }
            case ddt::GameMessage::kRoomReadyNotify: {
                m_game.m_hasShot = false;
                m_game.projectile.Reset();          // 清空残留炸弹
                m_game.m_pendingHit.active = false; // 清空残余的扣血结算
                m_game.m_explosions.clear();        // 清空旧爆炸特效
                m_game.m_damageFloats.clear();
                m_game.StatusText = "";             // 清空 “YOU WIN!” 文本

                // [BugFix] 重新生成地形，清除上一局弹坑和旧纹理
                if (m_game.GameTerrain) {
                    m_game.GameTerrain->Reset();
                }
                
                auto& nty = msg->room_ready_notify();
                auto& p1 = nty.player1();
                auto& p2 = nty.player2();

                m_game.Players[0]->Position = glm::vec2(p1.x(), p1.y());
                m_game.Players[1]->Position = glm::vec2(p2.x(), p2.y());
                m_game.HP[0] = p1.hp();
                m_game.HP[1] = p2.hp();
                m_game.Directions[0] = p1.direction();
                m_game.Directions[1] = p2.direction();
                m_game.Wind = nty.wind();

                // 存储服务端物理参数（问题3修复）
                if (nty.has_physics_params()) {
                    auto& pp = nty.physics_params();
                    m_game.m_serverPhysics.air_factor     = pp.air_factor();
                    m_game.m_serverPhysics.wind_factor    = pp.wind_factor();
                    m_game.m_serverPhysics.gravity_factor = pp.gravity_factor();
                    m_game.m_serverPhysics.force_factor   = pp.force_factor();
                }

                // 存储服务端移动限额（问题14修复）
                if (nty.max_move_per_turn() > 0) {
                    m_game.m_maxMovePerTurn = nty.max_move_per_turn();
                }

                if (p1.id() == m_game.MyId) {
                    m_game.MyIndex = 0;
                    m_game.OpponentAngle = p2.angle();
                } else {
                    m_game.MyIndex = 1;
                    m_game.OpponentAngle = p1.angle();
                }

                // [BugFix] 服务端存储的是绝对角度，转换为相对地形的基准角度
                // CurrentAngle = 绝对角度 - slopeAdj
                {
                    int serverAngle = (m_game.MyIndex == 0) ? p1.angle() : p2.angle();
                    int slopeAdj = 0;
                    if (m_game.GameTerrain && m_game.Players[m_game.MyIndex]) {
                        auto& hm = m_game.GameTerrain->GetHeightMap();
                        int ix = (int)(m_game.Players[m_game.MyIndex]->Position.x);
                        if (ix >= 1 && ix < (int)hm.size() - 1) {
                            float slope = ddt::PhysicsEngine::getSlopeAngle((float)ix, hm);
                            slopeAdj = (int)roundf(slope);
                        }
                    }
                    int baseAngle = serverAngle - (m_game.Directions[m_game.MyIndex] == 1 ? slopeAdj : -slopeAdj);
                    if (baseAngle < 20) baseAngle = 20;
                    if (baseAngle > 65) baseAngle = 65;
                    m_game.CurrentAngle = baseAngle;
                }

                m_game.IsMyTurn = (nty.first_turn_id() == m_game.MyId);
                m_game.State = GAME_PLAYING;

                m_game.m_cameraMode = CAM_INTRO;
                m_game.m_introPhase = 0;
                m_game.m_introTimer = 0;
                m_game.m_camera->snapTo(m_game.Players[0]->Position.x, m_game.Players[0]->Position.y);

                std::cout << C_BOLD << C_GREEN
                          << "\n======== GAME START ========"
                          << "\n  P1(ID=" << p1.id() << " " << p1.name()
                          << ") vs P2(ID=" << p2.id() << " " << p2.name() << ")"
                          << "\n  P1 pos=(" << p1.x() << "," << p1.y() << ")"
                          << "  P2 pos=(" << p2.x() << "," << p2.y() << ")"
                          << "\n  Wind: " << m_game.Wind
                          << "  First turn: " << (m_game.IsMyTurn ? "YOU" : "OPPONENT")
                          << "\n=============================" << C_RESET << std::endl;
                break;
            }
            case ddt::GameMessage::kTurnStartNotify: {
                // 回合开始时冷却缩减
                if (m_game.IsMyTurn && m_game.m_flyCooldown > 0) {
                    m_game.m_flyCooldown--;
                }
                m_game.m_useFlyItem = false; // 重置飞行道具使用状态

                m_game.m_hasShot = false;
                m_game.m_pendingHit.active = false;
                m_game.projectile.Reset(); // 确保不拖泥带水

                // 每个新回合开始，完整重置射击/蓄力状态
                m_game.IsCharging = false;
                m_game.Power = 0.0f;
                // 每个新回合开始，重置移动累计距离
                m_game.m_moveUsed = 0.0f;

                auto& nty = msg->turn_start_notify();
                m_game.Wind = nty.wind();
                m_game.TurnNumber = nty.turn_number();
                m_game.IsMyTurn = (nty.turn_player_id() == m_game.MyId);

                // 问题10修复：回合状态校准 — 用服务端权威状态覆盖本地
                if (nty.has_player1()) {
                    auto& sp1 = nty.player1();
                    m_game.HP[0] = sp1.hp();
                    m_game.Directions[0] = sp1.direction();
                    if (m_game.Players[0]) m_game.Players[0]->Position = glm::vec2(sp1.x(), sp1.y());
                }
                if (nty.has_player2()) {
                    auto& sp2 = nty.player2();
                    m_game.HP[1] = sp2.hp();
                    m_game.Directions[1] = sp2.direction();
                    if (m_game.Players[1]) m_game.Players[1]->Position = glm::vec2(sp2.x(), sp2.y());
                }

                std::cout << C_CYAN << "< [Turn " << m_game.TurnNumber << "] "
                          << (m_game.IsMyTurn ? C_BOLD "YOUR TURN" : "OPPONENT TURN")
                          << "  Wind: " << m_game.Wind << C_RESET << std::endl;

                if (m_game.IsMyTurn) {
                    m_game.StatusText = "YOUR TURN!";
                } else {
                    m_game.StatusText = "OPPONENT TURN...";
                }

                m_game.m_cameraMode = CAM_FOLLOW_TURN;
                GameObject* turnPlayer = m_game.IsMyTurn ? m_game.myPlayer() : m_game.opponentPlayer();
                m_game.m_camera->panTo(turnPlayer->Position.x, turnPlayer->Position.y, 800.0f);
                break;
            }
            case ddt::GameMessage::kShootResultNotify: {
                auto& nty = msg->shoot_result_notify();
                m_game.m_projIsFly = nty.is_fly(); // 记录本次是否为纸飞机，供 Renderer 使用
                m_game.m_lastShooterIdx = m_game.IsMyTurn ? m_game.MyIndex : (1 - m_game.MyIndex);

                if (m_game.MyIndex >= 0) {
                    if (m_game.MyIndex == 0) {
                        m_game.OpponentAngle = nty.updated_player2().angle();
                    } else {
                        m_game.OpponentAngle = nty.updated_player1().angle();
                    }
                }

                // 本地计算弹道轨迹（不再依赖网络传输 points）
                std::vector<TrajectoryPoint> points;
                if (m_game.GameTerrain) {
                    // 将 UI 角度转换为物理角度，与服务端 handleShoot 保持一致
                    int physicsAngle = nty.angle();
                    if (nty.direction() == 0) {
                        physicsAngle = 180 - nty.angle();
                    }
                    ddt::PhysicsParams params = m_game.m_serverPhysics;
                    ddt::ShootResult result = ddt::PhysicsEngine::computeTrajectory(
                        nty.start_x(), nty.start_y(),
                        physicsAngle, nty.force(), nty.wind(),
                        params,
                        m_game.GameTerrain->GetHeightMap(),
                        m_game.WORLD_W, m_game.WORLD_H);
                    points.reserve(result.points.size());
                    for (auto& pt : result.points) {
                        points.push_back({pt.x, pt.y, pt.t});
                    }
                    std::cout << C_CYAN << "[Physics] Local trajectory: "
                              << points.size() << " points computed" << C_RESET << std::endl;
                }
                m_game.projectile.Start(points);
                m_game.m_cameraMode = CAM_FOLLOW_PROJ;

                m_game.m_pendingHit.active = true;
                m_game.m_pendingHit.hit_x = nty.hit_x();
                m_game.m_pendingHit.hit_y = nty.hit_y();
                m_game.m_pendingHit.hit_player = nty.hit_player();
                m_game.m_pendingHit.hit_player_id = nty.hit_player_id();
                m_game.m_pendingHit.damage = nty.damage();
                m_game.m_pendingHit.damage_type = nty.damage_type();
                m_game.m_pendingHit.hp0 = nty.updated_player1().hp();
                m_game.m_pendingHit.hp1 = nty.updated_player2().hp();
                m_game.m_pendingHit.pos0 = glm::vec2(nty.updated_player1().x(), nty.updated_player1().y());
                m_game.m_pendingHit.pos1 = glm::vec2(nty.updated_player2().x(), nty.updated_player2().y());

                std::cout << C_RED << "< Shoot result: hit=" << nty.hit_player()
                          << " damage=" << nty.damage()
                          << "  P1 HP:" << nty.updated_player1().hp()
                          << "  P2 HP:" << nty.updated_player2().hp()
                          << "  local_traj_pts=" << points.size()
                          << C_RESET << std::endl;
                break;
            }
            case ddt::GameMessage::kMoveNotify: {
                auto& nty = msg->move_notify();
                if (m_game.MyIndex >= 0) {
                    int idx = (nty.player_id() == m_game.MyId) ? m_game.MyIndex : (1 - m_game.MyIndex);
                    // 仅更新对手方向；本地玩家方向由 ProcessInput 管理
                    if (idx != m_game.MyIndex) {
                        if (nty.new_x() < m_game.Players[idx]->Position.x) {
                            m_game.Directions[idx] = 0;
                        } else if (nty.new_x() > m_game.Players[idx]->Position.x) {
                            m_game.Directions[idx] = 1;
                        }
                    }
                    m_game.Players[idx]->Position.x = nty.new_x();
                }
                break;
            }
            case ddt::GameMessage::kGameOverNotify: {
                auto& nty = msg->game_over_notify();
                m_game.State = GAME_OVER;
                bool won = (nty.winner_id() == m_game.MyId);
                m_game.StatusText = won ? "YOU WIN!" : "YOU LOSE!";

                std::cout << C_BOLD << (won ? C_GREEN : C_RED)
                          << "\n======== GAME OVER ========"
                          << "\n  " << m_game.StatusText
                          << "\n  Reason: " << nty.reason()
                          << "\n===========================" << C_RESET << std::endl;
                break;
            }
            case ddt::GameMessage::kOpponentLeftNotify: {
                std::cout << C_GREEN << "< Opponent left! YOU WIN!" << C_RESET << std::endl;
                m_game.State = GAME_OVER;
                m_game.StatusText = "YOU WIN!";
                break;
            }
            case ddt::GameMessage::kErrorNotify: {
                auto& nty = msg->error_notify();
                std::cout << C_RED << "< Error " << nty.code()
                          << ": " << nty.msg() << C_RESET << std::endl;
                // 服务端拒绝射击（角度越界等），重置本地射击状态允许重试
                if (nty.code() == 460 && m_game.m_hasShot) {
                    m_game.m_hasShot = false;
                    m_game.IsCharging = false;
                    m_game.Power = 0.0f;
                    std::cout << C_YELLOW << "< Shot rejected, you may retry"
                              << C_RESET << std::endl;
                }
                if (nty.code() == 409) {
                    m_game.StatusText = "KICKED - Account logged in elsewhere";
                    m_game.State = GAME_LOGIN;
                    m_game.CurrentRoomId = 0;
                }
                break;
            }
            case ddt::GameMessage::kServerShutdownNotify: {
                auto& nty = msg->server_shutdown_notify();
                m_game.StatusText = "SERVER SHUTDOWN - " + nty.reason();
                m_game.State = GAME_LOGIN;
                m_game.CurrentRoomId = 0;
                m_game.m_myReady = false;
                std::cout << C_RED << "< Server shutdown: " << nty.reason()
                          << C_RESET << std::endl;
                break;
            }
            case ddt::GameMessage::kRegisterResponse: {
                auto& resp = msg->register_response();
                if (resp.ok()) {
                    std::cout << C_GREEN << "< Register OK! Account ID=" << resp.id() << C_RESET << std::endl;
                    m_game.StatusText = "REGISTER OK! NOW LOGIN";
                } else {
                    std::cout << C_RED << "< Register failed: " << resp.msg() << C_RESET << std::endl;
                    m_game.StatusText = "REGISTER FAILED: " + resp.msg();
                }
                break;
            }
            case ddt::GameMessage::kChatNotify: {
                auto& nty = msg->chat_notify();
                Game::ChatMsg cm;
                cm.channel = nty.channel();
                cm.sender_id = nty.sender_id();
                cm.sender_name = nty.sender_name();
                cm.message = nty.message();
                cm.timestamp = nty.timestamp();
                m_game.m_chatMessages.push_back(cm);
                if (m_game.m_chatMessages.size() > 500)
                    m_game.m_chatMessages.erase(m_game.m_chatMessages.begin(), m_game.m_chatMessages.begin() + 100);
                m_game.m_chatScrollToBottom = true;
                break;
            }
            case ddt::GameMessage::kChatHistoryResponse: {
                auto& resp = msg->chat_history_response();
                for (int i = 0; i < resp.messages_size(); i++) {
                    auto& nty = resp.messages(i);
                    Game::ChatMsg cm;
                    cm.channel = nty.channel();
                    cm.sender_id = nty.sender_id();
                    cm.sender_name = nty.sender_name();
                    cm.message = nty.message();
                    cm.timestamp = nty.timestamp();
                    m_game.m_chatMessages.push_back(cm);
                }
                m_game.m_chatScrollToBottom = true;
                break;
            }
            case ddt::GameMessage::kFriendAddResponse: {
                auto& resp = msg->friend_add_response();
                if (resp.ok()) {
                    std::cout << C_GREEN << "< Friend added: " << resp.friend_name() << C_RESET << std::endl;
                } else {
                    std::cout << C_RED << "< Friend add failed: " << resp.msg() << C_RESET << std::endl;
                }
                break;
            }
            case ddt::GameMessage::kRoomListResponse: {
                auto& resp = msg->room_list_response();
                m_game.RoomList.clear();
                for (int i = 0; i < resp.rooms_size(); i++) {
                    auto& r = resp.rooms(i);
                    RoomInfoClient ri;
                    ri.room_id = r.room_id();
                    ri.room_name = r.room_name();
                    ri.player_count = r.player_count();
                    ri.max_players = r.max_players();
                    ri.game_started = r.game_started();
                    for (int j = 0; j < r.players_size(); j++) {
                        auto& s = r.players(j);
                        RoomSlot slot;
                        slot.player_id = s.player_id();
                        slot.player_name = s.player_name();
                        slot.team = s.team();
                        slot.ready = s.ready();
                        ri.players.push_back(slot);
                    }
                    m_game.RoomList.push_back(ri);
                }
                break;
            }
            case ddt::GameMessage::kCreateRoomResponse: {
                auto& resp = msg->create_room_response();
                if (resp.ok()) {
                    m_game.CurrentRoomId = resp.room_id();
                    m_game.State = GAME_WAITING;
                    m_game.m_myReady = false;
                    m_game.StatusText = "ROOM CREATED, WAITING...";
                    std::cout << C_GREEN << "< Created room #" << resp.room_id() << C_RESET << std::endl;
                } else {
                    m_game.StatusText = "CREATE FAILED: " + resp.msg();
                    std::cout << C_RED << "< Create room failed: " << resp.msg() << C_RESET << std::endl;
                }
                break;
            }
            case ddt::GameMessage::kRoomUpdateNotify: {
                auto& nty = msg->room_update_notify();
                auto& r = nty.room_info();
                bool found = false;
                for (auto& ri : m_game.RoomList) {
                    if (ri.room_id == r.room_id()) {
                        ri.room_name = r.room_name();
                        ri.player_count = r.player_count();
                        ri.max_players = r.max_players();
                        ri.game_started = r.game_started();
                        ri.players.clear();
                        for (int j = 0; j < r.players_size(); j++) {
                            auto& s = r.players(j);
                            RoomSlot slot;
                            slot.player_id = s.player_id();
                            slot.player_name = s.player_name();
                            slot.team = s.team();
                            slot.ready = s.ready();
                            ri.players.push_back(slot);
                        }
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    RoomInfoClient ri;
                    ri.room_id = r.room_id();
                    ri.room_name = r.room_name();
                    ri.player_count = r.player_count();
                    ri.max_players = r.max_players();
                    ri.game_started = r.game_started();
                    for (int j = 0; j < r.players_size(); j++) {
                        auto& s = r.players(j);
                        RoomSlot slot;
                        slot.player_id = s.player_id();
                        slot.player_name = s.player_name();
                        slot.team = s.team();
                        slot.ready = s.ready();
                        ri.players.push_back(slot);
                    }
                    m_game.RoomList.push_back(ri);
                }
                break;
            }
            case ddt::GameMessage::kReadyNotify: {
                auto& nty = msg->ready_notify();
                for (auto& ri : m_game.RoomList) {
                    if (ri.room_id == m_game.CurrentRoomId) {
                        for (auto& slot : ri.players) {
                            if (slot.player_id == nty.player_id()) {
                                slot.ready = nty.ready();
                            }
                        }
                    }
                }
                if (nty.player_id() == m_game.MyId) {
                    m_game.m_myReady = nty.ready();
                }
                break;
            }
            case ddt::GameMessage::kFriendListResponse: {
                auto& resp = msg->friend_list_response();
                std::cout << C_GREEN << "< Friends: " << resp.friends_size() << C_RESET << std::endl;
                break;
            }
            case ddt::GameMessage::kSwitchTeamResponse: {
                auto& resp = msg->switch_team_response();
                if (resp.ok()) {
                    std::cout << C_GREEN << "< Switched to "
                              << (resp.team() == 0 ? "RED" : "BLUE") << C_RESET << std::endl;
                } else {
                    std::cout << C_RED << "< Switch failed: " << resp.msg() << C_RESET << std::endl;
                }
                break;
            }
            default:
                break;
        }
    }
}
