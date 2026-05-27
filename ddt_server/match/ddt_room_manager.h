#ifndef __DDT_ROOM_MANAGER_H__
#define __DDT_ROOM_MANAGER_H__

#include <map>
#include <vector>
#include <memory>
#include "sylar/thread.h"
#include "ddt_game_room.h"
#include "ddt_player.h"
#include "ddt.pb.h"

namespace ddt {

class RoomManager {
public:
    static RoomManager& Instance();

    GameRoom::ptr joinRoom(Player::ptr player, uint32_t preferred_room_id = 0,
                           ddt::TeamSide team = ddt::TEAM_RED);
    void leaveRoom(uint32_t player_id);
    GameRoom::ptr findRoomByPlayer(uint32_t player_id) const;

    GameRoom::ptr createRoom(Player::ptr player, const std::string& room_name);
    std::vector<ddt::RoomInfo> getRoomList() const;

private:
    RoomManager() = default;
    GameRoom::ptr findOrCreateAvailableRoom();
    void broadcastRoomUpdate(GameRoom::ptr room);

    mutable sylar::RWMutex m_mutex;
    std::map<uint32_t, GameRoom::ptr> m_rooms;
    std::map<uint32_t, uint32_t> m_playerToRoom;
    uint32_t m_nextRoomId = 1;
};

} // namespace ddt

#endif
