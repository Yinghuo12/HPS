// BattleField.cs — 战场根: 持世界尺寸/地形/角色/背景/弹道回放/爆炸/相机
//
// 由 BattleController 驱动: 收到 RoomReadyNotify 后调 Init 生成全部表现元素;
// 之后 BattleController 用 SetPlayerState/PlayTrajectory/Explode 更新。
using System.Collections;
using System.Collections.Generic;
using UnityEngine;

namespace Ddt.Net.Battle {
public class BattleField : MonoBehaviour {
    public const int WORLD_W = 3000;
    public const int WORLD_H = 1400;

    public Camera battleCam;
    public TerrainRenderer terrain;
    public bool IsProjectileActive => projectile_ != null && projectile_.gameObject.activeSelf && projectile_.IsActive; // 炮弹是否在飞行

    // "忙碌中"计数: 炮弹飞行 + 爆炸动画 + 纸飞机传送 + 落地后延迟 期间 > 0。
    // 此期间回合相机跟随让位, 必须等动画全部播完才回到回合玩家。
    private int busyCount_ = 0;
    public bool IsBusy => busyCount_ > 0 || IsProjectileActive;

    /// <summary>保持忙碌 N 秒(落地后给延迟再移动相机)。</summary>
    public void KeepBusyFor(float seconds) {
        StartCoroutine(KeepBusyCoroutine(seconds));
    }
    private IEnumerator KeepBusyCoroutine(float seconds) {
        busyCount_++;
        yield return new WaitForSeconds(seconds);
        busyCount_ = Mathf.Max(0, busyCount_ - 1);
    }

    private readonly Dictionary<ulong, PlayerEntity> players_ = new Dictionary<ulong, PlayerEntity>();
    private PlayerEntity myPlayer_;
    private ProjectilePlayer projectile_;
    private SpriteRenderer bgRenderer_;   // 地图背景
    private bool landingDone_ = false;     // 降落动画是否完成

    // ---- 相机状态(复刻旧 C++ 客户端 Camera: panTo/setCenter/clamp + updateCamera 状态机) ----
    private enum CamMode { Locked, Intro, PanFollow, FollowProj, Manual }
    private CamMode camMode_ = CamMode.Locked;
    private Vector3 camTarget_;        // 跟随目标(世界坐标中心点)
    private float camSnapLerp_ = 3.5f;  // PanFollow 指数平滑系数(值越小越慢, 接近旧 panTo 手感)
    // Manual 模式(小地图/右键拖拽): 3 秒后自动回退回合跟随(复刻旧 CAM_MANUAL + m_manualTimer)
    private float manualTimer_ = 0f;
    private const float MANUAL_TIMEOUT = 3f;
    // Intro 开场巡游状态(复刻旧 CAM_INTRO + m_introPhase/m_introTimer)
    private bool introDone_ = true;
    private float camIntroSpeed_ = 1500f;  // 开场快速平移速度(世界单位/秒)
    // 当前回合玩家(回合跟随 + intro 收尾用), 由 BattleController 设置
    private ulong turnAccountId_ = 0;

    public PlayerEntity MyPlayer => myPlayer_;
    /// <summary>2D 体素地形(供弹道碰撞/坡度/站位查询)。terrain 实现 PhysicsSim.ITerrain。</summary>
    public PhysicsSim.ITerrain Terrain => terrain;

    // ---- 相机查询(Minimap 视口框/玩家拖拽用) ----
    public bool IsIntroDone => introDone_;
    public Vector3 CamCenter => battleCam != null ? battleCam.transform.position : Vector3.zero;
    public float CamViewportHalfW => CameraViewportHalfW;
    public float CamViewportHalfH => CameraViewportHalfH;
    /// <summary>当前回合玩家 accountId(intro 收尾 + 回合跟随)。</summary>
    public void SetTurnPlayer(ulong accId) { turnAccountId_ = accId; }
    /// <summary>所有玩家(小地图画点用)。</summary>
    public IEnumerable<PlayerEntity> AllPlayers => players_.Values;

