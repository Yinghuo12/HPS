#ifndef __DDT_GAME_ROOM_H__
#define __DDT_GAME_ROOM_H__

#include <memory>
#include <map>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <atomic>
#include "ddt_player.h"
#include "ddt.pb.h"
#include "sylar/scheduler/timer.h"

namespace ddt {
class TurnStartNotify; 
}

namespace ddt {

class GameRoom : public std::enable_shared_from_this<GameRoom> {
public:
    typedef std::shared_ptr<GameRoom> ptr;

    explicit GameRoom(uint32_t room_id, const std::string& name = "", int maxPlayers = 4);
    ~GameRoom();
    
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
    void handleShoot(uint32_t player_id, int angle, double force, bool is_fly = false);
    void handleMove(uint32_t player_id, float delta_x);
    void handlePass(uint32_t player_id);  // [新增] 跳过回合
    
    uint32_t getId() const { return m_roomId; }
    const std::string& getName() const { return m_roomName; }
    Player::ptr getPlayer(uint32_t player_id) const;
    Player::ptr getOtherPlayer(uint32_t my_id) const;
    ddt::TeamSide getPlayerTeam(uint32_t player_id) const;
    
    ddt::RoomInfo getRoomInfo() const;
    void broadcastToRoom(const std::string& data);
    
    void post(std::function<void()> task);
    
    void startActor();
    void stopActor(); // [新增] 强制停止 Actor 清理内存泄露

private:
    void broadcastMessage(const std::string& data);
    void nextTurn();
    void checkGameOver();
    void startTurnTimer();
    void cancelTurnTimer();
    void generateHeightMap();
    void applyExplosion(float cx, float cy, float radius);
    void fillTurnPlayerStates(ddt::TurnStartNotify* turn);

    void actorLoop();
    
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
    
    std::vector<float> m_heightMap;
    std::map<uint32_t, float> m_playerMoveUsed;
    std::map<uint32_t, bool> m_playerShootLocked;
    
    sylar::Timer::ptr m_turnTimer;
    
    std::mutex m_queueMutex;
    std::condition_variable m_queueCV;
    std::queue<std::function<void()>> m_taskQueue;
    std::atomic<bool> m_running{false};
};

} // namespace ddt

#endif