// LobbyUI.cs — 大厅界面(两段式状态机: LOBBY ↔ IN_ROOM)
//
// LOBBY 状态:
//   - 订阅 MSG_ROOM_LIST_NOTIFY → 全量刷新房间列表(实时推送)
//   - 房名输入 + 统一的“加入房间”按钮 (进房后自选红蓝队，符合弹弹堂标准)
//
// IN_ROOM 状态 (复刻弹弹堂 2x2 经典对称等候室):
//   - 动态生成 8 个红蓝卡片格子 (空位显示 Open，有玩家显示方形色块 + 斜印红色 READY 印章)
//   - 换队伍按钮 / 准备-取消准备按钮 / 离开按钮
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.UI;
using UnityEngine.SceneManagement;
using Ddt;
using Ddt.Net.Game;

namespace Ddt.Net.UI {
public class LobbyUI : MonoBehaviour {
    [Header("场景名")]
    public string battleScene = "BattleScene";

    // ---- 状态机 ----
    private enum State { LOBBY, IN_ROOM }
    private State state_ = State.LOBBY;

    // 对象是否已销毁: 进战斗场景后 LobbyUI 的 GameObject 被销毁, 但订阅的委托仍被
    // MessageDispatcher(DontDestroyOnLoad 单例)持有 → 僵尸回调。此标志让回调提前返回,
    // 避免访问已销毁的 roomPanel_ 等 Unity 对象触发 NullReferenceException。
    private bool destroyed_ = false;
    // 开局后首回合通知是否已缓存(防多次开局叠加订阅)
    private bool turnStartCached_ = false;

    // ---- 当前房间信息 ----
    private uint currentRoomId_ = 0;
    private string currentRoomName_ = "";
    private RoomInfo currentRoom_;   // 最新收到的房间快照

    // ---- LOBBY UI 元素 ----
    private GameObject lobbyPanel_;
    private Text roomListText_;
    private InputField roomNameInput_;
    private Text lobbyStatusText_;

    // ---- IN_ROOM UI 元素 ----
    private GameObject roomPanel_;
    private Text roomTitleText_;
    private Text redTeamText_;
    private Text blueTeamText_;
    private Text readyBtnLabel_;
    private Text roomStatusText_;

    // ---- 动态生成的卡片格子列表 ----
    private List<GameObject> roomSlots_ = new List<GameObject>();

    // ---- 日志面板 ----
    private GameObject logPanel_;
    private Text logText_;
    private bool logExpanded_ = false;
    private int lastLogVersion_ = -1;

    private List<RoomInfo> lobbyRooms_ = new List<RoomInfo>();
    private List<GameObject> roomRowButtons_ = new List<GameObject>();

    // 建房模式
    private string selectedMode_ = "custom";
    private Button modeCustomBtn_;

    // 背包面板
    private GameObject backpackPanel_;
    private Image weapon1Highlight_, weapon2Highlight_;

    // 玩家格子位置记忆: accountId → (redSlot, blueSlot), 换队不抢占别人位置
    // 每个玩家在红队和蓝队各有一个稳定格子索引, 换队后显示在另一队的空闲格
    private Dictionary<ulong, int> playerRedSlot_ = new Dictionary<ulong, int>();
    private Dictionary<ulong, int> playerBlueSlot_ = new Dictionary<ulong, int>();