    void Awake() {
        if (terrain == null) terrain = GetComponentInChildren<TerrainRenderer>();
        if (battleCam == null) battleCam = Camera.main;
        SetupCamera();
    }

    private void SetupCamera() {
        if (battleCam == null) return;
        battleCam.orthographic = true;
        battleCam.clearFlags = CameraClearFlags.SolidColor;
        // 视口高度(世界单位): 参考旧 C++ 客户端窗口 1200x700, 在此高度内可清晰看到角色与弹道。
        // 取 700 的一半=350 为半高; 之后 FocusCamera 会按屏幕比例正确 clamp。
        battleCam.orthographicSize = CameraViewportHalfH;
        battleCam.transform.position = new Vector3(WORLD_W / 2f, WORLD_H / 2f, -10);
        battleCam.transform.rotation = Quaternion.identity;
        battleCam.backgroundColor = new Color(0.53f, 0.81f, 0.92f);
        // 初始无目标(锁定), 等 Init 后跟随玩家
        camMode_ = CamMode.Locked;
        camTarget_ = new Vector3(WORLD_W / 2f, WORLD_H / 2f, 0);
    }

    /// <summary>相机视口半高(世界单位)。保持与旧客户端近似的视野大小。</summary>
    private float CameraViewportHalfH => 350f;

    /// <summary>相机视口半宽(随屏幕宽高比计算)。</summary>
    private float CameraViewportHalfW => CameraViewportHalfH * ((float)Screen.width / Screen.height);

    /// <summary>相机可移动范围(世界坐标边界), 复刻旧 Camera::clamp: max=worldW-vpW, 小于0则0。</summary>
    private void ClampCamBounds(out float minX, out float maxX, out float minY, out float maxY) {
        float halfH = CameraViewportHalfH;
        float halfW = CameraViewportHalfW;
        minX = Mathf.Max(0, halfW);
        maxX = Mathf.Max(minX, WORLD_W - halfW);
        minY = Mathf.Max(0, halfH);
        maxY = Mathf.Max(minY, WORLD_H - halfH);
    }

    /// <summary>开局初始化: 建背景+地形+角色+炮弹(多人), mapName 选背景。
    /// terrainBitmap: 服务端下发的 2D 体素位图; terrainW/H: X/Y 维度。</summary>
    public void Init(byte[] terrainBitmap, int terrainW, int terrainH, ulong myAccountId,
                     Google.Protobuf.Collections.RepeatedField<Ddt.PlayerState> allPlayers,
                     string mapName) {
        if (terrain) terrain.Build(terrainBitmap, terrainW, terrainH);

        // 地图背景
        SetupBackground(mapName);

        // 清理旧玩家
        foreach (var kv in players_) { if (kv.Value != null) Destroy(kv.Value.gameObject); }
        players_.Clear();
        myPlayer_ = null;
        landingDone_ = false;

        // 创建所有玩家(记录地表位置, 用于降落动画)
        int redIdx = 0, blueIdx = 0;
        foreach (var ps in allPlayers) {
            bool isRed = ps.Team == Ddt.TeamSide.TeamRed;
            Color color = isRed ? new Color(0.9f, 0.3f, 0.3f) : new Color(0.3f, 0.3f, 0.9f);
            string name = (isRed ? "Red" : "Blue") + (isRed ? redIdx++ : blueIdx++);
            var pe = CreatePlayer(name, ps, color, myAccountId);
            players_[ps.AccountId] = pe;
            if (ps.AccountId == myAccountId) myPlayer_ = pe;
        }

        // 炮弹(初始隐藏)
        if (projectile_ == null) {
            var projGo = new GameObject("Projectile");
            var projSr = projGo.AddComponent<SpriteRenderer>();
            projSr.sortingOrder = 10;
            projectile_ = projGo.AddComponent<ProjectilePlayer>();
            projGo.transform.SetParent(transform, false);
            projGo.SetActive(false);
        }

        // 开局降落动画: 玩家从空中落到地表
        StartCoroutine(PlayLanding(allPlayers));
    }

