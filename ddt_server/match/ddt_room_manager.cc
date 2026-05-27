#include "ddt_room_manager.h"
#include "sylar/log.h"

namespace ddt {

static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

RoomManager& RoomManager::Instance() {
    static RoomManager inst;
    return inst;
}

GameRoom::ptr RoomManager::joinRoom(Player::ptr player, uint32_t preferred_room_id,
                                     ddt::TeamSide team) {
    sylar::RWMutex::WriteLock lock(m_mutex);

    // Check if player already in a room
    auto pit = m_playerToRoom.find(player->getId());
    if (pit != m_playerToRoom.end()) return nullptr;

    GameRoom::ptr room;
    if (preferred_room_id > 0) {
        auto it = m_rooms.find(preferred_room_id);
        if (it != m_rooms.end() && !it->second->isFull() && !it->second->hasStarted()) {
            room = it->second;
        }
    }

    if (!room) {
        room = findOrCreateAvailableRoom();
    }

    if (!room->addPlayer(player, team)) {
        SYLAR_LOG_WARN(g_logger) << "Room full, cannot add player " << player->getId();
        return nullptr;
    }

    m_playerToRoom[player->getId()] = room->getId();
    SYLAR_LOG_INFO(g_logger) << "Player " << player->getId()
        << " joined room " << room->getId()
        << " (" << (room->isFull() ? "full" : "waiting") << ")";

    broadcastRoomUpdate(room);

    return room;
}

void RoomManager::leaveRoom(uint32_t player_id) {
    sylar::RWMutex::WriteLock lock(m_mutex);

    auto it = m_playerToRoom.find(player_id);
    if (it == m_playerToRoom.end()) return;

    auto room_it = m_rooms.find(it->second);
    if (room_it != m_rooms.end()) {
        room_it->second->removePlayer(player_id);
        if (room_it->second->isEmpty()) {
            m_rooms.erase(room_it);
        } else {
            broadcastRoomUpdate(room_it->second);
        }
    }
    m_playerToRoom.erase(it);
}

GameRoom::ptr RoomManager::findRoomByPlayer(uint32_t player_id) const {
    sylar::RWMutex::ReadLock lock(m_mutex);

    auto it = m_playerToRoom.find(player_id);
    if (it == m_playerToRoom.end()) return nullptr;

    auto room_it = m_rooms.find(it->second);
    if (room_it != m_rooms.end()) return room_it->second;
    return nullptr;
}

GameRoom::ptr RoomManager::createRoom(Player::ptr player, const std::string& room_name) {
    sylar::RWMutex::WriteLock lock(m_mutex);

    // Check if player already in a room
    auto pit = m_playerToRoom.find(player->getId());
    if (pit != m_playerToRoom.end()) return nullptr;

    uint32_t id = m_nextRoomId++;
    auto room = std::make_shared<GameRoom>(id, room_name);

    if (!room->addPlayer(player, ddt::TEAM_RED)) {
        return nullptr;
    }

    m_rooms[id] = room;
    m_playerToRoom[player->getId()] = id;

    broadcastRoomUpdate(room);

    SYLAR_LOG_INFO(g_logger) << "Player " << player->getId()
        << " created room " << id << " '" << room_name << "'";

    return room;
}

std::vector<ddt::RoomInfo> RoomManager::getRoomList() const {
    sylar::RWMutex::ReadLock lock(m_mutex);

    std::vector<ddt::RoomInfo> result;
    for (auto& pair : m_rooms) {
        result.push_back(pair.second->getRoomInfo());
    }
    return result;
}

GameRoom::ptr RoomManager::findOrCreateAvailableRoom() {
    for (auto& pair : m_rooms) {
        if (!pair.second->isFull() && !pair.second->hasStarted()) return pair.second;
    }
    uint32_t id = m_nextRoomId++;
    auto room = std::make_shared<GameRoom>(id);
    m_rooms[id] = room;
    return room;
}

void RoomManager::broadcastRoomUpdate(GameRoom::ptr room) {
    ddt::GameMessage msg;
    auto* notify = msg.mutable_room_update_notify();
    *notify->mutable_room_info() = room->getRoomInfo();

    std::string data;
    msg.SerializeToString(&data);
    room->broadcastToRoom(data);
}

} // namespace ddt
