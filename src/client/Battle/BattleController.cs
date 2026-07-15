// BattleController.cs — 战斗场景主控
//
// 职责:
//   - 收到 RoomReadyNotify: 初始化 BattleField(地形/角色)
//   - 收到 TurnStartNotify: 切回合, 解锁/锁定操作
//   - 收到 ShootResultNotify: 回放弹道 + 更新 HP + 爆炸
//   - 收到 MoveNotify: 更新对方位置
//   - 收到 GameOverNotify/OpponentLeftNotify: 显示结果
//   - 处理本机玩家操作: A/D 移动, W/S 角度, 空格蓄力射击, P 跳过, F 纸飞机
//   - HUD: 回合提示/风向/角度力度蓄力条
//
// 挂在 BattleScene 里一个空 GameObject "BattleController" 上。
// Inspector 拖入 BattleField 引用。
using UnityEngine;
using UnityEngine.UI;
using System.Collections.Generic;
using Ddt;
using Ddt.Net.Game;

namespace Ddt.Net.Battle {
public class BattleController : MonoBehaviour {
    public BattleField field;          // Inspector 拖入战场根

    // HUD(UI Canvas 上的文本, Inspector 拖入; 可空则用代码建)
    public Text turnText;
    public Text windText;

    // 底部控制面板元素(代码自建; Inspector 也可拖)
    private Text angleText_;        // 角度数值
    private Text forceText_;        // 力度数值
    private Text hpText_;           // 自己 HP 数值
    private Image forceBarFillImg_; // 力度条前景填充(实时 fillAmount)
    private Minimap minimap_;       // 右上角小地图
    private Ddt.Net.UI.UIChat chatPanel_;   // 左下角聊天面板
    private Image flyIconImg_;      // 纸飞机道具图标
    private Image flyIconBg_;       // 纸飞机图标背景(高亮/变灰)
    private Text flyCdText_;        // 纸飞机冷却文字

    // 顶部玩家信息卡(动态数量, HP=0 变灰)
    private GameObject topCardsRoot_;
    private readonly List<GameObject> topCards_ = new List<GameObject>();

    // 倒计时 + PASS 按钮 + 回合提示
    private Text countdownText_;    // 本回合剩余秒数
    private Text turnPromptText_;   // "轮到你出手啦!" / "对方回合"(停留 1 秒淡出)
    private float timeLeft_ = 0f;   // 本回合剩余秒数
    private const float TURN_TIMEOUT = 10f;   // 与服务端 turn_timeout(battle.yml) 一致
    private bool promptShowing_ = false;

    private float curWind_ = 0f;             // 最近一次收到的风力(用于显示)

    // 物理(开局由 RoomReadyNotify 填)
    private PhysicsSim.PhysicsParams physics_;
    private bool inited_ = false;

    // 对象是否已销毁: 退出战斗回大厅后 BattleController 的 GameObject 被销毁, 但订阅的
    // 委托仍被 MessageDispatcher 持有 → 僵尸回调。此标志让 OnXxx 提前返回, 防止
    // 访问已销毁的 field 等 Unity 对象触发 NRE(与 LobbyUI 同样的僵尸订阅问题)。
    private bool destroyed_ = false;

    // 回合状态
    private bool myTurn_ = false;
    private bool shotLocked_ = false;   // 本回合已射, 锁操作
    private float moveUsed_ = 0f;
    private float maxMovePerTurn_ = 200f;
    // 缓冲的 TurnStartNotify: 射击结算期间(ShootResult 弹道回放)到达的回合切换通知。
    // 延迟到弹丸落地+爆炸动画播完 + TURN_SWITCH_DELAY 后才应用(切回合 + 倒计时)。
    // resolvingShoot_=true 期间缓冲; 首回合/Pass/超时(无弹道)直接应用。
    private byte[] pendingTurnStartBytes_ = null;
    private bool resolvingShoot_ = false;   // 正在回放弹道(OnShootResult 置 true)
    // 弹丸落地后切换回合的额外延迟(秒): 给玩家看清爆炸/命中结果的缓冲。
    private const float TURN_SWITCH_DELAY = 1.5f;

    // 移动 RPC 节流: 累积移动距离超过阈值才发一次, 避免每帧发(60次/秒→~10次/秒)。
    // 松开移动键时补发最终位置, 保证服务端拿到精确坐标。
    private float pendingMoveDist_ = 0f;           // 自上次发送以来累积的未发距离
    private const float MOVE_SEND_THRESHOLD = 15f;  // 累积超过此距离才发 RPC(像素)

    // 操作状态
    private int baseAngle_ = 45;
    private float power_ = 0f;
    private bool charging_ = false;
    private bool useFly_ = false;
    private int flyCooldown_ = 0;   // 纸飞机冷却(剩余自身回合数); >0 时不可用

    // 本机账号
    private ulong myAccountId_ = 0;
    private ulong turnAccountId_ = 0;   // 当前回合玩家(摄像机跟随用)

    void Start() {
        var disp = NetworkManager.Instance.Dispatcher;
        disp.Subscribe(MsgId.ROOM_READY_NOTIFY, OnRoomReady);
        disp.Subscribe(MsgId.TURN_START_NOTIFY, OnTurnStart);
        disp.Subscribe(MsgId.SHOOT_RESULT_NOTIFY, OnShootResult);
        disp.Subscribe(MsgId.MOVE_NOTIFY, OnMove);
        disp.Subscribe(MsgId.GAME_OVER_NOTIFY, OnGameOver);
        disp.Subscribe(MsgId.OPPONENT_LEFT_NOTIFY, OnOpponentLeft);
        disp.Subscribe(MsgId.ERROR, OnError);

        // 本机账号从 NetworkManager 取(登录后存的); 没存则用 Bootstrap/LoginUI 设的
        myAccountId_ = Session.MyAccountId;
        BuildHudIfMissing();
        UpdateHud();

        // 场景切换前缓存的 ROOM_READY_NOTIFY: 重放(否则 BattleController 错过开局消息)
        if (Session.PendingRoomReady != null) {
            var cached = Session.PendingRoomReady;
            Session.PendingRoomReady = null;
            OnRoomReady(cached);
        }
        // 重放缓存的 TURN_START_NOTIFY(服务端 startGame 后立刻发的首回合)
        if (Session.PendingTurnStart != null) {
            var cached = Session.PendingTurnStart;
            Session.PendingTurnStart = null;
            // 延迟一帧执行, 确保 OnRoomReady 的 Init 已完成
            StartCoroutine(DelayedTurnStart(cached));
        }
        NetworkManager.Instance.EndTransition();  // 修复：战斗场景初始化完毕，结束转场并派发加载期间缓存的所有网络消息
    }
    