    void Start() {
        EnsureEventSystem();
        // 去掉 Skybox 3D 背景: 主相机改纯色(UI 场景)
        var cam = Camera.main;
        if (cam != null) {
            cam.clearFlags = CameraClearFlags.SolidColor;
            cam.backgroundColor = new Color(0.15f, 0.2f, 0.3f);
        }
        BuildUI();
        var disp = NetworkManager.Instance.Dispatcher;

        // 大厅实时列表推送
        disp.Subscribe(MsgId.ROOM_LIST_NOTIFY, OnRoomListNotify);
        disp.Subscribe(MsgId.ROOM_LIST_RESP, OnRoomListResp);
        
        // 房间内更新(玩家加入/离开/换队/准备) —— 服务端 ready/switchTeam 后
        // 只发 MSG_ROOM_UPDATE(含完整 PlayerSlot[].ready), 不发 READY_NOTIFY。
        disp.Subscribe(MsgId.ROOM_UPDATE, OnRoomUpdate);

        // RPC 响应
        disp.Subscribe(MsgId.CREATE_ROOM_RESP, OnCreateRoom);
        disp.Subscribe(MsgId.JOIN_ROOM_RESP, OnJoinRoom);
        disp.Subscribe(MsgId.LEAVE_ROOM_RESP, OnLeaveRoom);
        disp.Subscribe(MsgId.SWITCH_TEAM_RESP, OnSwitchTeam);

        // 顶号: 同账号新登录, 旧连接被踢 → 回登录界面
        disp.Subscribe(MsgId.KICK_NOTIFY, bytes => {
            var m = KickNotify.Parser.ParseFrom(bytes);
            ClientLogger.Error("被顶号踢出: " + m.Msg);
            NetworkManager.Instance.StopReconnectAndRelogin();
            NetworkManager.Instance.Client.Close();
            Session.Clear();
            SceneManager.LoadScene("LoginScene");
        });
        
        // 开局
        disp.Subscribe(MsgId.ROOM_READY_NOTIFY, bytes => {
            var m = RoomReadyNotify.Parser.ParseFrom(bytes);
            ClientLogger.Info($"开局! 进入战斗 (房间 {m.RoomId})");
            // 缓存原始字节到 Session, BattleController 在新场景 Start 时重放
            Ddt.Net.Session.PendingRoomReady = bytes;
            // 重置首回合缓存标志: 等紧随其后的 TURN_START_NOTIFY 到达时缓存
            Ddt.Net.Session.PendingTurnStart = null;
            turnStartCached_ = false;
            NetworkManager.Instance.StartTransition();  // 开始转场拦截，防止加载期间丢失服务端下发的任何包
            SceneManager.LoadScene(battleScene);
        });

        // 首回合通知: 服务端 startGame 后立刻 nextTurn 发 TurnStartNotify。
        // 原实现嵌在 ROOM_READY_NOTIFY 回调内订阅(每次开局叠加一个 lambda → 多次开局后
        // 多个 lambda 抢写 PendingTurnStart)。提到顶层订阅一次, 用标志位确保只缓存开局后第一次。
        disp.Subscribe(MsgId.TURN_START_NOTIFY, tsBytes => {
            if (!turnStartCached_) {
                Ddt.Net.Session.PendingTurnStart = tsBytes;
                turnStartCached_ = true;
            }
        });
        
        // 重连恢复
        disp.Subscribe(MsgId.LOGIN_RESP, bytes => {
            var m = LoginResp.Parser.ParseFrom(bytes);
            if (m.Result == (int)Result.Success) {
                ClientLogger.Info("重连成功, 恢复大厅状态");
                // 修复：必须恢复心跳标记，否则45秒后会被服务端当作超时踢掉！
                NetworkManager.Instance.MarkLoggedIn(); 
                if (Session.InRoom && Session.RoomId > 0) {
                    currentRoomId_ = Session.RoomId;
                    EnterRoom(Session.RoomId, "重连房间");
                }
                GameFacade.SendRoomList();
            }
        });
        
        disp.Subscribe(MsgId.ERROR, bytes => {
            var m = ErrorNotify.Parser.ParseFrom(bytes);
            ClientLogger.Error("错误: " + m.Msg);
            if (state_ == State.LOBBY) SetLobbyStatus("失败: " + m.Msg);
        });

        if (Session.InRoom && Session.RoomId > 0) {
            currentRoomId_ = Session.RoomId;
            EnterRoom(Session.RoomId, "恢复房间");
        }
        GameFacade.SendRoomList();
        SwitchState(State.LOBBY);

        // 聊天面板(左下角, 自包含: 自订阅 MSG_CHAT_NOTIFY)
        var chatCanvas = FindFirstObjectByType<Canvas>();
        if (chatCanvas != null) UIChat.Create(chatCanvas);
    }

    void Update() {
        ClientLogger.Flush();
        UpdateLogPanel();
    }

    void OnDestroy() {
        // 标记已销毁: 之后 MessageDispatcher 调到本对象的僵尸回调时, 各 OnXxx 会据此提前返回。
        destroyed_ = true;
        // 反注册订阅: MessageDispatcher 是 DontDestroyOnLoad 单例, 不退订会导致进战斗后
        // 本 UI 已销毁但 dispatcher 仍持有僵尸回调(destroyed_ 守卫是兜底, 显式退订才是根治)。
        var disp = NetworkManager.Instance != null ? NetworkManager.Instance.Dispatcher : null;
        if (disp != null) {
            disp.Unsubscribe(MsgId.ROOM_LIST_NOTIFY);
            disp.Unsubscribe(MsgId.ROOM_LIST_RESP);
            disp.Unsubscribe(MsgId.ROOM_UPDATE);
            disp.Unsubscribe(MsgId.CREATE_ROOM_RESP);
            disp.Unsubscribe(MsgId.JOIN_ROOM_RESP);
            disp.Unsubscribe(MsgId.LEAVE_ROOM_RESP);
            disp.Unsubscribe(MsgId.SWITCH_TEAM_RESP);
            disp.Unsubscribe(MsgId.KICK_NOTIFY);
            disp.Unsubscribe(MsgId.ROOM_READY_NOTIFY);
            disp.Unsubscribe(MsgId.TURN_START_NOTIFY);
            disp.Unsubscribe(MsgId.LOGIN_RESP);
            disp.Unsubscribe(MsgId.ERROR);
        }
    }

    // ============ 状态切换 ============

    private void SwitchState(State s) {
        state_ = s;
        if (lobbyPanel_) lobbyPanel_.SetActive(s == State.LOBBY);
        if (roomPanel_) roomPanel_.SetActive(s == State.IN_ROOM);
        ClientLogger.Info($"UI 状态 → {s}");
    }

    private void EnterRoom(uint roomId, string roomName) {
        currentRoomId_ = roomId;
        currentRoomName_ = roomName;
        Session.InRoom = true;
        Session.RoomId = roomId;
        SwitchState(State.IN_ROOM);
        RefreshRoomPanel();
    }

    private void LeaveRoomToLocal() {
        currentRoomId_ = 0;
        currentRoomName_ = "";
        currentRoom_ = null;
        Session.InRoom = false;
        Session.RoomId = 0;
        SwitchState(State.LOBBY);
    }

    // ============ 通知与响应处理 ============

    private void OnRoomListNotify(byte[] bytes) {
        var m = RoomListResp.Parser.ParseFrom(bytes);
        lobbyRooms_.Clear();
        foreach (var r in m.Rooms) lobbyRooms_.Add(r);
        RefreshLobbyRoomList();
    }

