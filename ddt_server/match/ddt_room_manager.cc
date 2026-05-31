#include "ddt_room_manager.h"
#include "sylar/log.h"
#include "sylar/iomanager.h"

namespace ddt {

static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

RoomManager& RoomManager::Instance() {
    static RoomManager inst;
    return inst;
}

GameRoom::ptr RoomManager::joinRoom(Player::ptr player, uint32_t preferred_room_id, ddt::TeamSide team) {
    uint32_t pId = player->getId();
    uint32_t pShardIdx = pId % SHARD_COUNT;
    
    {
        sylar::RWMutex::ReadLock plock(m_playerShards[pShardIdx].mutex);
        if (m_playerShards[pShardIdx].playerToRoom.count(pId)) return nullptr;
    }
    
    GameRoom::ptr room;
    
    if (preferred_room_id > 0) {
        uint32_t rShardIdx = preferred_room_id % SHARD_COUNT;
        sylar::RWMutex::WriteLock rlock(m_roomShards[rShardIdx].mutex);
        auto it = m_roomShards[rShardIdx].rooms.find(preferred_room_id);
        if (it != m_roomShards[rShardIdx].rooms.end() && !it->second->isFull() && !it->second->hasStarted()) {
            if (it->second->addPlayer(player, team)) {
                room = it->second;
            }
        }
    }
    
    if (!room) {
        for (int i = 0; i < SHARD_COUNT && !room; ++i) {
            sylar::RWMutex::WriteLock rlock(m_roomShards[i].mutex);
            for (auto& pair : m_roomShards[i].rooms) {
                if (!pair.second->isFull() && !pair.second->hasStarted()) {
                    if (pair.second->addPlayer(player, team)) {
                        room = pair.second;
                        break;
                    }
                }
            }
        }
    }
    
    if (!room) {
        uint32_t id = m_nextRoomId++;
        uint32_t rShardIdx = id % SHARD_COUNT;
        room = std::make_shared<GameRoom>(id);
        room->startActor();
        if (room->addPlayer(player, team)) {
            sylar::RWMutex::WriteLock rlock(m_roomShards[rShardIdx].mutex);
            m_roomShards[rShardIdx].rooms[id] = room;
        } else {
            return nullptr;
        }
    }
    
    {
        sylar::RWMutex::WriteLock plock(m_playerShards[pShardIdx].mutex);
        m_playerShards[pShardIdx].playerToRoom[pId] = room->getId();
    }
    
    broadcastRoomUpdate(room);
    return room;
}

void RoomManager::leaveRoom(uint32_t player_id) {
    uint32_t pShardIdx = player_id % SHARD_COUNT;
    uint32_t room_id = 0;

    {
        sylar::RWMutex::WriteLock plock(m_playerShards[pShardIdx].mutex);
        auto it = m_playerShards[pShardIdx].playerToRoom.find(player_id);
        if (it == m_playerShards[pShardIdx].playerToRoom.end()) return;
        room_id = it->second;
        m_playerShards[pShardIdx].playerToRoom.erase(it);
    }

    uint32_t rShardIdx = room_id % SHARD_COUNT;
    GameRoom::ptr room;
    {
        sylar::RWMutex::ReadLock rlock(m_roomShards[rShardIdx].mutex);
        auto room_it = m_roomShards[rShardIdx].rooms.find(room_id);
        if (room_it != m_roomShards[rShardIdx].rooms.end()) {
            room = room_it->second;
        }
    }

    if (!room) return;

    // [BugFix] 通过 Actor 队列执行 removePlayer，避免与 Actor 线程的 startGame/setReady 并发修改房间状态
    // 先从 playerToRoom 移除（上面已完成），再 post 到 Actor 安全修改 m_players/m_playerCount
    room->post([this, room, player_id, room_id, rShardIdx]() {
        room->removePlayer(player_id);

        if (room->isEmpty()) {
            room->stopActor();
            // 房间清理在 IOManager 线程安全执行（需持 shard 写锁）
            sylar::IOManager::GetThis()->schedule([this, room_id, rShardIdx]() {
                sylar::RWMutex::WriteLock rlock(m_roomShards[rShardIdx].mutex);
                m_roomShards[rShardIdx].rooms.erase(room_id);
            });
        } else {
            broadcastRoomUpdate(room);
        }
    });
}

GameRoom::ptr RoomManager::findRoomByPlayer(uint32_t player_id) const {
    uint32_t pShardIdx = player_id % SHARD_COUNT;
    uint32_t room_id = 0;
    {
        sylar::RWMutex::ReadLock plock(m_playerShards[pShardIdx].mutex);
        auto it = m_playerShards[pShardIdx].playerToRoom.find(player_id);
        if (it == m_playerShards[pShardIdx].playerToRoom.end()) return nullptr;
        room_id = it->second;
    }
    uint32_t rShardIdx = room_id % SHARD_COUNT;
    {
        sylar::RWMutex::ReadLock rlock(m_roomShards[rShardIdx].mutex);
        auto room_it = m_roomShards[rShardIdx].rooms.find(room_id);
        if (room_it != m_roomShards[rShardIdx].rooms.end()) return room_it->second;
    }
    return nullptr;
}

GameRoom::ptr RoomManager::createRoom(Player::ptr player, const std::string& room_name) {
    uint32_t pId = player->getId();
    uint32_t pShardIdx = pId % SHARD_COUNT;
    
    {
        sylar::RWMutex::ReadLock plock(m_playerShards[pShardIdx].mutex);
        if (m_playerShards[pShardIdx].playerToRoom.count(pId)) return nullptr;
    }
    
    uint32_t id = m_nextRoomId++;
    uint32_t rShardIdx = id % SHARD_COUNT;
    auto room = std::make_shared<GameRoom>(id, room_name);
    room->startActor();
    
    if (!room->addPlayer(player, ddt::TEAM_RED)) return nullptr;
    
    {
        sylar::RWMutex::WriteLock rlock(m_roomShards[rShardIdx].mutex);
        m_roomShards[rShardIdx].rooms[id] = room;
    }
    {
        sylar::RWMutex::WriteLock plock(m_playerShards[pShardIdx].mutex);
        m_playerShards[pShardIdx].playerToRoom[pId] = id;
    }
    
    broadcastRoomUpdate(room);
    return room;
}

std::vector<ddt::RoomInfo> RoomManager::getRoomList() const {
    std::vector<ddt::RoomInfo> result;
    for (int i = 0; i < SHARD_COUNT; ++i) {
        sylar::RWMutex::ReadLock rlock(m_roomShards[i].mutex);
        for (auto& pair : m_roomShards[i].rooms) {
            // [BugFix] 检测幽灵房间：如果房间标记为空但未被清理，跳过它
            if (pair.second->isEmpty()) continue;
            result.push_back(pair.second->getRoomInfo());
        }
    }
    return result;
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