#ifndef __DDT_ROOM_MANAGER_H__
#define __DDT_ROOM_MANAGER_H__

#include <map>
#include <vector>
#include <memory>
#include <atomic>
#include "sylar/thread.h"
#include "ddt_game_room.h"
#include "ddt_player.h"
#include "ddt.pb.h"

namespace ddt {

class RoomManager {
public:
    static constexpr int SHARD_COUNT = 16; // 16个锁分片，解决全局锁冲突
    
    static RoomManager& Instance();

    GameRoom::ptr joinRoom(Player::ptr player, uint32_t preferred_room_id = 0,
                           ddt::TeamSide team = ddt::TEAM_RED);
    void leaveRoom(uint32_t player_id);
    GameRoom::ptr findRoomByPlayer(uint32_t player_id) const;
    
    GameRoom::ptr createRoom(Player::ptr player, const std::string& room_name);
    std::vector<ddt::RoomInfo> getRoomList() const;

private:
    RoomManager() = default;
    void broadcastRoomUpdate(GameRoom::ptr room);

    // 按房间ID分片保护房间实体
    struct RoomShard {
        mutable sylar::RWMutex mutex;
        std::map<uint32_t, GameRoom::ptr> rooms;
    };
    
    // 按玩家ID分片保护索引
    struct PlayerShard {
        mutable sylar::RWMutex mutex;
        std::map<uint32_t, uint32_t> playerToRoom;
    };

    RoomShard m_roomShards[SHARD_COUNT];
    PlayerShard m_playerShards[SHARD_COUNT];
    std::atomic<uint32_t> m_nextRoomId{1};
};

} // namespace ddt

#endif