    private void OnRoomListResp(byte[] bytes) {
        var m = RoomListResp.Parser.ParseFrom(bytes);
        lobbyRooms_.Clear();
        foreach (var r in m.Rooms) lobbyRooms_.Add(r);
        RefreshLobbyRoomList();
    }

    private void OnRoomUpdate(byte[] bytes) {
        if (destroyed_) return;   // 进战斗后本 UI 已销毁, 忽略僵尸回调
        var m = RoomUpdateNotify.Parser.ParseFrom(bytes);
        if (m.RoomInfo == null) return;

        // 只要房间列表含我们自己，就强制同步状态
        bool containsMe = false;
        foreach (var p in m.RoomInfo.Players) {
            if (p.AccountId == Session.MyAccountId) { containsMe = true; break; }
        }

        if (containsMe) {
            currentRoom_ = m.RoomInfo;
            currentRoomId_ = m.RoomInfo.RoomId;
            currentRoomName_ = m.RoomInfo.RoomName;
            Session.InRoom = true;
            Session.RoomId = m.RoomInfo.RoomId;
            if (state_ != State.IN_ROOM) SwitchState(State.IN_ROOM);
            RefreshRoomPanel();
        }
    }

    private void OnCreateRoom(byte[] bytes) {
        var m = CreateRoomResp.Parser.ParseFrom(bytes);
        if (m.Result == (int)Result.Success) {
            ClientLogger.Info($"建房成功 room_id={m.RoomId}");
            EnterRoom(m.RoomId, roomNameInput_ != null ? roomNameInput_.text : "新房间");
        } else {
            ClientLogger.Error("建房失败: " + m.Msg);
        }
    }

    private void OnJoinRoom(byte[] bytes) {
        var m = JoinRoomResp.Parser.ParseFrom(bytes);
        if (m.Result == (int)Result.Success) {
            ClientLogger.Info($"加入房间 {m.RoomId}");
            EnterRoom(m.RoomId, "房间#" + m.RoomId);
        } else {
            ClientLogger.Error("加入失败: " + m.Msg);
        }
    }

    private void OnLeaveRoom(byte[] bytes) {
        var m = LeaveRoomResp.Parser.ParseFrom(bytes);
        if (m.Result == (int)Result.Success) {
            ClientLogger.Info("已离开房间");
            LeaveRoomToLocal();
        } else {
            ClientLogger.Error("离开失败: " + m.Msg);
        }
    }

    private void OnSwitchTeam(byte[] bytes) {
        var m = SwitchTeamResp.Parser.ParseFrom(bytes);
        if (m.Result != (int)Result.Success) {
            ClientLogger.Warn("换队失败: " + m.Msg);
        }
    }

    // ============ UI 渲染 ============

    private void RefreshLobbyRoomList() {
        if (roomListText_ == null) return;
        // 清除旧的房间行按钮
        foreach (var go in roomRowButtons_) { if (go != null) Destroy(go); }
        roomRowButtons_.Clear();

        if (lobbyRooms_.Count == 0) {
            roomListText_.text = "(暂无房间, 建一个吧)";
            return;
        }
        roomListText_.text = "";
        // 动态生成可点击的房间行
        float yPos = 220f;
        foreach (var r in lobbyRooms_) {
            uint rid = r.RoomId;
            string label = $"#{r.RoomId}  {r.RoomName}  {r.PlayerCount}/{r.MaxPlayers}人 {(r.GameStarted ? "(进行中)" : "")}";
            var btn = MakeButton(lobbyPanel_.transform, $"Room_{rid}", new Vector2(0, yPos), label, () => OnJoinRoomRow(rid));
            btn.GetComponent<RectTransform>().sizeDelta = new Vector2(700, 36);
            roomRowButtons_.Add(btn.gameObject);
            yPos -= 40f;
        }
    }

    private void OnJoinRoomRow(uint rid) {
        ClientLogger.Info($"点击加入房间 {rid}");
        GameFacade.SendJoinRoom(rid, TeamSide.TeamRed);
    }

    private void RefreshRoomPanel() {
        if (currentRoom_ == null) {
            if (roomTitleText_) roomTitleText_.text = $"房间 #{currentRoomId_} ({currentRoomName_})";
            foreach (var go in roomSlots_) { if (go != null) Destroy(go); }
            roomSlots_.Clear();
            return;
        }
        if (roomTitleText_) roomTitleText_.text = $"{currentRoom_.RoomName}  #{currentRoom_.RoomId}  ({currentRoom_.PlayerCount}/{currentRoom_.MaxPlayers})";

        RefreshRoomSlots();

        bool myReady = false;
        foreach (var p in currentRoom_.Players) {
            if (p.AccountId == Session.MyAccountId) { myReady = p.Ready; Session.MyWeaponId = p.WeaponId; break; }
        }
        if (readyBtnLabel_) readyBtnLabel_.text = myReady ? "取消准备" : "准备";
        if (backpackPanel_ != null && backpackPanel_.activeSelf) UpdateBackpackHighlight();
    }