    private System.Collections.IEnumerator DelayedTurnStart(byte[] bytes) {
        yield return null;   // 等一帧
        OnTurnStart(bytes);
    }

    void OnDestroy() {
        destroyed_ = true;   // 标记已销毁: 防止回大厅后僵尸回调访问已销毁的 field
        // 反注册订阅: MessageDispatcher 是 DontDestroyOnLoad 单例, 不退订会导致
        // 回大厅后本控制器已销毁但 dispatcher 仍持有僵尸回调(destroyed_ 守卫是兜底,
        // 显式 Unsubscribe 才是根治)。与 UIChat.OnDestroy 的退订模式对齐。
        var disp = NetworkManager.Instance != null ? NetworkManager.Instance.Dispatcher : null;
        if (disp != null) {
            disp.Unsubscribe(MsgId.ROOM_READY_NOTIFY);
            disp.Unsubscribe(MsgId.TURN_START_NOTIFY);
            disp.Unsubscribe(MsgId.SHOOT_RESULT_NOTIFY);
            disp.Unsubscribe(MsgId.MOVE_NOTIFY);
            disp.Unsubscribe(MsgId.GAME_OVER_NOTIFY);
            disp.Unsubscribe(MsgId.OPPONENT_LEFT_NOTIFY);
            disp.Unsubscribe(MsgId.ERROR);
        }
    }

    void Update() {
        // 摄像机跟随当前回合玩家: 仅当 降落完成 且 开场巡游(intro)结束 且 战场不忙碌
        // (炮弹飞行 + 爆炸动画 + 纸飞机传送 全部播完才回到回合玩家)
        // 且 用户未在小地图手动拖拽相机(手动模式下让位, 3 秒后自动回退)
        if (inited_ && field != null && field.IsLandingDone && field.IsIntroDone && turnAccountId_ != 0) {
            if (!field.IsBusy && !field.IsCameraManual) {
                var turnPlayer = field.GetPlayer(turnAccountId_);
                if (turnPlayer != null) field.FocusCamera(turnPlayer.transform.position);
            }
        }
        // 2. 核心修复：当战斗初始化完成后，每帧持续更新 HUD 文本与蓄力条状态
        if (inited_) {
            UpdateCountdown();   // 倒计时递减 + 显示
            UpdateHud();

            // 3. 操作解锁条件: 轮到自己 + 未射击锁定 + 开局降落完成 且 开场巡视(intro)结束。
            //    与倒计时同步: 都等 intro 完成才开始, 保证"落地+巡视后再开始回合"。
            //    否则 intro 期间能操作但倒计时没走, 行为不一致。
            if (myTurn_ && !shotLocked_ && field != null && field.IsLandingDone && field.IsIntroDone) {
                HandleInput();
            }
        }
    }

    // 本回合倒计时递减(本地显示)。
    // 规则:
    //   1) 自己回合 + 未出手 + 已落地开场结束(intro 完) 才开始递减
    //      → 保证"开局人物落地、摄像机巡视完后才开始回合计时"
    //   2) 蓄力中暂停递减 → 防止蓄力途中倒计时归零切回合
    //   3) 倒计时初值 = TURN_TIMEOUT(10s), 与服务端 turn_timeout 一致
    private void UpdateCountdown() {
        bool landedAndReady = field != null && field.IsLandingDone && field.IsIntroDone;
        bool shouldTick = myTurn_ && !shotLocked_ && landedAndReady && !charging_ && timeLeft_ > 0f;
        if (shouldTick) {
            timeLeft_ -= Time.deltaTime;
            if (timeLeft_ < 0f) timeLeft_ = 0f;
        }
        if (countdownText_) {
            if (!myTurn_) {
                countdownText_.text = "";
            } else if (!landedAndReady) {
                // 开局降落/巡视期间: 不显示倒计时(还没开始计时), 避免误导
                countdownText_.text = "准备中...";
                countdownText_.color = new Color(1f, 0.85f, 0.3f);
            } else if (shotLocked_) {
                // 已出手(炮弹飞行/爆炸/纸飞机动画期间): 隐藏倒计时,
                // 等服务端 TurnStartNotify 切到下一回合再重置显示。
                countdownText_.text = "";
            } else if (charging_) {
                // 蓄力中: 暂停计时, 显示"蓄力中"提示(不冻结但提示玩家计时已暂停)
                countdownText_.text = "蓄力中...";
                countdownText_.color = new Color(1f, 0.85f, 0.3f);
            } else {
                int sec = Mathf.CeilToInt(timeLeft_);
                countdownText_.text = $"倒计时 {sec}s";
                countdownText_.color = timeLeft_ <= 3f ? new Color(1f, 0.35f, 0.3f) : Color.white;
            }
        }
    }

    // ============ 通知处理(主线程) ============

    private void OnRoomReady(byte[] bytes) {
        var m = RoomReadyNotify.Parser.ParseFrom(bytes);
        // 取 2D 体素位图(替代旧 1D heightMap)
        byte[] terrainBitmap = m.TerrainBitmap.ToByteArray();
        // 物理
        var pp = m.PhysicsParams;
        physics_ = new PhysicsSim.PhysicsParams {
            airFactor = pp.AirFactor, windFactor = pp.WindFactor,
            gravityFactor = pp.GravityFactor, forceFactor = pp.ForceFactor
        };
        maxMovePerTurn_ = m.MaxMovePerTurn;
        curWind_ = m.Wind;
        if (field) field.Init(terrainBitmap, m.TerrainW, m.TerrainH, myAccountId_, m.Players, m.MapName);
        inited_ = true;
        // 确保 HUD(含小地图)已建好: field 在 Start 时可能还没就绪
        BuildHudIfMissing();
        Debug.Log($"[Battle] room ready: {m.Players.Count} players, wind={m.Wind} map={m.MapName} terrain={m.TerrainW}x{m.TerrainH}");
    }

    private void OnTurnStart(byte[] bytes) {
        if (destroyed_) return;   // 退出战斗后本控制器已销毁, 忽略僵尸回调
        // 客户端掌控回合切换时序: 弹道回放期间(field 忙 或 resolvingShoot_)到达的
        // TurnStartNotify 缓冲, 延迟到弹丸落地+爆炸动画播完 + TURN_SWITCH_DELAY 后才应用。
        // resolvingShoot_ 覆盖 ShootResult 已到但弹丸刚启动(IsBusy 可能还没置 true)的窗口;
        // IsBusy 覆盖 TurnStart 在弹丸飞行中途到达的情况。
        // 无弹道动画时(Pass/超时/首回合)直接应用。
        if ((field != null && field.IsBusy) || resolvingShoot_) {
            pendingTurnStartBytes_ = bytes;
            return;
        }
        ApplyTurnStart(bytes);
    }