    // 地图背景: 按 mapName 加载 bg_ghost / bg_rainbow, 铺满世界
    private void SetupBackground(string mapName) {
        if (bgRenderer_ == null) {
            var bgGo = new GameObject("Background");
            bgGo.transform.SetParent(transform, false);
            bgRenderer_ = bgGo.AddComponent<SpriteRenderer>();
            bgRenderer_.sortingOrder = -10;   // 在所有物体后面
        }
        string spritePath = "Backgrounds/bg_" + (string.IsNullOrEmpty(mapName) ? "rainbow" : mapName);
        Sprite bg = LoadSpriteAnyType(spritePath);
        if (bg != null) {
            bgRenderer_.sprite = bg;
            bgRenderer_.drawMode = SpriteDrawMode.Sliced;
            bgRenderer_.size = new Vector2(WORLD_W, WORLD_H);
        }
        bgRenderer_.transform.position = new Vector3(WORLD_W / 2f, WORLD_H / 2f, 5);
    }

    // 开局降落: 所有玩家从 Y+800 按重力下落到地表(纯视觉, 不影响逻辑位置)
    private IEnumerator PlayLanding(Google.Protobuf.Collections.RepeatedField<Ddt.PlayerState> allPlayers) {
        // 记录每个玩家的目标(地表)位置, 起始位置设为空中
        var landingData = new List<System.Tuple<PlayerEntity, Vector3, Vector3>>();
        foreach (var ps in allPlayers) {
            var pe = GetPlayer(ps.AccountId);
            if (pe == null) continue;
            Vector3 ground = new Vector3(ps.X, ps.Y, 0);
            Vector3 sky = new Vector3(ps.X, ps.Y + 800f, 0);
            pe.transform.position = sky;
            landingData.Add(System.Tuple.Create(pe, sky, ground));
        }

        // 摄像机先看天空区域(瞬移对准开场视野)
        if (battleCam != null && landingData.Count > 0) {
            SnapCameraTo(landingData[0].Item2);
        }

        float duration = 1.5f;
        float elapsed = 0f;
        while (elapsed < duration) {
            elapsed += Time.deltaTime;
            float t = Mathf.Clamp01(elapsed / duration);
            // 缓动: 先快后慢(类似重力加速)
            float eased = t * t;
            foreach (var d in landingData) {
                d.Item1.transform.position = Vector3.Lerp(d.Item2, d.Item3, eased);
            }
            // 摄像机跟随平均位置
            if (landingData.Count > 0) {
                Vector3 avg = Vector3.zero;
                foreach (var d in landingData) avg += d.Item1.transform.position;
                avg /= landingData.Count;
                FocusCamera(avg);
            }
            yield return null;
        }
        // 确保最终位置精确
        foreach (var d in landingData) d.Item1.transform.position = d.Item3;
        landingDone_ = true;
        // 降落完成后, 启动开场巡游: 逐个展示玩家 → 停在本回合玩家(复刻旧 CAM_INTRO)
        StartCoroutine(PlayIntro(landingData));
    }

    // 开场巡游: 依次平移到每个玩家并停留, 最后切到本回合玩家(复刻旧 CAM_INTRO 多阶段)
    private IEnumerator PlayIntro(List<System.Tuple<PlayerEntity, Vector3, Vector3>> landingData) {
        introDone_ = false;
        yield return new WaitForSeconds(0.3f);

        // 逐个玩家: 平移过去 + 停留 1.5s
        foreach (var d in landingData) {
            PanToCamera(d.Item3, camIntroSpeed_);
            // 等平移完成: 用"相机到达可达目标(clamp后)"判定, 防止目标在边界外时永远等不到
            yield return new WaitUntil(() => IsCamReachedTarget());
            yield return new WaitForSeconds(1.5f);   // 停留展示
        }

        // 收尾: 平移到本回合玩家
        PlayerEntity turn = turnAccountId_ != 0 ? GetPlayer(turnAccountId_) : null;
        Vector3 finalPos = turn != null ? turn.transform.position
                          : (landingData.Count > 0 ? landingData[0].Item3 : new Vector3(WORLD_W / 2f, WORLD_H / 2f, 0));
        PanToCamera(finalPos, 800f);
        yield return new WaitUntil(() => IsCamReachedTarget());

        introDone_ = true;
        // 进入回合跟随(由 BattleController.Update 每帧 FocusCamera 接管)
    }