    // 稳定格子排版: 每个玩家在各自队伍有固定格子索引, 换队后在新队伍的空闲格出现
    private void RefreshRoomSlots() {
        foreach (var go in roomSlots_) { if (go != null) Destroy(go); }
        roomSlots_.Clear();

        PlayerSlot[] redTeam = new PlayerSlot[4];
        PlayerSlot[] blueTeam = new PlayerSlot[4];

        if (currentRoom_ != null) {
            // 清理已离开房间的玩家的格子记忆
            var stillInRoom = new System.Collections.Generic.HashSet<ulong>();
            foreach (var p in currentRoom_.Players) stillInRoom.Add(p.AccountId);
            CleanupSlotMemory(playerRedSlot_, stillInRoom);
            CleanupSlotMemory(playerBlueSlot_, stillInRoom);

            foreach (var p in currentRoom_.Players) {
                int slot = AssignSlot(p);
                if (p.Team == TeamSide.TeamRed) {
                    if (slot < 4) redTeam[slot] = p;
                } else {
                    if (slot < 4) blueTeam[slot] = p;
                }
            }
        }

        Vector2[] redPos = {
            new Vector2(-290, 170), new Vector2(-110, 170),
            new Vector2(-290, -30), new Vector2(-110, -30)
        };
        for (int i = 0; i < 4; i++) CreateSlotCard(redTeam[i], redPos[i], TeamSide.TeamRed);

        Vector2[] bluePos = {
            new Vector2(110, 170), new Vector2(290, 170),
            new Vector2(110, -30), new Vector2(290, -30)
        };
        for (int i = 0; i < 4; i++) CreateSlotCard(blueTeam[i], bluePos[i], TeamSide.TeamBlue);
    }

    // 为玩家分配稳定格子: 当前队伍的已有索引, 或第一个空闲格
    private int AssignSlot(PlayerSlot p) {
        var teamMap = (p.Team == TeamSide.TeamRed) ? playerRedSlot_ : playerBlueSlot_;
        var otherMap = (p.Team == TeamSide.TeamRed) ? playerBlueSlot_ : playerRedSlot_;

        // 如果在当前队伍已有格子, 继续用
        if (teamMap.TryGetValue(p.AccountId, out int existing)) return existing;

        // 找第一个空闲格子
        var occupied = new System.Collections.Generic.HashSet<int>();
        foreach (var kv in teamMap) if (kv.Key != p.AccountId) occupied.Add(kv.Value);
        int slot = 0;
        while (slot < 4 && occupied.Contains(slot)) slot++;

        // 从另一队清除(如果之前在那队有过格子)
        otherMap.Remove(p.AccountId);
        teamMap[p.AccountId] = slot;
        return slot;
    }

    private void CleanupSlotMemory(Dictionary<ulong, int> map, System.Collections.Generic.HashSet<ulong> stillInRoom) {
        var toRemove = new System.Collections.Generic.List<ulong>();
        foreach (var kv in map) if (!stillInRoom.Contains(kv.Key)) toRemove.Add(kv.Key);
        foreach (var k in toRemove) map.Remove(k);
    }

    private void CreateSlotCard(PlayerSlot p, Vector2 pos, TeamSide team) {
        var card = new GameObject("SlotCard", typeof(RectTransform)); // 极其重要：父级必须是 RectTransform
        card.transform.SetParent(roomPanel_.transform, false);
        card.AddComponent<CanvasRenderer>();
        var img = card.AddComponent<Image>();

        var rt = card.GetComponent<RectTransform>();
        rt.sizeDelta = new Vector2(160, 170);
        rt.anchorMin = rt.anchorMax = new Vector2(0.5f, 0.5f);
        rt.anchoredPosition = pos;

        if (p == null) {
            img.color = new Color(0, 0, 0, 0.4f);
            var openGo = new GameObject("OpenLabel", typeof(RectTransform));
            openGo.transform.SetParent(card.transform, false);
            openGo.AddComponent<CanvasRenderer>();
            var openTxt = openGo.AddComponent<Text>();
            openTxt.font = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
            openTxt.fontSize = 24;
            openTxt.fontStyle = FontStyle.Bold;
            openTxt.color = new Color(1, 1, 1, 0.25f);
            openTxt.alignment = TextAnchor.MiddleCenter;
            openTxt.text = "✖\nOpen";
            
            var ort = openTxt.rectTransform;
            ort.anchorMin = Vector2.zero; ort.anchorMax = Vector2.one;
            ort.offsetMin = ort.offsetMax = Vector2.zero;
        } else {
            Color baseColor = (team == TeamSide.TeamRed) 
                ? new Color(0.85f, 0.35f, 0.35f, 1f) 
                : new Color(0.35f, 0.5f, 0.85f, 1f);
            img.color = baseColor;

            if (p.AccountId == Session.MyAccountId) {
                var outline = card.AddComponent<Outline>();
                outline.effectColor = Color.yellow;
                outline.effectDistance = new Vector2(4, 4);
            }

            var nameGo = new GameObject("Name", typeof(RectTransform));
            nameGo.transform.SetParent(card.transform, false);
            nameGo.AddComponent<CanvasRenderer>();
            var nameTxt = nameGo.AddComponent<Text>();
            nameTxt.font = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
            nameTxt.fontSize = 18;
            nameTxt.fontStyle = FontStyle.Bold;
            nameTxt.color = Color.white;
            nameTxt.alignment = TextAnchor.MiddleCenter;
            nameTxt.text = p.Name;
            
            var nrt = nameTxt.rectTransform;
            nrt.sizeDelta = new Vector2(140, 30);
            nrt.anchorMin = nrt.anchorMax = new Vector2(0.5f, 0.8f);
            nrt.anchoredPosition = Vector2.zero;

            var infoGo = new GameObject("Info", typeof(RectTransform));
            infoGo.transform.SetParent(card.transform, false);
            infoGo.AddComponent<CanvasRenderer>();
            var infoTxt = infoGo.AddComponent<Text>();
            infoTxt.font = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
            infoTxt.fontSize = 14;
            infoTxt.color = new Color(1, 1, 1, 0.8f);
            infoTxt.alignment = TextAnchor.MiddleCenter;
            infoTxt.text = $"ID: {p.AccountId % 10000}";
            
            var irt = infoTxt.rectTransform;
            irt.sizeDelta = new Vector2(140, 24);
            irt.anchorMin = irt.anchorMax = new Vector2(0.5f, 0.2f);
            irt.anchoredPosition = Vector2.zero;

            if (p.Ready) {
                var readyGo = new GameObject("ReadyStamp", typeof(RectTransform));
                readyGo.transform.SetParent(card.transform, false);
                readyGo.AddComponent<CanvasRenderer>();
                var readyTxt = readyGo.AddComponent<Text>();
                readyTxt.font = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
                readyTxt.fontSize = 28;
                readyTxt.fontStyle = FontStyle.Bold;
                readyTxt.color = new Color(1f, 0.15f, 0.15f, 0.9f);
                readyTxt.alignment = TextAnchor.MiddleCenter;
                readyTxt.text = "READY";

                var outline = readyGo.AddComponent<Outline>();
                outline.effectColor = Color.white;
                outline.effectDistance = new Vector2(1.5f, 1.5f);

                var rrt = readyTxt.rectTransform;
                rrt.sizeDelta = new Vector2(140, 50);
                rrt.anchorMin = rrt.anchorMax = new Vector2(0.5f, 0.5f);
                rrt.anchoredPosition = Vector2.zero;
                rrt.localRotation = Quaternion.Euler(0, 0, 15f);
            }
        }

        roomSlots_.Add(card);
    }