    // 真正应用 TurnStartNotify: 切回合 + 重置倒计时 + 刷新玩家位置 + 提示。
    private void ApplyTurnStart(byte[] bytes) {
        var m = TurnStartNotify.Parser.ParseFrom(bytes);
        turnAccountId_ = m.TurnAccountId;
        myTurn_ = (m.TurnAccountId == myAccountId_);
        shotLocked_ = false;
        moveUsed_ = 0f;
        pendingMoveDist_ = 0f;   // 清空移动节流累积, 避免跨回合残留
        power_ = 0f;
        curWind_ = m.Wind;
        // 自身回合开始: 纸飞机冷却 -1(用于上一回合发射后的冷却递减)
        if (myTurn_ && flyCooldown_ > 0) flyCooldown_--;
        timeLeft_ = TURN_TIMEOUT;   // 重置倒计时
        // 告诉战场当前回合玩家(intro 收尾 + 回合跟随 + Manual 回退 都用它)
        if (field) field.SetTurnPlayer(m.TurnAccountId);
        // 关键: 用通知刷新玩家位置必须等战场不忙碌(炮弹飞行/爆炸/纸飞机动画播完)!
        // 否则 TurnStartNotify 紧跟 ShootResultNotify 到达, 会立即把纸飞机射击者设到落点,
        // 导致"刚发射人物就瞬移"(飞行动画还没播完)。
        if (field) {
            byte[] saved = bytes;   // 捕获, 协程里再解析
            StartCoroutine(DelayedTurnStartPlayerUpdate(saved));
        }
        // 顶部玩家卡刷新(HP 可能变化)
        RefreshTopCards();
        // 回合提示 + 快速平移到当前回合玩家(intro 结束后)
        StartCoroutine(ShowTurnPromptAndPan());
        Debug.Log($"[Battle] turn {m.TurnNumber}: {(myTurn_ ? "MY" : "OPP")} turn, wind={m.Wind}");
    }

    // 弹丸落地+爆炸动画播完(IsBusy 清零)后, 再等 TURN_SWITCH_DELAY 才应用缓冲的
    // TurnStartNotify(切回合 + 重置倒计时)。让玩家看清弹道结果后再切回合。
    // 无论 TurnStart 是否已缓冲都启动: 解除 resolvingShoot_; 若有缓冲则应用。
    private System.Collections.IEnumerator DelayedApplyTurnStart() {
        // 等战场不忙碌(炮弹飞行 + 爆炸 + KeepBusyFor 0.8s), 最多 6s 防卡死
        float waited = 0f;
        while (field != null && field.IsBusy && waited < 6f) {
            waited += Time.deltaTime;
            yield return null;
        }
        // 再额外等 TURN_SWITCH_DELAY(看清爆炸/命中结果)
        yield return new WaitForSeconds(TURN_SWITCH_DELAY);
        resolvingShoot_ = false;
        // 应用缓冲的回合切换(若有): TurnStart 在弹道期间到达被缓冲到这里
        if (pendingTurnStartBytes_ != null && !destroyed_) {
            byte[] saved = pendingTurnStartBytes_;
            pendingTurnStartBytes_ = null;
            ApplyTurnStart(saved);
        }
    }

    // 延迟到战场不忙碌(炮弹/爆炸/纸飞机动画播完)再刷新玩家位置,
    // 避免纸飞机射击者被 TurnStartNotify 提前设到落点(瞬移)。
    private System.Collections.IEnumerator DelayedTurnStartPlayerUpdate(byte[] bytes) {
        // 最多等 5 秒(防卡死), 期间战场忙碌就等
        float waited = 0f;
        while (field != null && field.IsBusy && waited < 5f) {
            waited += Time.deltaTime;
            yield return null;
        }
        var m = TurnStartNotify.Parser.ParseFrom(bytes);
        if (field) {
            foreach (var ps in m.Players) {
                field.SetPlayerState(ps.AccountId, ps.X, ps.Y, ps.Hp, ps.MaxHp, ps.Angle, ps.Direction);
            }
        }
        // 本机玩家角度同步到 UI
        if (myTurn_ && field && field.MyPlayer) baseAngle_ = field.MyPlayer.Angle;
        RefreshTopCards();
    }

    // 回合开始: 显示提示文字(1 秒后淡出) + 快速平移到当前回合玩家
    private System.Collections.IEnumerator ShowTurnPromptAndPan() {
        // 等 intro(开场巡游)结束, 再切回合镜头(最多等 10 秒, 防后台卡死)
        float waited = 0f;
        while (field != null && !field.IsIntroDone && waited < 10f) {
            waited += Time.deltaTime;
            yield return null;
        }

        if (turnPromptText_ != null) {
            turnPromptText_.text = myTurn_ ? "轮到你出手啦!" : "对方回合";
            turnPromptText_.color = myTurn_ ? new Color(0.4f, 1f, 0.5f, 1f) : new Color(1f, 0.7f, 0.3f, 1f);
            turnPromptText_.gameObject.SetActive(true);
            promptShowing_ = true;
        }
        // 快速平移到当前回合玩家
        if (field != null && turnAccountId_ != 0) {
            var p = field.GetPlayer(turnAccountId_);
            if (p != null) field.PanToCamera(p.transform.position, 1500f);
        }
        // 提示停留 1 秒后淡出
        yield return new WaitForSeconds(1f);
        if (turnPromptText_ != null) {
            // 淡出 0.4 秒
            float t = 0f;
            Color c = turnPromptText_.color;
            while (t < 0.4f) {
                t += Time.deltaTime;
                c.a = Mathf.Lerp(1f, 0f, t / 0.4f);
                turnPromptText_.color = c;
                yield return null;
            }
            turnPromptText_.gameObject.SetActive(false);
            c.a = 1f; turnPromptText_.color = c;   // 复位 alpha
        }
        promptShowing_ = false;
    }

