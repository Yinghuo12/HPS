// GameFacade.cs — 业务门面: 封装发消息的便捷方法
//
// 调用方只需关心业务语义(Login/JoinRoom/Shoot),不用管 msg_id 和 protobuf 组包。
// 所有方法都经 NetworkManager.Instance.Send 发送。
using Ddt;
using UnityEngine;

namespace Ddt.Net.Game {
public static class GameFacade {
    // ---- 登录(TCP 首包, 发 token) ----
    public static void SendLogin(string token) {
        NetworkManager.Instance.Send(MsgId.LOGIN, new LoginReq { Token = token });
    }
    // ---- 退出登录 ----
    public static void SendLogout() {
        NetworkManager.Instance.Send(MsgId.LOGOUT, new LogoutReq());
    }
    // ---- 设置性别/角色 ----
    public static void SendSetGender(Gender gender) {
        NetworkManager.Instance.Send(MsgId.SET_GENDER, new SetGenderReq { Gender = gender });
    }

    // ---- 房间 ----
    public static void SendRoomList() {
        NetworkManager.Instance.Send(MsgId.ROOM_LIST, new RoomListReq());
    }
    public static void SendCreateRoom(string roomName, string mode = "custom") {
        NetworkManager.Instance.Send(MsgId.CREATE_ROOM, new CreateRoomReq { RoomName = roomName, Mode = mode });
    }
    public static void SendJoinRoom(uint roomId, TeamSide team) {
        NetworkManager.Instance.Send(MsgId.JOIN_ROOM, new JoinRoomReq { RoomId = roomId, Team = team });
    }
    public static void SendLeaveRoom() {
        NetworkManager.Instance.Send(MsgId.LEAVE_ROOM, new LeaveRoomReq());
    }
    public static void SendReady(bool ready) {
        NetworkManager.Instance.Send(MsgId.READY, new ReadyReq { Ready = ready });
    }
    public static void SendSwitchTeam(TeamSide team) {
        NetworkManager.Instance.Send(MsgId.SWITCH_TEAM, new SwitchTeamReq { Team = team });
    }
    public static void SendSwitchWeapon(int weaponId) {
        NetworkManager.Instance.Send(MsgId.SWITCH_WEAPON, new SwitchWeaponReq { WeaponId = weaponId });
    }

    // ---- 战斗 ----
    public static void SendShoot(int angle, double force, bool isFly, int weaponId = 1) {
        NetworkManager.Instance.Send(MsgId.SHOOT, new ShootReq { Angle = angle, Force = force, IsFly = isFly, WeaponId = weaponId });
    }
    public static void SendMove(float deltaX) {
        NetworkManager.Instance.Send(MsgId.MOVE, new MoveReq { DeltaX = deltaX });
    }
    public static void SendPass() {
        NetworkManager.Instance.Send(MsgId.PASS, new PassReq());
    }
    // 蓄力开始: 通知服务端重置回合计时(给蓄力预留完整时间)
    public static void SendAimBegin() {
        NetworkManager.Instance.Send(MsgId.AIM_BEGIN, new PassReq());
    }

    // ---- 社交 ----
    public static void SendChat(ChannelType channel, string message) {
        NetworkManager.Instance.Send(MsgId.CHAT, new ChatReq { Channel = channel, Message = message });
    }
    public static void SendPrivateChat(ulong targetAccountId, string message) {
        NetworkManager.Instance.Send(MsgId.PRIVATE_CHAT,
            new PrivateChatReq { TargetAccountId = targetAccountId, Message = message });
    }
}
}
