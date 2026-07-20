#ifndef __DDT_MSG_ID_H__
#define __DDT_MSG_ID_H__

#include <cstdint>

// 注意: 此处值与 src/proto/gate.proto 的 MsgId 枚举保持一致
// 之所以手写一份(而非直接用 proto 生成的 MsgId), 是因为 frame 层不依赖 proto 头,
// 网关分发时只需 msg_id 数值即可路由
namespace ddt {

enum MsgId : uint16_t {
    MSG_LOGIN                = 1,
    MSG_LOGIN_RESP           = 2,
    MSG_REGISTER             = 3,
    MSG_REGISTER_RESP        = 4,

    MSG_ROOM_LIST            = 10,
    MSG_ROOM_LIST_RESP       = 11,
    MSG_CREATE_ROOM          = 12,
    MSG_CREATE_ROOM_RESP     = 13,
    MSG_JOIN_ROOM            = 14,
    MSG_JOIN_ROOM_RESP       = 15,
    MSG_LEAVE_ROOM           = 16,
    MSG_LEAVE_ROOM_RESP      = 17,
    MSG_READY                = 18,
    MSG_READY_NOTIFY         = 19,
    MSG_SWITCH_TEAM          = 20,
    MSG_SWITCH_TEAM_RESP     = 21,
    MSG_ROOM_UPDATE          = 22,
    MSG_ROOM_LIST_NOTIFY     = 23,   // S->C 全量房间列表推送(大厅实时刷新)
    MSG_LOGOUT               = 24,   // C->S 退出登录
    MSG_KICK_NOTIFY          = 25,   // S->C 顶号通知(同账号新登录踢旧连接)
    MSG_LOGOUT_RESP          = 26,   // S->C 退出登录响应
    MSG_SET_GENDER           = 27,   // C->S 设置性别/角色
    MSG_SET_GENDER_RESP      = 28,   // S->C 设置性别响应
    MSG_SWITCH_WEAPON        = 29,   // C->S 房间内切换武器
    MSG_SWITCH_WEAPON_RESP   = 50,   // S->C 武器切换响应

    MSG_ROOM_READY_NOTIFY    = 30,
    MSG_TURN_START_NOTIFY    = 31,
    MSG_SHOOT                = 32,
    MSG_SHOOT_RESULT_NOTIFY  = 33,
    MSG_MOVE                 = 34,
    MSG_MOVE_NOTIFY          = 35,
    MSG_PASS                 = 36,
    MSG_GAME_OVER_NOTIFY     = 37,
    MSG_OPPONENT_LEFT_NOTIFY = 38,
    MSG_AIM_BEGIN            = 39,   // C->S 蓄力开始(服务端重置回合计时, 给蓄力预留时间)

    MSG_CHAT                 = 40,
    MSG_CHAT_NOTIFY          = 41,
    MSG_CHAT_HISTORY         = 42,
    MSG_CHAT_HISTORY_RESP    = 43,
    MSG_PRIVATE_CHAT         = 44,
    MSG_FRIEND_ADD           = 45,
    MSG_FRIEND_ADD_RESP      = 46,
    MSG_FRIEND_LIST          = 47,
    MSG_FRIEND_LIST_RESP     = 48,

    MSG_ERROR                = 90,
    MSG_HEARTBEAT            = 91,
    MSG_HEARTBEAT_RESP       = 92,
};

} // namespace ddt

#endif