    // 相机是否已到达"可达目标": 把原始目标 clamp 到相机边界后, 与当前位置比对。
    // 这样即使玩家在边界外(相机到不了), 相机贴到边界时也视为到达, 不会卡死 WaitUntil。
    private bool IsCamReachedTarget() {
        if (battleCam == null) return true;
        ClampCamBounds(out float minX, out float maxX, out float minY, out float maxY);
        float tx = Mathf.Clamp(camTarget_.x, minX, maxX);
        float ty = Mathf.Clamp(camTarget_.y, minY, maxY);
        Vector3 p = battleCam.transform.position;
        float dx = p.x - tx;
        float dy = p.y - ty;
        return dx * dx + dy * dy < 2.5f * 2.5f;
    }

    public bool IsLandingDone => landingDone_;

    private PlayerEntity CreatePlayer(string name, Ddt.PlayerState st, Color color, ulong myAccountId) {
        var go = new GameObject(name);
        go.transform.SetParent(transform, false);
        var pe = go.AddComponent<PlayerEntity>();
        bool me = (st.AccountId == myAccountId);
        pe.Init(st.AccountId, st.Name, me, color, st.Team, st.Gender);
        pe.SetTerrain(terrain);   // 注入地形: 坡度旋转 + 地表贴合用
        pe.SetState(st.X, st.Y, st.Hp, st.MaxHp, st.Angle, st.Direction);
        return pe;
    }

    public PlayerEntity GetPlayer(ulong accountId) {
        PlayerEntity pe;
        players_.TryGetValue(accountId, out pe);
        return pe;
    }

    public void SetPlayerState(ulong accountId, float x, float y, int hp, int maxHp, int angle, int direction) {
        var p = GetPlayer(accountId);
        if (p != null) p.SetState(x, y, hp, maxHp, angle, direction);
    }

    /// <summary>回放弹道: 用本地物理算轨迹并播放(按武器/纸飞机选弹丸贴图)。
    /// isFly=true 时用纸飞机贴图(flyAttack), 飞行结束后由 onComplete 触发传送(无爆炸)。
    /// 服务端下发 start_y: 普通弹=shooter.y(脚底, 客户端+60 炮口); 纸飞机=炮口 originY(已含+60)。</summary>
    public void PlayTrajectory(double startX, double startY, int baseAngle, int direction,
                               double force, float wind, PhysicsSim.PhysicsParams param,
                               int weaponId, bool isFly, System.Action onComplete) {
        if (projectile_ == null || terrain == null) { onComplete?.Invoke(); return; }
        // 物理角度: 叠加发射点坡度(与服务端 onShoot 一致, 支持反抛)
        float slopeDeg = PhysicsSim.GetSlopeAngle((float)startX, terrain);
        int physAngle = (direction < 0) ? (int)(180 - baseAngle + slopeDeg) : (int)(baseAngle + slopeDeg);
        // 起点: 服务端已发实际弹道起点 originY(炮口, 含+60 和坑底抬高), 客户端直接用, 不再+60
        double originY = startY;
        var res = PhysicsSim.ComputeTrajectory(startX, originY, physAngle, force, wind, param,
                                               terrain, WORLD_W, WORLD_H, 0.01);
        // 防御: 若算出的点太少(<3), 至少补足起止两点保证飞行动画可见
        if (res.points.Count < 3) {
            res.points.Clear();
            res.points.Add(new PhysicsSim.TrajPoint { x = (float)startX, y = (float)originY, t = 0f });
            res.points.Add(new PhysicsSim.TrajPoint { x = res.hitX, y = res.hitY, t = 1.5f });
        }
        // 炮弹/纸飞机都用真实物理轨迹时间(不做时间重映射):沿轨迹插值, 落地(轨迹结束)才触发 onComplete。
        // 物理时间由 ComputeTrajectory 的 t 值决定(炮弹飞多久就播多久), 飞出上方会落回。
        // 选弹丸贴图(1f PPU, 与地图 1:1): _r 后缀=面朝右(与 player1_r 一致)
        string projName;
        if (isFly) projName = "flyAttack";
        else projName = weaponId == 2 ? "projectile" : "ice_cream";
        if (direction >= 0) projName += "_r";   // 朝右用 _r 贴图
        Sprite projSprite = LoadSpriteAnyType("Projectiles/" + projName, 1f);
        if (projSprite == null) {
            // 贴图缺失: 用醒目的橙黄圆形兜底, 确保一定看得见(旧客户端弹道也是橙黄色)
            projSprite = SpriteFactory.MakeCircle(24, new Color(1f, 0.7f, 0.2f));
        }
        projectile_.SetSprite(projSprite, direction);
        projectile_.StartPlay(res.points, onComplete);
        Debug.Log($"[Battle] PlayTrajectory: isFly={isFly} pts={res.points.Count} dur={res.points[res.points.Count-1].t:F2}s hit=({res.hitX:F0},{res.hitY:F0})");
        // 摄像机跟随弹丸(实时贴合, 复刻旧 CAM_FOLLOW_PROJ)
        StartCoroutine(FollowProjectile());
    }