    // ============ 按钮事件 ============

    public void OnCreateClick() {
        string name = (roomNameInput_ != null && !string.IsNullOrEmpty(roomNameInput_.text))
                      ? roomNameInput_.text : "新房间";
        ClientLogger.Info($"请求建房: {name} 模式={selectedMode_}");
        GameFacade.SendCreateRoom(name, selectedMode_);
    }

    // 问题 2：合并大厅按钮，点击即申请加入
    public void OnJoinClick() {
        uint rid = ParseRoomIdFromInput();
        if (rid > 0) {
            ClientLogger.Info($"请求加入房间 {rid}");
            GameFacade.SendJoinRoom(rid, TeamSide.TeamRed);
        }
    }

    public void OnLeaveClick() {
        ClientLogger.Info("请求离开房间");
        GameFacade.SendLeaveRoom();
    }

    public void OnReadyClick() {
        bool curReady = false;
        if (currentRoom_ != null) {
            foreach (var p in currentRoom_.Players) {
                if (p.AccountId == Session.MyAccountId) { curReady = p.Ready; break; }
            }
        }
        ClientLogger.Info($"请求 {(curReady ? "取消准备" : "准备")}");
        GameFacade.SendReady(!curReady);
    }

    public void OnSwitchTeamClick() {
        TeamSide cur = TeamSide.TeamRed;
        if (currentRoom_ != null) {
            foreach (var p in currentRoom_.Players) {
                if (p.AccountId == Session.MyAccountId) { cur = p.Team; break; }
            }
        }
        TeamSide target = (cur == TeamSide.TeamRed) ? TeamSide.TeamBlue : TeamSide.TeamRed;
        ClientLogger.Info($"请求换队 → {target}");
        GameFacade.SendSwitchTeam(target);
    }

    // ---- 背包 ----
    public void OnBackpackClick() {
        // 准备后不能开背包
        bool myReady = false;
        if (currentRoom_ != null) {
            foreach (var p in currentRoom_.Players) {
                if (p.AccountId == Session.MyAccountId) { myReady = p.Ready; break; }
            }
        }
        if (myReady) {
            if (roomStatusText_) roomStatusText_.text = "已准备, 取消准备后才能换武器";
            return;
        }
        ShowBackpack();
    }

    private void ShowBackpack() {
        if (backpackPanel_ != null) { backpackPanel_.SetActive(true); UpdateBackpackHighlight(); return; }
        var canvas = FindFirstObjectByType<Canvas>();
        if (canvas == null) return;

        backpackPanel_ = new GameObject("BackpackPanel", typeof(RectTransform));
        backpackPanel_.transform.SetParent(canvas.transform, false);
        var prt = backpackPanel_.GetComponent<RectTransform>();
        prt.anchorMin = prt.anchorMax = new Vector2(0.5f, 0.5f);
        prt.sizeDelta = new Vector2(500, 300);
        prt.anchoredPosition = Vector2.zero;

        var img = backpackPanel_.AddComponent<CanvasRenderer>();
        var bg = backpackPanel_.AddComponent<Image>();
        bg.color = new Color(0.1f, 0.1f, 0.15f, 0.95f);

        MakeText(backpackPanel_.transform, "Title", new Vector2(0, 120), 26, "背包 - 选择武器");

        // 武器1: ice_cream (贴图 + 选中高亮)
        var w1 = CreateWeaponCard(backpackPanel_.transform, new Vector2(-110, 0), "ice_cream", "冰淇淋", 1, out weapon1Highlight_);
        var w2 = CreateWeaponCard(backpackPanel_.transform, new Vector2(110, 0), "projectile", "炮弹", 2, out weapon2Highlight_);

        MakeButton(backpackPanel_.transform, "Close", new Vector2(0, -120), "关闭", () => { backpackPanel_.SetActive(false); });
        UpdateBackpackHighlight();
    }