    private void OnShootResult(byte[] bytes) {
        if (destroyed_) return;
        var m = ShootResultNotify.Parser.ParseFrom(bytes);
        Debug.Log($"[Battle] shootresult: dir={m.Direction} angle={m.Angle} start=({m.StartX},{m.StartY}) hit=({m.HitX},{m.HitY}) isFly={m.IsFly}");
        resolvingShoot_ = true;   // 标记弹道回放中: 缓冲紧随其后的 TurnStartNotify
        // 回放弹道(本地复算, 按武器/纸飞机选弹丸贴图)
        if (field && field.MyPlayer != null) {
            field.PlayTrajectory(m.StartX, m.StartY, m.Angle, m.Direction, m.Force, m.Wind,
                physics_, m.WeaponId, m.IsFly, () => {
                    if (field) {
                        foreach (var ps in m.UpdatedPlayers) {
                            field.SetPlayerState(ps.AccountId, ps.X, ps.Y, ps.Hp, ps.MaxHp, ps.Angle, ps.Direction);
                        }
                        // 普通弹: 落点爆炸(挖坑 + 动画); 出界(飞出左右)不爆炸
                        bool offscreen = (m.HitX < 0 || m.HitX > Ddt.Net.Battle.BattleField.WORLD_W);
                        if (!m.IsFly && !offscreen) field.Explode(m.HitX, m.HitY, 50f);   // blast_radius=50, 与服务端一致
                        // 命中玩家: 弹出伤害数字(在命中点)
                        if (m.HitPlayer && m.Damage > 0) {
                            bool crit = m.DamageType == ShootResultNotify.Types.DamageType.Critical;
                            bool block = m.DamageType == ShootResultNotify.Types.DamageType.Block;
                            field.ShowDamageText(m.HitX, m.HitY, m.Damage, crit, block);
                        }
                        // 落地后给延迟再移动相机(看清爆炸/传送结果)
                        field.KeepBusyFor(0.8f);
                    }
                    RefreshTopCards();   // HP 变化后刷新顶部卡片
                    Debug.Log($"[Battle] shoot result: hit={m.HitPlayer} dmg={m.Damage} type={m.DamageType} weapon={m.WeaponId} isFly={m.IsFly}");
                    // 弹丸落地+爆炸动画播完(busy 清零)后, 再等 TURN_SWITCH_DELAY 才切回合。
                    // 无论 TurnStartNotify 是否已缓冲, 都启动(需解除 resolvingShoot_)。
                    StartCoroutine(DelayedApplyTurnStart());
                });
        }
    }

    private void OnMove(byte[] bytes) {
        if (destroyed_) return;
        var m = MoveNotify.Parser.ParseFrom(bytes);
        if (field) {
            // 多人: 按 accountId 查找对应玩家更新位置(不依赖单一 OppPlayer)
            var target = field.GetPlayer(m.AccountId);
            if (target != null) {
                // 由位移方向推断面朝向(向右 dir=1, 向左 dir=-1), 与服务端 onMove 一致
                int dir = target.Direction;
                if (m.NewX > target.X + 0.001f) dir = 1;
                else if (m.NewX < target.X - 0.001f) dir = -1;
                field.SetPlayerState(m.AccountId, m.NewX, target.Y, target.Hp, target.MaxHp, target.Angle, dir);
            }
        }
    }

    private void OnGameOver(byte[] bytes) {
        if (destroyed_) return;
        var m = GameOverNotify.Parser.ParseFrom(bytes);
        myTurn_ = false;
        Debug.Log($"[Battle] GAME OVER! winner={m.WinnerAccountId} team={m.WinningTeam} reason={m.Reason}");
        bool iWon = m.WinnerAccountId == myAccountId_;
        if (!iWon && m.WinningTeam != 0) {
            // 队伍模式: 看自己队伍是否=获胜队
            var me = field != null ? field.MyPlayer : null;
            if (me != null) iWon = ((int)me.team == (int)m.WinningTeam);
        }
        if (turnText) turnText.text = iWon ? "胜利!" : "失败";
        ShowResultPanel(iWon);
    }

    // 战斗结算画面: 胜利/失败 + 返回房间按钮(回大厅房间场景)
    private void ShowResultPanel(bool won) {
        var canvas = FindFirstObjectByType<Canvas>();
        if (canvas == null) return;
        // 半透明遮罩
        var mask = new GameObject("ResultMask", typeof(RectTransform));
        mask.transform.SetParent(canvas.transform, false);
        var mrt = mask.GetComponent<RectTransform>();
        mrt.anchorMin = Vector2.zero; mrt.anchorMax = Vector2.one;
        mrt.offsetMin = mrt.offsetMax = Vector2.zero;
        var mimg = mask.AddComponent<Image>();
        mimg.color = new Color(0, 0, 0, 0.6f);

        // 结算面板(居中)
        var panel = new GameObject("ResultPanel", typeof(RectTransform));
        panel.transform.SetParent(mask.transform, false);
        var prt = panel.GetComponent<RectTransform>();
        prt.anchorMin = prt.anchorMax = new Vector2(0.5f, 0.5f);
        prt.pivot = new Vector2(0.5f, 0.5f);
        prt.sizeDelta = new Vector2(360, 220);
        var pimg = panel.AddComponent<Image>();
        pimg.color = new Color(0.12f, 0.14f, 0.2f, 0.95f);

        // 标题(胜利/失败)
        var title = MakeText(panel.transform, "Title", new Vector2(0, 60), 44);
        title.text = won ? "胜  利" : "失  败";
        title.color = won ? new Color(0.4f, 1f, 0.5f) : new Color(1f, 0.35f, 0.35f);
        title.rectTransform.sizeDelta = new Vector2(340, 60);

        // 副标题
        var sub = MakeText(panel.transform, "Sub", new Vector2(0, 0), 20);
        sub.text = won ? "恭喜获胜!" : "再接再厉!";
        sub.color = new Color(1, 1, 1, 0.8f);
        sub.rectTransform.sizeDelta = new Vector2(340, 30);

        // 返回房间按钮: 回到房间场景(而非大厅), 保持 seat 方便再次开局。
        // 发一次 Ready(false) 触发服务端重置房间 started 状态(lobby Ready RPC 里:
        // 若 room.started 则重置 started + 清所有人 ready)并广播 ROOM_UPDATE,
        // 让房间面板刷新出队友/座位。不发 LeaveRoom(避免清 seat)。
        var btn = MakeButton(panel.transform, "BackBtn", new Vector2(0, -70), "返回房间", () => {
            Session.IsInBattle = false;
            GameFacade.SendReady(false);   // 触发服务端重置 started + 广播房间更新
            UnityEngine.SceneManagement.SceneManager.LoadScene("LobbyScene");
        });
        btn.GetComponent<RectTransform>().sizeDelta = new Vector2(200, 44);
    }

    private void OnOpponentLeft(byte[] bytes) {
        if (destroyed_) return;
        var m = OpponentLeftNotify.Parser.ParseFrom(bytes);
        Debug.Log($"[Battle] player {m.AccountId} left");
        if (turnText) turnText.text = $"玩家 {m.AccountId % 10000} 离开";
    }