    // 摄像机跟随弹丸(每帧把目标刷新为弹丸当前坐标, CamMode=FollowProj 直接贴合)
    private IEnumerator FollowProjectile() {
        while (projectile_ != null && projectile_.gameObject.activeSelf && projectile_.IsActive) {
            FollowProjectileCamera(projectile_.transform.position);
            yield return null;
        }
    }

    /// <summary>在命中点爆炸(挖坑 + 4帧动画)。</summary>
    public void Explode(float x, float y, float radius) {
        if (terrain) terrain.RemoveCircle(x, y, radius);
        // 挖坑后高度图降低: 立即把附近玩家重新贴到新地表(实时下落, 不悬空)
        ReSnapPlayersToGround(x, radius);
        StartCoroutine(PlayExplosionAnimBusy(x, y));
    }

    /// <summary>把指定 X 范围内的玩家重新贴到当前地表(爆炸挖坑后实时下落)。</summary>
    private void ReSnapPlayersToGround(float centerX, float radius) {
        if (terrain == null) return;
        foreach (var kv in players_) {
            var p = kv.Value;
            if (p == null || p.Hp <= 0) continue;
            int ix = Mathf.RoundToInt(p.X);
            // 仅处理爆炸影响范围内的列(含余量)
            if (ix < Mathf.FloorToInt(centerX - radius - 5) || ix > Mathf.CeilToInt(centerX + radius + 5)) continue;
            if (ix < 0 || ix >= terrain.Width) continue;
            float newGround = terrain.ColumnHeight(ix);
            // 用 SetState 重新贴合(它会读 terrain.ColumnHeight 把脚放到 newGround, 并重算坡度/炮管)
            p.SetState(p.X, newGround, p.Hp, p.MaxHp, p.Angle, p.Direction);
        }
    }

    /// <summary>在命中点弹出伤害数字(向上飘 + 淡出, 1 秒)。</summary>
    public void ShowDamageText(float x, float y, int damage, bool critical, bool blocked) {
        if (damage <= 0) return;
        StartCoroutine(PlayDamageFloat(x, y, damage, critical, blocked));
    }

    private IEnumerator PlayDamageFloat(float x, float y, int damage, bool critical, bool blocked) {
        var go = new GameObject("DamageText");
        go.transform.SetParent(transform, false);
        var tm = go.AddComponent<TextMesh>();
        tm.font = Resources.GetBuiltinResource<Font>("Arial.ttf");
        tm.fontSize = critical ? 80 : 60;
        tm.characterSize = 1.2f;   // 世界单位字符大小(更大更醒目)
        tm.anchor = TextAnchor.MiddleCenter;
        tm.alignment = TextAlignment.Center;
        tm.fontStyle = FontStyle.Bold;
        // 颜色: 暴击金 / 格挡蓝 / 普通 红
        tm.color = critical ? new Color(1f, 0.8f, 0.1f) : (blocked ? new Color(0.4f, 0.8f, 1f) : new Color(1f, 0.3f, 0.3f));
        tm.text = critical ? $"暴击 -{damage}" : (blocked ? $"格挡 -{damage}" : $"-{damage}");
        tm.GetComponent<Renderer>().sortingOrder = 25;
        Vector3 startPos = new Vector3(x, y + 40f, -1f);
        go.transform.position = startPos;

        float dur = 1f, elapsed = 0f;
        while (elapsed < dur) {
            elapsed += Time.deltaTime;
            float k = elapsed / dur;
            go.transform.position = startPos + new Vector3(0, 60f * k, 0);   // 向上飘
            // 前 70% 不透明, 后 30% 淡出
            Color c = tm.color;
            c.a = k < 0.7f ? 1f : Mathf.Lerp(1f, 0f, (k - 0.7f) / 0.3f);
            tm.color = c;
            yield return null;
        }
        Destroy(go);
    }

