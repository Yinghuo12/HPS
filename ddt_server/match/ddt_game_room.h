#ifndef __DDT_GAME_ROOM_H__
#define __DDT_GAME_ROOM_H__

#include <memory>
#include <map>
#include "ddt_player.h"
#include "ddt.pb.h"
#include "sylar/timer.h"

namespace ddt {

class GameRoom : public std::enable_shared_from_this<GameRoom> {
public:
    typedef std::shared_ptr<GameRoom> ptr;

    explicit GameRoom(uint32_t room_id, const std::string& name = "", int maxPlayers = 4);

    bool addPlayer(Player::ptr player, ddt::TeamSide team = ddt::TEAM_RED);
    bool isFull() const { return m_playerCount >= m_maxPlayers; }
    bool isEmpty() const { return m_playerCount == 0; }
    bool hasStarted() const { return m_gameStarted; }
    bool hasPlayer(uint32_t player_id) const;
    void removePlayer(uint32_t player_id);

    void setReady(uint32_t player_id, bool ready);
    bool switchTeam(uint32_t player_id, ddt::TeamSide new_team);
    bool canStart() const;
    void startGame();
    void handleShoot(uint32_t player_id, int angle, double force);
    void handleMove(uint32_t player_id, float delta_x);

    uint32_t getId() const { return m_roomId; }
    const std::string& getName() const { return m_roomName; }
    Player::ptr getPlayer(uint32_t player_id) const;
    Player::ptr getOtherPlayer(uint32_t my_id) const;
    ddt::TeamSide getPlayerTeam(uint32_t player_id) const;

    ddt::RoomInfo getRoomInfo() const;
    void broadcastToRoom(const std::string& data);

private:
    void broadcastMessage(const std::string& data);
    void nextTurn();
    void checkGameOver();
    void startTurnTimer();
    void cancelTurnTimer();
    void generateHeightMap();
    void applyExplosion(float cx, float cy, float radius);

    uint32_t m_roomId;
    std::string m_roomName;
    int m_maxPlayers;
    std::vector<Player::ptr> m_players;
    int m_playerCount = 0;
    std::map<uint32_t, ddt::TeamSide> m_playerTeam;
    std::map<uint32_t, bool> m_playerReady;

    uint32_t m_currentTurnIndex = 0;
    uint32_t m_turnNumber = 0;
    float m_wind = 0.0f;
    bool m_gameStarted = false;

    // Server-side terrain
    std::vector<float> m_heightMap;

    // Anti-cheat: per-turn move tracking
    std::map<uint32_t, float> m_playerMoveUsed;

    sylar::Timer::ptr m_turnTimer;
};

} // namespace ddt

#endif
