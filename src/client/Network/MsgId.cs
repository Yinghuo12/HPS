// MsgId.cs — 消息 ID 常量(与 C++ src/common/msg_id.h、gate.proto 注释严格一致)
// 编号空间: 客户端请求与服务端响应/通知共用。
namespace Ddt.Net {
public static class MsgId {
    // 登录/注册
    public const ushort LOGIN            = 1;
    public const ushort LOGIN_RESP       = 2;
    // (REGISTER=3 客户端不发: 注册请求走 HTTP LoginClient.Register)
    public const ushort REGISTER_RESP    = 4;   // S->C 注册响应(LoginUI.OnRegisterResp 订阅)

    // 房间/大厅
    public const ushort ROOM_LIST        = 10;
    public const ushort ROOM_LIST_RESP   = 11;
    public const ushort CREATE_ROOM      = 12;
    public const ushort CREATE_ROOM_RESP = 13;
    public const ushort JOIN_ROOM        = 14;
    public const ushort JOIN_ROOM_RESP   = 15;
    public const ushort LEAVE_ROOM       = 16;
    public const ushort LEAVE_ROOM_RESP  = 17;
    public const ushort READY            = 18;
    // (READY_NOTIFY=19 客户端不用: 服务端只发 ROOM_UPDATE)
    public const ushort SWITCH_TEAM      = 20;
    public const ushort SWITCH_TEAM_RESP = 21;
    public const ushort ROOM_UPDATE      = 22;
    public const ushort ROOM_LIST_NOTIFY = 23;   // S->C 全量房间列表推送(大厅实时刷新)
    public const ushort LOGOUT            = 24;   // C->S 退出登录
    public const ushort KICK_NOTIFY       = 25;   // S->C 顶号通知(同账号新登录踢旧)
    // (LOGOUT_RESP=26 客户端不订阅: SendLogout 后直接切场景不等响应)
    public const ushort SET_GENDER        = 27;   // C->S 设置性别/角色
    public const ushort SET_GENDER_RESP   = 28;   // S->C 设置性别响应
    public const ushort SWITCH_WEAPON      = 29;   // C->S 房间内切换武器
    // (SWITCH_WEAPON_RESP=50 客户端不订阅)

    // 战斗
    public const ushort ROOM_READY_NOTIFY    = 30;
    public const ushort TURN_START_NOTIFY    = 31;
    public const ushort SHOOT                = 32;
    public const ushort SHOOT_RESULT_NOTIFY  = 33;
    public const ushort MOVE                 = 34;
    public const ushort MOVE_NOTIFY          = 35;
    public const ushort PASS                 = 36;
    public const ushort GAME_OVER_NOTIFY     = 37;
    public const ushort OPPONENT_LEFT_NOTIFY = 38;
    public const ushort AIM_BEGIN          = 39;   // C->S 蓄力开始(服务端重置回合计时)

    // 社交/聊天
    public const ushort CHAT             = 40;
    public const ushort CHAT_NOTIFY      = 41;
    // (CHAT_HISTORY=42 / CHAT_HISTORY_RESP=43 客户端不用)
    public const ushort PRIVATE_CHAT     = 44;
    // (FRIEND_ADD=45 / FRIEND_ADD_RESP=46 / FRIEND_LIST=47 / FRIEND_LIST_RESP=48 客户端不用)

    // 通用
    public const ushort ERROR            = 90;
    public const ushort HEARTBEAT        = 91;
    public const ushort HEARTBEAT_RESP   = 92;
}
}