    private void OnError(byte[] bytes) {
        var m = ErrorNotify.Parser.ParseFrom(bytes);
        Debug.LogWarning($"[Battle] server error: code={m.Code} msg={m.Msg}");
    }

    // ============ 本机操作(仅自己回合) ============

    private void HandleInput() {
        if (field == null || field.MyPlayer == null) return;

        // A/D 移动 (使用 Time.deltaTime 确保帧率平滑一致，并强制限制在世界左右边界内)
        // RPC 节流: 累积移动超过阈值才发一次(60次/秒→~10次/秒), 松开移动键时补发最终位置。
        float moveSpeed = 150f; // 移动速度：每秒 150 像素/单位
        bool aDown = Input.GetKey(KeyCode.A);
        bool dDown = Input.GetKey(KeyCode.D);
        if (moveUsed_ < maxMovePerTurn_) {
            float dx = 0;
            if (aDown) dx -= moveSpeed * Time.deltaTime;
            if (dDown) dx += moveSpeed * Time.deltaTime;
            if (dx != 0) {
                var me = field.MyPlayer;
                float nextX = me.X + dx;

                // 限制在地图左右物理边界内 [30f, WORLD_W - 30f]
                float clampedX = Mathf.Clamp(nextX, 30f, BattleField.WORLD_W - 30f);
                float actual = clampedX - me.X;

                // 重力限制: 目标位置地面比当前位置高超过 80(陡坡)则爬不上去(与服务端一致)
                var terrain = field.Terrain;
                if (terrain != null) {
                    int curIx = Mathf.RoundToInt(me.X);
                    int newIx = Mathf.RoundToInt(nextX);
                    if (curIx >= 0 && curIx < terrain.Width && newIx >= 0 && newIx < terrain.Width) {
                        float heightDiff = terrain.ColumnHeight(newIx) - terrain.ColumnHeight(curIx);
                        if (heightDiff > 80f) actual = 0f;   // 太陡, 不移动
                    }
                }

                if (Mathf.Abs(actual) > 0.001f) {
                    float remain = maxMovePerTurn_ - moveUsed_;
                    float moveAmount = Mathf.Min(Mathf.Abs(actual), remain);
                    if (actual < 0) actual = -moveAmount;
                    else actual = moveAmount;

                    moveUsed_ += Mathf.Abs(actual);
                    pendingMoveDist_ += Mathf.Abs(actual);
                    int dir = actual > 0 ? 1 : -1;

                    // 本地立即更新(乐观, 每帧): 视觉平滑
                    field.SetPlayerState(me.accountId, me.X + actual, me.Y, me.Hp, me.MaxHp, baseAngle_, dir);

                    // RPC 节流: 累积超过阈值才发, 减少服务端 RPC 频率(及 fiber 创建量)
                    if (pendingMoveDist_ >= MOVE_SEND_THRESHOLD) {
                        GameFacade.SendMove(pendingMoveDist_ * dir);   // 发累积距离(带方向)
                        pendingMoveDist_ = 0f;
                    }
                }
            }
        }
        // 松开移动键时补发剩余累积, 保证服务端拿到精确最终坐标
        if (!aDown && !dDown && pendingMoveDist_ > 0.001f) {
            GameFacade.SendMove(pendingMoveDist_ * (field.MyPlayer != null ? field.MyPlayer.Direction : 1));
            pendingMoveDist_ = 0f;
        }


        // W/S 角度(20-65): W 抬高(增大)/S 降低(减小), 朝左朝右一致。
        if (Input.GetKeyDown(KeyCode.W) || Input.GetKey(KeyCode.W)) baseAngle_ = Mathf.Min(65, baseAngle_ + 1);
        if (Input.GetKeyDown(KeyCode.S) || Input.GetKey(KeyCode.S)) baseAngle_ = Mathf.Max(20, baseAngle_ - 1);
        // 更新炮管角度
        if (field.MyPlayer != null) {
            field.SetPlayerState(field.MyPlayer.accountId, field.MyPlayer.X, field.MyPlayer.Y,
                                 field.MyPlayer.Hp, field.MyPlayer.MaxHp, baseAngle_, field.MyPlayer.Direction);
        }

        // 空格蓄力(按住蓄力, 松开发射; 力度为 0 时不发射, 避免误触跳过回合)
        if (Input.GetKey(KeyCode.Space) && !charging_) {
            charging_ = true;
            // 蓄力开始: 通知服务端重置回合计时(防止蓄力中途倒计时归零切回合)
            GameFacade.SendAimBegin();
        }
        if (charging_) {
            power_ += 80f * Time.deltaTime;
            if (power_ > 100f) power_ = 100f;
        }
        if (Input.GetKeyUp(KeyCode.Space) && charging_) {
            charging_ = false;
            if (power_ > 1f) {
                DoShoot();   // 有蓄力才发射
            } else {
                power_ = 0f;   // 力度太小, 取消(不浪费回合)
            }
        }

        // P 跳过
        if (Input.GetKeyDown(KeyCode.P)) {
            GameFacade.SendPass();
            shotLocked_ = true;
        }

        // F 切换纸飞机(冷却中禁用)
        if (Input.GetKeyDown(KeyCode.F) && flyCooldown_ <= 0) useFly_ = !useFly_;
    }

    private void DoShoot() {
        float sendPower = power_;   // 先存, 发完再清零
        bool usedFly = useFly_;
        GameFacade.SendShoot(baseAngle_, sendPower, usedFly, Session.MyWeaponId);
        shotLocked_ = true;   // 射后锁定, 等服务端结果
        power_ = 0f;
        // 用过纸飞机: 进入冷却(2 个自身回合), 并切回普通炮弹
        if (usedFly) {
            flyCooldown_ = 2;
            useFly_ = false;
        }
        Debug.Log($"[Battle] shoot: angle={baseAngle_} force={(int)sendPower} fly={usedFly} flyCd={flyCooldown_}");
    }

    // ============ HUD ============

