#ifndef __DDT_LOBBY_SERVICE_H__
#define __DDT_LOBBY_SERVICE_H__

#include <atomic>
#include <map>
#include <memory>
#include <string>

#include "rpc.pb.h"
#include "routing.h"
#include "sylar/core/thread.h"
#include "sylar/rpc/rpc_channel.h"

namespace ddt {

// 大厅房间(匹配阶段; 开战后转交 battle 服)
struct LobbyRoom {
    uint32_t roomId = 0;
    std::string name;
    std::string mode = "custom";   // "custom" / "match" / "pve"
    bool started = false;

    struct Seat {
        uint64_t accountId = 0;
        std::string name;
        TeamSide team = TEAM_RED;
        bool ready = false;
        uint64_t gatewayId = 0;
        Gender gender = GENDER_NONE;
        int weaponId = 1;   // 默认 ice_cream
    };

    static constexpr int kMaxSeats = 8;   // 8 席(红蓝各 4), 对应客户端 8 格房间 UI
    Seat seats[kMaxSeats];
    int seatCount = 0;
    std::string mapName = "rainbow";   // 默认地图背景

    RoomInfo toRoomInfo() const;
};

// LobbyService: 房间/匹配/好友/聊天。房间状态用 RWMutex 保护, 经 PushService 推送。
class LobbyServiceImpl : public LobbyService {
public:
    typedef std::shared_ptr<LobbyServiceImpl> ptr;

    explicit LobbyServiceImpl(const std::string& etcdEndpoint);
    ~LobbyServiceImpl();

    // 设置推送闭包: 由 lobby_main 注入(调用 gate 的 PushService)
    void setPushFn(PushFn fn) {
        m_push = fn;
    }

    // 设置"广播给所有在线玩家"闭包(调用 gate 的 NotifyAllOnline)
    using PushAllFn = std::function<void(uint16_t msg_id, const std::string& payload)>;

    void setPushAllFn(PushAllFn fn) {
        m_pushAll = fn;
    }

    // ---- RPC 方法 ----
    void RoomList(::google::protobuf::RpcController*, const RoomListRpcReq*, RoomListRpcResp*, ::google::protobuf::Closure*) override;
    void CreateRoom(::google::protobuf::RpcController*, const CreateRoomRpcReq*, CreateRoomRpcResp*, ::google::protobuf::Closure*) override;
    void JoinRoom(::google::protobuf::RpcController*, const JoinRoomRpcReq*, JoinRoomRpcResp*, ::google::protobuf::Closure*) override;
    void LeaveRoom(::google::protobuf::RpcController*, const LeaveRoomRpcReq*, LeaveRoomRpcResp*, ::google::protobuf::Closure*) override;
    void Ready(::google::protobuf::RpcController*, const ReadyRpcReq*, ResultResp*, ::google::protobuf::Closure*) override;
    void SwitchTeam(::google::protobuf::RpcController*, const SwitchTeamRpcReq*, ResultResp*, ::google::protobuf::Closure*) override;
    void SwitchWeapon(::google::protobuf::RpcController*, const SwitchWeaponRpcReq*, ResultResp*, ::google::protobuf::Closure*) override;
    void FriendAdd(::google::protobuf::RpcController*, const FriendAddRpcReq*, FriendAddRpcResp*, ::google::protobuf::Closure*) override;
    void FriendList(::google::protobuf::RpcController*, const IdReq*, FriendListRpcResp*, ::google::protobuf::Closure*) override;
    void Chat(::google::protobuf::RpcController*, const ChatRpcReq*, ResultResp*, ::google::protobuf::Closure*) override;
    void PrivateChat(::google::protobuf::RpcController*, const PrivateChatRpcReq*, ResultResp*, ::google::protobuf::Closure*) override;
    void ChatHistory(::google::protobuf::RpcController*, const GetChatHistoryReq*, ChatHistoryRespRpc*, ::google::protobuf::Closure*) override;

private:
    // 找到某玩家所在的房间(锁内调用)
    LobbyRoom* findRoomByAccountLocked(uint64_t accountId);
    // 广播房间更新通知给房内全部玩家
    void broadcastRoomUpdate(uint32_t roomId);
    // 全量房间列表推给所有在线玩家(大厅实时刷新)
    void broadcastRoomListToAll();
    // 尝试开局: 双方就绪且有人则转交 battle
    void tryStart(uint32_t roomId);

    std::shared_ptr<sylar::rpc::RpcChannel> dataChannel();
    std::shared_ptr<sylar::rpc::RpcChannel> battleChannel();

    std::string m_etcdEndpoint;
    std::shared_ptr<sylar::rpc::RpcChannel> m_dataChannel;
    std::shared_ptr<sylar::rpc::RpcChannel> m_battleChannel;
    std::mutex m_channelMutex;

    mutable sylar::RWMutex m_mutex;
    std::map<uint32_t, std::shared_ptr<LobbyRoom>> m_rooms;
    std::atomic<uint32_t> m_nextRoomId{1000};

    PushFn m_push;
    PushAllFn m_pushAll;   // 广播给所有在线玩家(世界聊天用)
};

}  // namespace ddt

#endif
