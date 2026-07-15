// Session.cs — 跨场景会话状态(静态)
//
// 存登录后/重连需要的信息。所有字段静态, 切场景不丢。
// 重连时复用 Token 直接重新 LOGIN(无需再走 HTTP /login)。
namespace Ddt.Net {
public static class Session {
    public static ulong MyAccountId;
    public static string MyName;
    /// <summary>登录 token(HTTP /login 拿到)。重连时复用它直接发 LOGIN, 不再走 HTTP。</summary>
    public static string Token;
    /// <summary>性别/角色: 0=未选择 1=男 2=女。0 时弹角色选择界面。</summary>
    public static int Gender;
    /// <summary>当前武器: 1=ice_cream 2=projectile。默认 ice_cream。</summary>
    public static int MyWeaponId = 1;
    /// <summary>缓存的 ROOM_READY_NOTIFY 原始字节(场景切换后 BattleController 重放)。</summary>
    public static byte[] PendingRoomReady;
    /// <summary>缓存的 TURN_START_NOTIFY 原始字节(场景切换后 BattleController 重放)。</summary>
    public static byte[] PendingTurnStart;

    // 重连后回到正确场景的提示(当前未用于自动跳场景, 仅记录)
    public static bool IsInLobby;
    public static bool IsInBattle;

    // 房间状态(建房/加入成功写入, 离开清零, 重连后据此恢复界面)
    public static bool InRoom;
    public static uint RoomId;

    public static void Clear() {
        MyAccountId = 0;
        MyName = null;
        Token = null;
        Gender = 0;
        MyWeaponId = 1;
        PendingRoomReady = null;
        PendingTurnStart = null;
        IsInLobby = false;
        IsInBattle = false;
        InRoom = false;
        RoomId = 0;
    }
}
}