    private void BuildHudIfMissing() {
        // 小地图拖拽需要 EventSystem(登录场景已建 DontDestroyOnLoad; 直接进战斗时兜底)
        if (FindFirstObjectByType<UnityEngine.EventSystems.EventSystem>() == null) {
            var esGo = new GameObject("EventSystem");
            esGo.AddComponent<UnityEngine.EventSystems.EventSystem>();
            esGo.AddComponent<UnityEngine.EventSystems.StandaloneInputModule>();
        }
        var canvas = FindFirstObjectByType<Canvas>();
        if (canvas == null) {
            var cgo = new GameObject("BattleCanvas");
            canvas = cgo.AddComponent<Canvas>();
            canvas.renderMode = RenderMode.ScreenSpaceOverlay;
            var scaler = cgo.AddComponent<CanvasScaler>();
            scaler.uiScaleMode = CanvasScaler.ScaleMode.ScaleWithScreenSize;
            scaler.referenceResolution = new Vector2(1280, 720);
            scaler.screenMatchMode = CanvasScaler.ScreenMatchMode.MatchWidthOrHeight;
            scaler.matchWidthOrHeight = 0.5f;
            cgo.AddComponent<GraphicRaycaster>();
        }
        // 顶部信息(回合 + 风向)
        if (turnText == null) turnText = MakeText(canvas.transform, "TurnText", new Vector2(0, 320), 22);
        if (windText == null) windText = MakeText(canvas.transform, "WindText", new Vector2(0, 296), 18);

        // 顶部玩家信息卡(动态数量, 屏幕上方左右排开)
        if (topCardsRoot_ == null) {
            topCardsRoot_ = new GameObject("TopCards", typeof(RectTransform));
            topCardsRoot_.transform.SetParent(canvas.transform, false);
            var tcrt = topCardsRoot_.GetComponent<RectTransform>();
            tcrt.anchorMin = tcrt.anchorMax = new Vector2(0.5f, 1f);
            tcrt.pivot = new Vector2(0.5f, 1f);
            tcrt.anchoredPosition = Vector2.zero;
            tcrt.sizeDelta = new Vector2(0, 0);
            tcrt.offsetMin = tcrt.offsetMax = Vector2.zero;
        }

        // 倒计时 + PASS 按钮(屏幕顶部居中偏下)
        if (countdownText_ == null) {
            countdownText_ = MakeText(canvas.transform, "Countdown", new Vector2(0, 262), 24);
        }
        if (turnPromptText_ == null) {
            turnPromptText_ = MakeText(canvas.transform, "TurnPrompt", new Vector2(0, 120), 40);
            turnPromptText_.gameObject.SetActive(false);
        }
        // PASS 按钮
        if (passBtn_ == null) {
            var btn = MakeButton(canvas.transform, "PassBtn", new Vector2(0, 220), "PASS", OnPassClick);
            btn.GetComponent<RectTransform>().sizeDelta = new Vector2(100, 36);
            passBtn_ = btn;
        }

        // 底部控制面板(角度左 / 力度条中 / HP 右), 参照截图布局
        if (angleText_ == null) BuildBottomPanel(canvas);

        // 右上角小地图
        if (minimap_ == null && field != null) minimap_ = Minimap.Create(canvas, field);

        // 左下角聊天面板(战斗中也能聊)
        if (chatPanel_ == null) chatPanel_ = Ddt.Net.UI.UIChat.Create(canvas);

        // 刷新顶部卡片(玩家就绪后)
        RefreshTopCards();
    }

    // PASS 按钮(点击) / P 键: 跳过本回合
    private UnityEngine.UI.Button passBtn_;
    public void OnPassClick() {
        if (!myTurn_ || shotLocked_) return;
        GameFacade.SendPass();
        shotLocked_ = true;
    }

    // 顶部玩家信息卡: 红队排左上, 蓝队排右上; HP=0 变灰
    private void RefreshTopCards() {
        if (topCardsRoot_ == null || field == null) return;
        foreach (var go in topCards_) { if (go != null) Destroy(go); }
        topCards_.Clear();

        var reds = new List<Ddt.Net.Battle.PlayerEntity>();
        var blues = new List<Ddt.Net.Battle.PlayerEntity>();
        foreach (var p in field.AllPlayers) {
            if (p.team == Ddt.TeamSide.TeamRed) reds.Add(p); else blues.Add(p);
        }
        // 红队从左往右排(屏幕左侧), 蓝队从右往左排(屏幕右侧)
        float cardW = 180f, cardH = 56f, gap = 8f;
        float leftStartX = -560f;   // 红队起点(屏幕坐标, 中心原点)
        for (int i = 0; i < reds.Count; i++) {
            float x = leftStartX + i * (cardW + gap);
            topCards_.Add(BuildOneCard(topCardsRoot_.transform, reds[i], new Vector2(x, -10f), cardW, cardH));
        }
        float rightStartX = 560f;   // 蓝队起点(向左排)
        for (int i = 0; i < blues.Count; i++) {
            float x = rightStartX - i * (cardW + gap);
            topCards_.Add(BuildOneCard(topCardsRoot_.transform, blues[i], new Vector2(x, -10f), cardW, cardH));
        }
    }

    private GameObject BuildOneCard(Transform parent, Ddt.Net.Battle.PlayerEntity p, Vector2 pos, float w, float h) {
        var card = new GameObject("Card_" + p.accountId, typeof(RectTransform));
        card.transform.SetParent(parent, false);
        card.AddComponent<CanvasRenderer>();
        var img = card.AddComponent<Image>();
        bool dead = p.Hp <= 0;
        bool isRed = p.team == Ddt.TeamSide.TeamRed;
        // 队伍色; HP=0 变灰
        Color baseCol = isRed ? new Color(0.85f, 0.35f, 0.35f, 1f) : new Color(0.35f, 0.5f, 0.85f, 1f);
        img.color = dead ? new Color(0.4f, 0.4f, 0.4f, 0.7f) : baseCol;
        var rt = card.GetComponent<RectTransform>();
        rt.pivot = new Vector2(0.5f, 1f);
        rt.sizeDelta = new Vector2(w, h);
        rt.anchorMin = rt.anchorMax = new Vector2(0.5f, 1f);
        rt.anchoredPosition = pos;

        // 名字
        var nameT = MakeText(card.transform, "Name", new Vector2(0, -12f), 16);
        nameT.text = p.DisplayName;
        nameT.color = dead ? new Color(0.7f, 0.7f, 0.7f) : Color.white;
        nameT.rectTransform.anchorMin = nameT.rectTransform.anchorMax = new Vector2(0.5f, 1f);
        nameT.rectTransform.pivot = new Vector2(0.5f, 1f);
        nameT.rectTransform.sizeDelta = new Vector2(w - 8, 18);

        // HP 文字
        var hpT = MakeText(card.transform, "Hp", new Vector2(0, -34f), 14);
        hpT.text = $"HP {p.Hp}/{p.MaxHp}";
        hpT.color = dead ? new Color(0.7f, 0.7f, 0.7f) : Color.white;
        hpT.rectTransform.anchorMin = hpT.rectTransform.anchorMax = new Vector2(0.5f, 1f);
        hpT.rectTransform.pivot = new Vector2(0.5f, 1f);
        hpT.rectTransform.sizeDelta = new Vector2(w - 8, 16);
        return card;
    }