    private GameObject CreateWeaponCard(Transform parent, Vector2 pos, string spriteName, string label, int weaponId, out Image highlight) {
        var card = new GameObject("Weapon_" + weaponId, typeof(RectTransform));
        card.transform.SetParent(parent, false);
        var crt = card.GetComponent<RectTransform>();
        crt.sizeDelta = new Vector2(160, 200);
        crt.anchoredPosition = pos;

        // 选中高亮边框
        var hlGo = new GameObject("Highlight", typeof(RectTransform));
        hlGo.transform.SetParent(card.transform, false);
        hlGo.AddComponent<CanvasRenderer>();
        highlight = hlGo.AddComponent<Image>();
        highlight.color = new Color(1f, 1f, 0f, 0f);   // 默认透明
        var hrt = highlight.rectTransform;
        hrt.anchorMin = Vector2.zero; hrt.anchorMax = Vector2.one;
        hrt.offsetMin = new Vector2(-4, -4); hrt.offsetMax = new Vector2(4, 4);

        // 武器贴图
        var spriteObj = new GameObject("Sprite", typeof(RectTransform));
        spriteObj.transform.SetParent(card.transform, false);
        spriteObj.AddComponent<CanvasRenderer>();
        var wimg = spriteObj.AddComponent<Image>();
        wimg.raycastTarget = false;
        wimg.preserveAspect = true;
        Sprite sp = LoadSpriteAnyType("Projectiles/" + spriteName);
        if (sp != null) wimg.sprite = sp; else wimg.color = new Color(0.3f, 0.3f, 0.3f, 0.5f);
        wimg.rectTransform.sizeDelta = new Vector2(100, 100);
        wimg.rectTransform.anchoredPosition = new Vector2(0, 20);

        MakeText(card.transform, "Label", new Vector2(0, -70), 20, label);

        // 点击切换
        var btnGo = new GameObject("Btn", typeof(RectTransform));
        btnGo.transform.SetParent(card.transform, false);
        var btnImg = btnGo.AddComponent<Image>();
        btnImg.color = new Color(0, 0, 0, 0.01f);
        var brt = btnGo.GetComponent<RectTransform>();
        brt.anchorMin = Vector2.zero; brt.anchorMax = Vector2.one;
        brt.offsetMin = brt.offsetMax = Vector2.zero;
        var btn = btnGo.AddComponent<Button>();
        btn.onClick.AddListener(() => OnPickWeapon(weaponId));
        return card;
    }

    private void OnPickWeapon(int weaponId) {
        Session.MyWeaponId = weaponId;
        GameFacade.SendSwitchWeapon(weaponId);
        ClientLogger.Info($"切换武器 → {weaponId}");
        UpdateBackpackHighlight();
    }

    private void UpdateBackpackHighlight() {
        if (weapon1Highlight_ != null) weapon1Highlight_.color = new Color(1f, 1f, 0f, Session.MyWeaponId == 1 ? 0.5f : 0f);
        if (weapon2Highlight_ != null) weapon2Highlight_.color = new Color(1f, 1f, 0f, Session.MyWeaponId == 2 ? 0.5f : 0f);
    }

    // 通用贴图加载(先 Sprite 再 Texture2D fallback)
    internal static Sprite LoadSpriteAnyType(string path) {
        Sprite sp = Resources.Load<Sprite>(path);
        if (sp != null) return sp;
        Texture2D tex = Resources.Load<Texture2D>(path);
        if (tex != null) return Sprite.Create(tex, new Rect(0, 0, tex.width, tex.height), new Vector2(0.5f, 0.5f), 100f);
        return null;
    }

    public void OnToggleLogClick() {
        logExpanded_ = !logExpanded_;
        if (logPanel_) logPanel_.GetComponent<RectTransform>().sizeDelta =
            logExpanded_ ? new Vector2(600, 250) : new Vector2(200, 30);
    }

    public void OnRefreshClick() {
        ClientLogger.Info("请求刷新房间列表");
        GameFacade.SendRoomList();
    }

    public void OnLogoutClick() {
        ClientLogger.Info("退出登录");
        GameFacade.SendLogout();
        // 不等响应, 立即清理本地状态回登录界面
        Session.Clear();
        NetworkManager.Instance.Client.Close();
        SceneManager.LoadScene("LoginScene");
    }

    public void OnQuitClick() {
        ClientLogger.Info("退出游戏");
        Application.Quit();
    }

    public void OnModeCustomClick() {
        selectedMode_ = "custom";
        ClientLogger.Info("选择模式: 自定义对战");
        if (lobbyStatusText_) lobbyStatusText_.text = "模式: 自定义对战(红蓝各≥1人+全员准备开战)";
    }

    public void OnModeUnavailable(string name) {
        if (lobbyStatusText_) lobbyStatusText_.text = name + " 模式未开放, 敬请期待";
        ClientLogger.Info(name + " 模式未开放");
    }

    private uint ParseRoomIdFromInput() {
        if (roomNameInput_ == null) return 0;
        uint rid; return uint.TryParse(roomNameInput_.text, out rid) ? rid : 0;
    }

    private void SetLobbyStatus(string s) {
        if (lobbyStatusText_ != null) {
            lobbyStatusText_.text = s;
        }
    }

    // ============ 日志 ============