    // 爆炸动画: 期间 busyCount_+1(锁相机), 结束后 -1
    private IEnumerator PlayExplosionAnimBusy(float x, float y) {
        busyCount_++;
        yield return PlayExplosionAnim(x, y);
        busyCount_ = Mathf.Max(0, busyCount_ - 1);
    }

    // 4帧爆炸动画(explosion0~3): 逐帧变大(40→70→100→130), 每帧 90ms, 按 1f PPU 原尺寸
    private IEnumerator PlayExplosionAnim(float x, float y) {
        var go = new GameObject("Explosion");
        go.transform.position = new Vector3(x, y, 0);
        go.transform.SetParent(transform, false);
        var sr = go.AddComponent<SpriteRenderer>();
        sr.sortingOrder = 20;
        sr.drawMode = SpriteDrawMode.Sliced;

        Sprite[] frames = new Sprite[4];
        for (int i = 0; i < 4; i++) {
            frames[i] = LoadSpriteAnyType("Effects/explosion" + i, 1f);
        }
        // 逐帧尺寸递增(模拟爆炸扩散); 任一帧贴图缺失用纯色圆兜底, 保证可见
        float[] sizes = { 50f, 80f, 110f, 140f };
        float frameDuration = 0.09f;
        for (int i = 0; i < 4; i++) {
            if (frames[i] != null) {
                sr.sprite = frames[i];
            } else {
                sr.sprite = SpriteFactory.MakeCircle((int)sizes[i], new Color(1f, 0.6f, 0.2f, 0.9f));
            }
            sr.size = new Vector2(sizes[i], sizes[i]);
            sr.color = Color.white;
            yield return new WaitForSeconds(frameDuration);
        }
        // 末帧停留一瞬再消失
        yield return new WaitForSeconds(0.05f);
        Destroy(go);
    }

    /// <summary>相机跟随某点(平滑 Lerp)。</summary>
    public void FocusCamera(Vector3 worldPos) {
        if (battleCam == null) return;
        // 进入"平滑跟随"模式: 每帧以指数平滑靠近目标, 复刻旧客户端 CAM_FOLLOW_TURN 的手感。
        camMode_ = CamMode.PanFollow;
        camTarget_ = worldPos;
    }

    /// <summary>相机以固定速度平移到目标(复刻旧 Camera::panTo(speed)), 用于开场巡游。</summary>
    public void PanToCamera(Vector3 worldPos, float speed) {
        if (battleCam == null) return;
        camMode_ = CamMode.Intro;
        camTarget_ = worldPos;
        camIntroSpeed_ = Mathf.Max(1f, speed);
    }

    /// <summary>手动控相机(小地图拖拽/右键拖拽): 瞬移到点, 3 秒后自动回退回合跟随(复刻 CAM_MANUAL)。</summary>
    public void ManualCamera(Vector3 worldPos) {
        if (battleCam == null) return;
        camMode_ = CamMode.Manual;
        camTarget_ = worldPos;
        manualTimer_ = 0f;
        ApplyCamCenter(worldPos.x, worldPos.y);
    }

    /// <summary>相机是否处于用户手动控制(小地图拖拽)状态: 此时回合自动跟随应让位。</summary>
    public bool IsCameraManual => camMode_ == CamMode.Manual;