    // 底部控制面板: 半透明背景条, 内含 角度(左) + 力度条(中,带数值) + HP(右)
    private void BuildBottomPanel(Canvas canvas) {
        // 背景条(屏幕底部居中)
        var panel = new GameObject("BottomPanel", typeof(RectTransform));
        panel.transform.SetParent(canvas.transform, false);
        var prt = panel.GetComponent<RectTransform>();
        prt.anchorMin = prt.anchorMax = new Vector2(0.5f, 0f);
        prt.pivot = new Vector2(0.5f, 0f);
        prt.anchoredPosition = new Vector2(0, 16f);
        prt.sizeDelta = new Vector2(900, 90);
        var pimg = panel.AddComponent<Image>();
        pimg.color = new Color(0, 0, 0, 0.55f);

        // ---- 左: 角度 ----
        var angleLabel = MakeText(panel.transform, "AngleLabel", new Vector2(-340, 18), 16);
        angleLabel.text = "角度";
        angleLabel.alignment = TextAnchor.LowerCenter;
        angleText_ = MakeText(panel.transform, "AngleVal", new Vector2(-340, -8), 30);
        angleText_.text = "45°";

        // ---- 中: 力度条 + 数值 ----
        float barW = 420f, barH = 30f;
        forceText_ = MakeText(panel.transform, "ForceLabel", new Vector2(0, 24), 16);
        forceText_.text = "力度  0%";
        forceText_.alignment = TextAnchor.LowerCenter;

        // 力度条容器(居中)
        var barRoot = new GameObject("PowerBar", typeof(RectTransform));
        barRoot.transform.SetParent(panel.transform, false);
        var brt = barRoot.GetComponent<RectTransform>();
        brt.anchorMin = brt.anchorMax = new Vector2(0.5f, 0.5f);
        brt.pivot = new Vector2(0.5f, 0.5f);
        brt.anchoredPosition = new Vector2(0, -10f);
        brt.sizeDelta = new Vector2(barW + 8, barH + 8);

        // 外框(黑)
        var border = MakeImage(barRoot.transform, "Border", new Color(0, 0, 0, 0.9f));
        border.sizeDelta = new Vector2(barW + 8, barH + 8);
        // 底槽(深灰)
        var bg = MakeImage(barRoot.transform, "Bg", new Color(0.22f, 0.22f, 0.22f, 1f));
        bg.sizeDelta = new Vector2(barW, barH);
        // 前景填充: 用纯色 Image + 生成白色 Sprite(避免 Filled 模式无 Sprite 不渲染的问题)。
        // 采用 Simple + 水平缩放(localScale.x = ratio)实现"从左往右"填充, pivot 在左中。
        var fillGo = new GameObject("Fill", typeof(RectTransform));
        fillGo.transform.SetParent(barRoot.transform, false);
        fillGo.AddComponent<CanvasRenderer>();
        forceBarFillImg_ = fillGo.AddComponent<Image>();
        forceBarFillImg_.color = new Color(0.35f, 0.85f, 0.4f);
        forceBarFillImg_.raycastTarget = false;
        forceBarFillImg_.sprite = SpriteFactory.MakeRect(8, 8, Color.white);   // 必须有 Sprite 才渲染
        forceBarFillImg_.type = Image.Type.Simple;
        var fillRt = fillGo.GetComponent<RectTransform>();
        fillRt.anchorMin = new Vector2(0f, 0.5f);
        fillRt.anchorMax = new Vector2(0f, 0.5f);
        fillRt.pivot = new Vector2(0f, 0.5f);   // 左中: scale.x 增长时从左往右
        fillRt.anchoredPosition = Vector2.zero;
        fillRt.sizeDelta = new Vector2(barW, barH);

        // ---- 右: HP ----
        var hpLabel = MakeText(panel.transform, "HpLabel", new Vector2(340, 18), 16);
        hpLabel.text = "生命";
        hpLabel.alignment = TextAnchor.LowerCenter;
        hpText_ = MakeText(panel.transform, "HpVal", new Vector2(340, -8), 30);
        hpText_.text = "100/100";

        // ---- 纸飞机道具图标(力度条与HP之间, 显示可用/冷却, 点击或F切换) ----
        var flyGo = new GameObject("FlyIcon", typeof(RectTransform));
        flyGo.transform.SetParent(panel.transform, false);
        flyGo.AddComponent<CanvasRenderer>();
        var flyBg = flyGo.AddComponent<Image>();
        flyBg.color = new Color(0, 0, 0, 0.3f);
        var flyRt = flyGo.GetComponent<RectTransform>();
        flyRt.anchorMin = flyRt.anchorMax = new Vector2(0.5f, 0.5f);
        flyRt.pivot = new Vector2(0.5f, 0.5f);
        flyRt.anchoredPosition = new Vector2(220, 0);
        flyRt.sizeDelta = new Vector2(64, 64);
        // 贴图
        var flyImgGo = new GameObject("Icon", typeof(RectTransform));
        flyImgGo.transform.SetParent(flyGo.transform, false);
        flyImgGo.AddComponent<CanvasRenderer>();
        flyIconImg_ = flyImgGo.AddComponent<Image>();
        flyIconImg_.raycastTarget = false;
        flyIconImg_.preserveAspect = true;
        Sprite flySp = Resources.Load<Sprite>("Projectiles/fly");
        if (flySp == null) {
            Texture2D flyTex = Resources.Load<Texture2D>("Projectiles/fly");
            if (flyTex != null) flySp = Sprite.Create(flyTex, new Rect(0, 0, flyTex.width, flyTex.height), new Vector2(0.5f, 0.5f), 100f);
        }
        if (flySp != null) flyIconImg_.sprite = flySp;
        flyIconImg_.rectTransform.anchorMin = Vector2.zero; flyIconImg_.rectTransform.anchorMax = Vector2.one;
        flyIconImg_.rectTransform.offsetMin = flyIconImg_.rectTransform.offsetMax = new Vector2(6, 6);
        // 冷却文字
        var flyCdGo = new GameObject("Cd", typeof(RectTransform));
        flyCdGo.transform.SetParent(flyGo.transform, false);
        flyCdGo.AddComponent<CanvasRenderer>();
        flyCdText_ = flyCdGo.AddComponent<Text>();
        flyCdText_.font = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
        flyCdText_.fontSize = 20; flyCdText_.fontStyle = FontStyle.Bold;
        flyCdText_.alignment = TextAnchor.MiddleCenter; flyCdText_.color = Color.white;
        flyCdText_.raycastTarget = false; flyCdText_.text = "";
        flyCdText_.rectTransform.anchorMin = Vector2.zero; flyCdText_.rectTransform.anchorMax = Vector2.one;
        flyCdText_.rectTransform.offsetMin = flyCdText_.rectTransform.offsetMax = Vector2.zero;
        // 点击切换纸飞机(冷却中禁用)
        var flyBtn = flyGo.AddComponent<Button>();
        flyBtn.onClick.AddListener(() => { if (flyCooldown_ <= 0) useFly_ = !useFly_; });
        flyIconBg_ = flyBg;
    }