    private void UpdateLogPanel() {
        if (logText_ == null) return;
        if (ClientLogger.Version == lastLogVersion_) return;
        lastLogVersion_ = ClientLogger.Version;
        var entries = ClientLogger.GetRecent(logExpanded_ ? 20 : 1);
        var sb = new System.Text.StringBuilder();
        foreach (var e in entries) sb.AppendLine(ClientLogger.FormatEntry(e));
        logText_.text = sb.ToString();
    }

    // ============ UI 搭建 ============

    private void EnsureEventSystem() {
        if (FindFirstObjectByType<UnityEngine.EventSystems.EventSystem>() == null) {
            var esGo = new GameObject("EventSystem");
            esGo.AddComponent<UnityEngine.EventSystems.EventSystem>();
            esGo.AddComponent<UnityEngine.EventSystems.StandaloneInputModule>();
            DontDestroyOnLoad(esGo);
        }
    }

    private void BuildUI() {
        var canvas = FindFirstObjectByType<Canvas>();
        if (canvas == null) {
            var cgo = new GameObject("LobbyCanvas");
            canvas = cgo.AddComponent<Canvas>();
            canvas.renderMode = RenderMode.ScreenSpaceOverlay;
            cgo.AddComponent<GraphicRaycaster>();
        }
        // 统一设 CanvasScaler: 以 1280x720 为基准等比缩放, 匹配宽高各半。
        // 解决不同分辨率/窗口大小下 UI 元素超出屏幕的问题。
        var scaler = canvas.GetComponent<CanvasScaler>();
        if (scaler == null) scaler = canvas.gameObject.AddComponent<CanvasScaler>();
        scaler.uiScaleMode = CanvasScaler.ScaleMode.ScaleWithScreenSize;
        scaler.referenceResolution = new Vector2(1280, 720);
        scaler.screenMatchMode = CanvasScaler.ScreenMatchMode.MatchWidthOrHeight;
        scaler.matchWidthOrHeight = 0.5f;
        BuildLobbyPanel(canvas.transform);
        BuildRoomPanel(canvas.transform);
        BuildLogPanel(canvas.transform);
    }

    private void BuildLobbyPanel(Transform parent) {
        lobbyPanel_ = new GameObject("LobbyPanel", typeof(RectTransform)); // 核心：使用 RectTransform
        lobbyPanel_.transform.SetParent(parent, false);
        
        var rt = lobbyPanel_.GetComponent<RectTransform>();
        rt.anchorMin = Vector2.zero; rt.anchorMax = Vector2.one;
        rt.offsetMin = rt.offsetMax = Vector2.zero;
        
        MakeText(lobbyPanel_.transform, "Title", new Vector2(-100, 310), 28, $"大厅 — {Session.MyName}");
        MakeButton(lobbyPanel_.transform, "Logout", new Vector2(470, 310), "退出登录", OnLogoutClick).GetComponent<RectTransform>().sizeDelta = new Vector2(100, 32);
        MakeButton(lobbyPanel_.transform, "Quit", new Vector2(580, 310), "退出游戏", OnQuitClick).GetComponent<RectTransform>().sizeDelta = new Vector2(100, 32);

        MakeText(lobbyPanel_.transform, "ModeLabel", new Vector2(-280, 260), 16, "模式:");
        // 三个模式按钮(自定义可选 / 匹配+副本未开放)
        modeCustomBtn_ = MakeButton(lobbyPanel_.transform, "ModeCustom", new Vector2(-120, 260), "[自定义对战]", OnModeCustomClick);
        modeCustomBtn_.GetComponent<RectTransform>().sizeDelta = new Vector2(130, 30);
        var mb1 = MakeButton(lobbyPanel_.transform, "ModeMatch", new Vector2(40, 260), "匹配对战", () => OnModeUnavailable("匹配对战"));
        mb1.GetComponent<RectTransform>().sizeDelta = new Vector2(110, 30);
        var mb2 = MakeButton(lobbyPanel_.transform, "ModePve", new Vector2(180, 260), "副本模式", () => OnModeUnavailable("副本模式"));
        mb2.GetComponent<RectTransform>().sizeDelta = new Vector2(110, 30);

        MakeText(lobbyPanel_.transform, "Hint", new Vector2(0, 220), 15, "点击房间直接加入, 或输入房名建房");

        roomListText_ = MakeText(lobbyPanel_.transform, "RoomListHeader", new Vector2(0, 180), 18, "");
        roomListText_.rectTransform.sizeDelta = new Vector2(700, 30);

        MakeText(lobbyPanel_.transform, "RL", new Vector2(-280, -220), 18, "房名:");
        roomNameInput_ = LoginUI.MakeInputPublic(lobbyPanel_.transform, "RoomNameInput", new Vector2(-80, -220), "输入房名建房");

        MakeButton(lobbyPanel_.transform, "Create", new Vector2(200, -220), "建房", OnCreateClick);
        MakeButton(lobbyPanel_.transform, "Refresh", new Vector2(60, -280), "刷新列表", OnRefreshClick);

        lobbyStatusText_ = MakeText(lobbyPanel_.transform, "Status", new Vector2(0, -310), 16, "房间列表实时推送");
    }