    /// <summary>相机平滑跟随弹丸(仍每帧刷新目标, 但用更快更贴的平滑)。</summary>
    public void FollowProjectileCamera(Vector3 worldPos) {
        if (battleCam == null) return;
        camMode_ = CamMode.FollowProj;
        camTarget_ = worldPos;
    }

    /// <summary>相机瞬移对准某点(复刻旧 Camera::snapTo/setCenter, 用于开场和弹道实时跟随)。</summary>
    public void SnapCameraTo(Vector3 worldPos) {
        if (battleCam == null) return;
        ApplyCamCenter(worldPos.x, worldPos.y);
    }

    // 把相机中心(世界坐标)直接写入 transform, 并做边界 clamp
    private void ApplyCamCenter(float cx, float cy) {
        ClampCamBounds(out float minX, out float maxX, out float minY, out float maxY);
        float x = Mathf.Clamp(cx, minX, maxX);
        float y = Mathf.Clamp(cy, minY, maxY);
        battleCam.transform.position = new Vector3(x, y, -10f);
    }

    // 每帧驱动相机(复刻旧 Camera::update + Game::updateCamera)
    void Update() {
        if (battleCam == null) return;
        float dt = Time.deltaTime;
        switch (camMode_) {
            case CamMode.Intro: {
                // 固定速度平移(复刻旧 Camera::update 的 m_panSpeed*dt 线性移动)
                Vector3 p = battleCam.transform.position;
                float dx = camTarget_.x - p.x;
                float dy = camTarget_.y - p.y;
                float dist = Mathf.Sqrt(dx * dx + dy * dy);
                if (dist < 2f) {
                    ApplyCamCenter(camTarget_.x, camTarget_.y);   // 到位
                } else {
                    float step = camIntroSpeed_ * dt;
                    if (step >= dist) ApplyCamCenter(camTarget_.x, camTarget_.y);
                    else ApplyCamCenter(p.x + dx / dist * step, p.y + dy / dist * step);
                }
                break;
            }
            case CamMode.PanFollow: {
                // 指数平滑(帧率无关): 越靠近越慢, 视觉自然
                float k = 1f - Mathf.Exp(-camSnapLerp_ * dt);
                Vector3 p = battleCam.transform.position;
                float nx = Mathf.Lerp(p.x, camTarget_.x, k);
                float ny = Mathf.Lerp(p.y, camTarget_.y, k);
                ApplyCamCenter(nx, ny);
                break;
            }
            case CamMode.FollowProj: {
                // 弹道飞行中: 直接贴合目标(复刻旧 CAM_FOLLOW_PROJ 的 setCenter)
                ApplyCamCenter(camTarget_.x, camTarget_.y);
                break;
            }
            case CamMode.Manual: {
                // 手动控相机停留, 3 秒后自动回退回合跟随(复刻旧 CAM_MANUAL + m_manualTimer)
                manualTimer_ += dt;
                if (manualTimer_ >= MANUAL_TIMEOUT) {
                    camMode_ = CamMode.PanFollow;
                    // 回到本回合玩家(若有)
                    PlayerEntity turn = turnAccountId_ != 0 ? GetPlayer(turnAccountId_) : null;
                    if (turn != null) camTarget_ = turn.transform.position;
                }
                break;
            }
            case CamMode.Locked:
            default:
                break;
        }
    }

    // 通用贴图加载
    // pixelsPerUnit: 战斗世界以 1像素=1世界单位 渲染(SpriteFactory/地形/角色均用 1f),
    // 因此炮弹/特效贴图也必须用 1f 才能与地图比例 1:1; UI 场景(大厅)用 100f。
    internal static Sprite LoadSpriteAnyType(string path, float pixelsPerUnit = 1f) {
        Texture2D tex = Resources.Load<Texture2D>(path);
        if (tex != null)
            return Sprite.Create(tex, new Rect(0, 0, tex.width, tex.height), new Vector2(0.5f, 0.5f), pixelsPerUnit);
        Sprite sp = Resources.Load<Sprite>(path);
        if (sp != null) return sp;
        return null;
    }
}
}