    private static RectTransform MakeImage(Transform parent, string name, Color c) {
        var go = new GameObject(name, typeof(RectTransform));
        go.transform.SetParent(parent, false);
        go.AddComponent<CanvasRenderer>();
        var img = go.AddComponent<Image>();
        img.color = c;
        img.raycastTarget = false;
        var rt = go.GetComponent<RectTransform>();
        rt.anchorMin = rt.anchorMax = new Vector2(0.5f, 0.5f);
        rt.anchoredPosition = Vector2.zero;
        return rt;
    }

    private static Text MakeText(Transform parent, string name, Vector2 pos, int size) {
        var go = new GameObject(name);
        go.transform.SetParent(parent, false);
        go.AddComponent<CanvasRenderer>();
        var t = go.AddComponent<Text>();
        t.font = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
        t.fontSize = size;
        t.alignment = TextAnchor.MiddleCenter;
        t.color = Color.white;
        t.raycastTarget = false;
        var rt = t.rectTransform;
        rt.sizeDelta = new Vector2(260, 34);
        rt.anchorMin = rt.anchorMax = new Vector2(0.5f, 0.5f);
        rt.anchoredPosition = pos;
        return t;
    }

    private static Button MakeButton(Transform parent, string name, Vector2 pos, string label, UnityEngine.Events.UnityAction onClick) {
        var go = new GameObject(name, typeof(RectTransform));
        go.transform.SetParent(parent, false);
        go.AddComponent<CanvasRenderer>();
        var img = go.AddComponent<Image>();
        img.color = new Color(0.25f, 0.5f, 0.9f, 1f);
        var rt = go.GetComponent<RectTransform>();
        rt.sizeDelta = new Vector2(110, 38);
        rt.anchorMin = rt.anchorMax = new Vector2(0.5f, 0.5f);
        rt.anchoredPosition = pos;
        var btn = go.AddComponent<Button>();
        var colors = btn.colors; colors.highlightedColor = new Color(0.4f, 0.7f, 1f); btn.colors = colors;
        var lblGo = new GameObject("Label", typeof(RectTransform));
        lblGo.transform.SetParent(go.transform, false);
        lblGo.AddComponent<CanvasRenderer>();
        var lbl = lblGo.AddComponent<Text>();
        lbl.font = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf"); lbl.fontSize = 22;
        lbl.alignment = TextAnchor.MiddleCenter; lbl.color = Color.white; lbl.text = label; lbl.raycastTarget = false;
        lbl.rectTransform.anchorMin = Vector2.zero; lbl.rectTransform.anchorMax = Vector2.one;
        lbl.rectTransform.offsetMin = lbl.rectTransform.offsetMax = Vector2.zero;
        btn.onClick.AddListener(onClick);
        return btn;
    }

    private void UpdateHud() {
        // 顶部: 回合状态
        if (turnText) {
            var cs = NetworkManager.Instance != null ? NetworkManager.Instance.ConnState : NetworkManager.ReconnectState.None;
            if (cs == NetworkManager.ReconnectState.Reconnecting) {
                int n = NetworkManager.Instance.ReconnectAttempt;
                turnText.text = $"⚠ 连接断开, 重连中(第{n}次)...";
            } else if (!inited_) turnText.text = "等待开局...";
            else if (!myTurn_) turnText.text = "对方回合";
            else if (shotLocked_) turnText.text = "已出手, 等待结果";
            else turnText.text = "你的回合  (A/D 移动 · W/S 角度 · 空格蓄力 · P 跳过 · F 纸飞机)";
        }
        // 顶部: 风向
        if (windText) {
            string arrow = curWind_ > 0.5f ? "→" : (curWind_ < -0.5f ? "←" : "·");
            windText.text = $"风向 {arrow} {Mathf.Abs((int)curWind_)}";
        }

        // 底部: 角度
        if (angleText_) angleText_.text = $"{baseAngle_}°";

        // 底部: 力度数值 + 实时填充条
        if (forceText_) forceText_.text = charging_ ? $"力度  {(int)power_}%" : (useFly_ ? "[纸飞机模式]" : "力度  0%");
        if (forceBarFillImg_ != null) {
            float ratio = Mathf.Clamp01(power_ / 100f);
            // 用水平缩放实现"从左往右"实时填充(pivot 在左中, scale.x=ratio)
            forceBarFillImg_.rectTransform.localScale = new Vector3(ratio, 1f, 1f);
            // 力度越高颜色越红(复刻弹弹堂手感): 绿→黄→红
            forceBarFillImg_.color = ratio > 0.66f ? new Color(0.95f, 0.35f, 0.3f)
                                   : ratio > 0.33f ? new Color(1f, 0.75f, 0.2f)
                                   : new Color(0.35f, 0.85f, 0.4f);
        }

        // 底部: 自己 HP
        if (hpText_ && field != null && field.MyPlayer != null) {
            hpText_.text = $"{field.MyPlayer.Hp}/{field.MyPlayer.MaxHp}";
        }

        // 纸飞机道具图标: 冷却中显示剩余回合数+变灰; 激活时高亮; 可用时正常
        if (flyIconBg_ != null && flyCdText_ != null) {
            if (flyCooldown_ > 0) {
                flyIconBg_.color = new Color(0.2f, 0.2f, 0.2f, 0.7f);     // 变灰
                flyCdText_.text = flyCooldown_.ToString();
                if (flyIconImg_ != null) flyIconImg_.color = new Color(0.5f, 0.5f, 0.5f, 1f);
            } else {
                flyCdText_.text = "";
                if (useFly_) flyIconBg_.color = new Color(0.95f, 0.75f, 0.2f, 0.9f);  // 激活: 金黄高亮
                else flyIconBg_.color = new Color(0, 0, 0, 0.3f);
                if (flyIconImg_ != null) flyIconImg_.color = Color.white;
            }
        }
    }
}

// Session 类已移至 Game/Session.cs(加 Token 等字段, 供重连复用)
}