    private void BuildRoomPanel(Transform parent) {
        roomPanel_ = new GameObject("RoomPanel", typeof(RectTransform)); // 核心：使用 RectTransform
        roomPanel_.transform.SetParent(parent, false);
        
        var rt = roomPanel_.GetComponent<RectTransform>();
        rt.anchorMin = Vector2.zero; rt.anchorMax = Vector2.one;
        rt.offsetMin = rt.offsetMax = Vector2.zero;
        
        roomTitleText_ = MakeText(roomPanel_.transform, "RoomTitle", new Vector2(0, 310), 26, "房间");

        redTeamText_ = MakeText(roomPanel_.transform, "RedTeam", new Vector2(-250, 255), 22, "红队");
        redTeamText_.rectTransform.sizeDelta = new Vector2(300, 36);
        redTeamText_.alignment = TextAnchor.MiddleCenter;

        blueTeamText_ = MakeText(roomPanel_.transform, "BlueTeam", new Vector2(250, 255), 22, "蓝队");
        blueTeamText_.rectTransform.sizeDelta = new Vector2(300, 36);
        blueTeamText_.alignment = TextAnchor.MiddleCenter;

        var readyBtn = MakeButton(roomPanel_.transform, "Ready", new Vector2(-200, -290), "准备", OnReadyClick);
        readyBtnLabel_ = readyBtn.transform.Find("Label").GetComponent<Text>();
        MakeButton(roomPanel_.transform, "SwitchTeam", new Vector2(-40, -290), "换队伍", OnSwitchTeamClick);
        MakeButton(roomPanel_.transform, "Backpack", new Vector2(150, -290), "背包", OnBackpackClick);
        MakeButton(roomPanel_.transform, "Leave", new Vector2(280, -290), "离开", OnLeaveClick);

        roomStatusText_ = MakeText(roomPanel_.transform, "RoomStatus", new Vector2(0, -330), 16, "");
    }

    private void BuildLogPanel(Transform parent) {
        logPanel_ = new GameObject("LogPanel", typeof(RectTransform)); // 核心：使用 RectTransform
        logPanel_.transform.SetParent(parent, false);
        logPanel_.AddComponent<CanvasRenderer>();
        var img = logPanel_.AddComponent<Image>();
        img.color = new Color(0, 0, 0, 0.7f);
        var rt = logPanel_.GetComponent<RectTransform>();
        rt.sizeDelta = new Vector2(200, 30);
        rt.anchorMin = rt.anchorMax = new Vector2(1f, 0f);
        rt.pivot = new Vector2(1f, 0f);
        rt.anchoredPosition = new Vector2(-10, 10);
        
        logText_ = MakeText(logPanel_.transform, "LogText", new Vector2(0, -15), 13, "");
        logText_.rectTransform.anchorMin = new Vector2(0, 0);
        logText_.rectTransform.anchorMax = new Vector2(1, 1);
        logText_.rectTransform.offsetMin = new Vector2(5, 5);
        logText_.rectTransform.offsetMax = new Vector2(-5, -20);
        logText_.rectTransform.pivot = new Vector2(0.5f, 0);
        logText_.alignment = TextAnchor.LowerLeft;
        logText_.supportRichText = true;
        
        var toggleBtn = MakeButton(logPanel_.transform, "Toggle", new Vector2(0, 0), "日志", OnToggleLogClick);
        var trt = toggleBtn.GetComponent<RectTransform>();
        trt.anchorMin = trt.anchorMax = new Vector2(0.5f, 1f);
        trt.pivot = new Vector2(0.5f, 1f);
        trt.anchoredPosition = Vector2.zero;
        trt.sizeDelta = new Vector2(80, 18);
    }

    // ---- UI 工具 ----
    internal static Text MakeText(Transform parent, string name, Vector2 pos, int size, string content) {
        var go = new GameObject(name, typeof(RectTransform));
        go.transform.SetParent(parent, false);
        go.AddComponent<CanvasRenderer>();
        var t = go.AddComponent<Text>();
        t.font = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
        t.fontSize = size; t.alignment = TextAnchor.MiddleCenter; t.color = Color.white;
        t.raycastTarget = false; t.text = content; t.supportRichText = true;
        var rt = t.rectTransform; rt.sizeDelta = new Vector2(300, 40);
        rt.anchorMin = rt.anchorMax = new Vector2(0.5f, 0.5f); rt.anchoredPosition = pos;
        return t;
    }
    internal static Button MakeButton(Transform parent, string name, Vector2 pos, string label, UnityEngine.Events.UnityAction onClick) {
        var go = new GameObject(name, typeof(RectTransform));
        go.transform.SetParent(parent, false);
        go.AddComponent<CanvasRenderer>();
        var img = go.AddComponent<Image>(); img.color = new Color(0.25f, 0.5f, 0.9f, 1f);
        var rt = go.GetComponent<RectTransform>(); rt.sizeDelta = new Vector2(120, 38);
        rt.anchorMin = rt.anchorMax = new Vector2(0.5f, 0.5f); rt.anchoredPosition = pos;
        var btn = go.AddComponent<Button>();
        var c = btn.colors; c.highlightedColor = new Color(0.4f, 0.7f, 1f); btn.colors = c;
        var lblGo = new GameObject("Label", typeof(RectTransform)); lblGo.transform.SetParent(go.transform, false);
        lblGo.AddComponent<CanvasRenderer>();
        var lbl = lblGo.AddComponent<Text>();
        lbl.font = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf"); lbl.fontSize = 20;
        lbl.alignment = TextAnchor.MiddleCenter; lbl.color = Color.white; lbl.text = label; lbl.raycastTarget = false;
        lbl.rectTransform.anchorMin = Vector2.zero; lbl.rectTransform.anchorMax = Vector2.one;
        lbl.rectTransform.offsetMin = lbl.rectTransform.offsetMax = Vector2.zero;
        btn.onClick.AddListener(onClick);
        return btn;
    }
}
}