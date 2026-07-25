// PlayerEntity.cs — 战斗内一个角色的表现
//
// 彻底剥离 UI 组件，改用标准的 SpriteRenderer(血条) 和 TextMesh(名字/HP文本)
// 100% 保证在 2D 世界坐标下以正确的分辨率和尺寸完美渲染，不依赖任何 Canvas。
using UnityEngine;

namespace Ddt.Net.Battle {
public class PlayerEntity : MonoBehaviour {
    public ulong accountId;
    public Ddt.TeamSide team;   // 队伍
    public Ddt.Gender gender;   // 性别
    public bool isMe;

    private SpriteRenderer bodySr_;
    private Sprite maleRight_, maleLeft_, femaleRight_, femaleLeft_;

    private Transform body_;    // 角色渲染节点
    private Transform barrel_;  // 炮管(本机玩家才有)

    // 瞄准虚线(本机玩家才有):沿(基础角度+坡度)方向的黄色虚线
    private LineRenderer aimLine_;

    // 2D 世界渲染组件
    private SpriteRenderer hpBgSr_;
    private SpriteRenderer hpFillSr_;
    private TextMesh nameTm_;
    private TextMesh hpTm_;

    // 地形引用(坡度计算 + 地表贴合用), 由 BattleField.CreatePlayer 注入
    private TerrainRenderer terrain_;

    public float X { get; private set; }
    public float Y { get; private set; }
    public int Hp { get; private set; }
    public int MaxHp { get; private set; }
    public int Angle { get; private set; }      // 基准角度
    public int Direction { get; private set; }  // 1=右 -1=左
    public string DisplayName { get; private set; } = "";   // 显示名(顶部卡片用)

    // 位置插值(消除瞬移/卡顿): SetState 设目标, Update 每帧 lerp 靠近。
    private float targetX_, targetY_;
    private bool posInited_ = false;   // 首次 SetState 直接定位(无插值)

    public void Init(ulong accId, string name, bool me, Color color, Ddt.TeamSide teamSide = Ddt.TeamSide.TeamRed, Ddt.Gender genderSide = Ddt.Gender.None) {
        accountId = accId;
        team = teamSide;
        gender = genderSide;
        isMe = me;
        
        // 预加载贴图, 枢轴底部中心(脚): 旋转绕脚, 角色垂直地形法线, 头朝上
        Vector2 footPivot = new Vector2(0.5f, 0f);
        maleRight_ = LoadSpriteAnyType("Players/player1_r", footPivot);
        maleLeft_  = LoadSpriteAnyType("Players/player1", footPivot);
        femaleRight_ = LoadSpriteAnyType("Players/player2_r", footPivot);
        femaleLeft_  = LoadSpriteAnyType("Players/player2", footPivot);
        
        BuildVisuals(color);
        SetName(name);
    }

    /// <summary>注入地形(坡度计算 + 地表贴合用), 由 BattleField.CreatePlayer 调用。</summary>
    public void SetTerrain(TerrainRenderer t) { terrain_ = t; }

    // 强制加载为 Texture2D，并以 1f PPU 构建 Sprite，确保 1:1 地图尺寸
    private static Sprite LoadSpriteAnyType(string path) {
        return LoadSpriteAnyType(path, new Vector2(0.5f, 0.5f));
    }
    // pivot: 贴图锚点(0.5,0)=底部中心(让旋转绕脚, 角色垂直地形法线); (0.5,0.5)=中心
    private static Sprite LoadSpriteAnyType(string path, Vector2 pivot) {
        Texture2D tex = Resources.Load<Texture2D>(path);
        if (tex != null) {
            return Sprite.Create(tex, new Rect(0, 0, tex.width, tex.height), pivot, 1f);
        }
        Sprite sp = Resources.Load<Sprite>(path);
        if (sp != null) return sp;
        return null;
    }

    /// <summary>强制设位置(降落动画用): 直接写 transform + target, 不触发坡度/HP/Sprite 更新。</summary>
    public void ForcePosition(Vector3 pos) {
        X = pos.x; Y = pos.y;
        targetX_ = pos.x; targetY_ = pos.y;
        transform.position = pos;
        posInited_ = true;
    }

    public void SetState(float x, float y, int hp, int maxHp, int angle, int direction) {
        X = x; Y = y; Hp = hp; MaxHp = maxHp; Angle = angle; Direction = direction;
        // 视觉贴合地表: 用 2D 体素地形的 columnHeight 把脚贴到该列最高实体格, 不悬空。
        float groundY = y;
        if (terrain_ != null) {
            int ix = Mathf.RoundToInt(x);
            if (ix >= 0 && ix < terrain_.Width) groundY = terrain_.ColumnHeight(ix);
        }
        // 坠落死亡只由服务端权威 hp=0 决定, 不由本地地形猜。
        if (Hp <= 0 && !falling_) {
            StartCoroutine(PlayFallDeath());
        }
        // 本机玩家: 每帧精确增量更新, 直接定位(lerp 会引入滞后 + 闪烁)。
        // 远程玩家: 离散 RPC 更新(每 15px 一个), 用 lerp 平滑过渡。
        targetX_ = x;
        targetY_ = groundY;
        if (isMe || !posInited_) {
            posInited_ = true;
            transform.position = new Vector3(x, groundY, 0f);
        }
        UpdateSpriteForDirection(direction);
        UpdateHp();
        UpdateSlopeAndBarrel();
    }

    // 远程玩家位置插值: 消除 RPC 离散更新(每 15px 一个 MoveNotify)造成的瞬移。
    // 本机玩家不插值(SetState 直接定位, 每帧增量精确)。
    void Update() {
        if (!posInited_ || falling_ || isMe) return;
        Vector3 p = transform.position;
        float dx = targetX_ - p.x, dy = targetY_ - p.y;
        if (dx * dx + dy * dy < 0.25f) return;   // 已到位, 跳过
        float k = 0.2f;   // 远程平滑系数(~6帧到位, 消除 15px RPC 跳变)
        float smooth = 1f - Mathf.Exp(-k * Time.deltaTime * 60f);   // 帧率无关
        float nx = p.x + dx * smooth;
        float ny = p.y + dy * smooth;
        // 大跳变(>200px, 如回合重置/传送): 直接到位, 不插值
        if (Mathf.Abs(dx) > 200f) nx = targetX_;
        if (Mathf.Abs(dy) > 200f) ny = targetY_;
        p.x = nx; p.y = ny;
        transform.position = p;
    }

    private bool falling_ = false;

    // 坠落死亡: 持续向下加速下坠, 离开屏幕(纯视觉)
    private System.Collections.IEnumerator PlayFallDeath() {
        if (falling_) yield break;
        falling_ = true;
        float vy = 0f;
        // 一直下坠, 直到掉出世界底部很远
        while (transform.position.y > -400f) {
            vy -= 2000f * Time.deltaTime;        // 重力加速
            transform.position += new Vector3(0, vy * Time.deltaTime, 0);
            yield return null;
        }
        // 隐藏尸体
        if (body_ != null) body_.gameObject.SetActive(false);
    }

    // 当前玩家在的坡度角(度): 由 2D 体素地形的 columnHeight 差分得到(Y 向上, 朝右上坡为正)
    private float CurrentSlopeDeg() {
        if (terrain_ == null) return 0f;
        return PhysicsSim.GetSlopeAngle(X, terrain_);
    }

    // 身体随地形坡度旋转(绕脚, 垂直地形法线, 头朝上)。
    // 炮管/瞄准线是 transform(根, 不旋转)的子节点, 用"绝对角度"(相对水平方向)设置,
    // 与身体独立(复刻旧 C++ game_renderer.cc: body 单独画 -slope, barrel 用 absAngle=base+slope)。
    // 反抛机制: 朝左时 absAngle = 180-base-slope, 当 slope 大(陡坡)时该值 < 90 → 炮口转向右,
    // 即面朝左的玩家在陡坡上加大基础角度可向右发射。
    private void UpdateSlopeAndBarrel() {
        float slopeDeg = CurrentSlopeDeg();
        // 身体旋转: 垂直于斜坡站立(脚不动, 头朝坡上方向)。
        if (body_ != null) body_.localRotation = Quaternion.Euler(0, 0, slopeDeg);

        // 炮管/瞄准线绝对视觉角度(指向弹道方向, 与服务端物理一致):
        // 朝右: Angle + slope
        // 朝左: 180 - Angle + slope
        // (slope 在左上坡为负: 朝左时 180-base+slope = 180-base-|slope|, 角度减小,
        //  当 < 90 时炮口转向右 → 实现反抛。原 -slope 符号错误导致角度被加大→炮管朝左下。)
        bool facingRight = Direction >= 0;
        float barrelDeg = facingRight ? (Angle + slopeDeg) : (180 - Angle + slopeDeg);

        if (barrel_ != null) {
            barrel_.localRotation = Quaternion.Euler(0, 0, barrelDeg);
        }
        UpdateAimLine(barrelDeg);
    }

    private void UpdateSpriteForDirection(int direction) {
        if (bodySr_ == null) return;
        Sprite sp = null;
        bool faceRight = direction >= 0;
        if (gender == Ddt.Gender.Male) {
            sp = faceRight ? maleRight_ : maleLeft_;
        } else if (gender == Ddt.Gender.Female) {
            sp = faceRight ? femaleRight_ : femaleLeft_;
        }

        if (sp != null) {
            bodySr_.sprite = sp;
        } else if (bodySr_.sprite == null) {
            // 贴图缺失时才用纯色方块占位(保持原 teamColor)
            bodySr_.sprite = SpriteFactory.MakeRect(60, 60, bodySr_.color);
        }
        // 关键修复：贴图本体始终用白色(1,1,1)，不染色，保留原画真实颜色。
        // 队伍归属改由队伍徽章(TeamBadge)体现，与旧 C++ 客户端 vec4(1.0) 行为一致。
        bodySr_.color = Color.white;
    }

    private void BuildVisuals(Color color) {
        // 1. 角色身体 —— 贴图枢轴在底部中心(脚), 故 body 节点 localPosition=(0,0,0) 即脚踩 transform 原点(地面)。
        //    旋转 body 时绕脚转, 角色始终垂直地形法线、头朝上。
        var bodyGo = new GameObject("Body");
        body_ = bodyGo.transform;
        body_.SetParent(transform, false);
        bodySr_ = bodyGo.AddComponent<SpriteRenderer>();
        bodySr_.sortingOrder = 5;
        bodySr_.color = Color.white;   // 不染色, 保留贴图原色
        UpdateSpriteForDirection(1);
        body_.localPosition = new Vector3(0, 0, 0);   // 脚在原点(贴图底部中心枢轴)

        // 2. HP 条底槽 (80x8 像素, 灰黑色)
        var hpBgGo = new GameObject("HpBg");
        hpBgGo.transform.SetParent(transform, false);
        hpBgSr_ = hpBgGo.AddComponent<SpriteRenderer>();
        hpBgSr_.sprite = SpriteFactory.MakeRect(80, 8, new Color(0.2f, 0.2f, 0.2f, 0.6f));
        hpBgSr_.sortingOrder = 7;
        hpBgGo.transform.localPosition = new Vector3(0, 90, 0);

        // 3. HP 填充条 (绿色)
        var hpFillGo = new GameObject("HpFill");
        hpFillGo.transform.SetParent(transform, false);
        hpFillSr_ = hpFillGo.AddComponent<SpriteRenderer>();
        hpFillSr_.sprite = SpriteFactory.MakeRect(80, 8, Color.green);
        hpFillSr_.sortingOrder = 8;
        hpFillGo.transform.localPosition = new Vector3(0, 90, 0);

        // 4. 名字 (使用 2D 空间 TextMesh)
        var nameGo = new GameObject("NameText");
        nameGo.transform.SetParent(transform, false);
        nameTm_ = nameGo.AddComponent<TextMesh>();
        nameTm_.font = Resources.GetBuiltinResource<Font>("Arial.ttf");
        nameTm_.fontSize = 32;          // 调大字体防止模糊
        nameTm_.characterSize = 0.5f;   // 缩放字符尺寸保证清晰度
        nameTm_.anchor = TextAnchor.MiddleCenter;
        nameTm_.alignment = TextAlignment.Center;
        nameTm_.color = Color.white;
        nameGo.transform.localPosition = new Vector3(0, 115, -0.5f); // 修复：Z 轴设为 -0.5f 靠近相机
        nameTm_.GetComponent<Renderer>().sortingOrder = 9;           // 修复：强制设置排序层在最前，不与地形冲突

        // 5. HP 数字文本
        var hpTxtGo = new GameObject("HpText");
        hpTxtGo.transform.SetParent(transform, false);
        hpTm_ = hpTxtGo.AddComponent<TextMesh>();
        hpTm_.font = Resources.GetBuiltinResource<Font>("Arial.ttf");
        hpTm_.fontSize = 24;
        hpTm_.characterSize = 0.5f;
        hpTm_.anchor = TextAnchor.MiddleCenter;
        hpTm_.alignment = TextAlignment.Center;
        hpTm_.color = Color.white;
        hpTxtGo.transform.localPosition = new Vector3(0, 75, -0.5f);  // 修复：Z 轴设为 -0.5f
        hpTm_.GetComponent<Renderer>().sortingOrder = 9;             // 修复：排序层设为 9

        // 6. 炮管(短黄条): 挂在 body_ 下, 随身体一起随坡度旋转; 角度=基础角度(相对身体)
        //    位置在身体上部(约 45, 接近肩膀/炮口), body 枢轴在脚(0), 身体高 40~59
        {
            var barrelGo = new GameObject("Barrel");
            barrel_ = barrelGo.transform;
            barrel_.SetParent(transform, false);   // 挂根节点(不随身体旋转), 用绝对角度
            var bsr = barrelGo.AddComponent<SpriteRenderer>();
            bsr.sprite = SpriteFactory.MakeRect(30, 6, new Color(1f, 0.85f, 0.2f));
            bsr.sortingOrder = 6;
            barrel_.localPosition = new Vector3(0, 45, 0);
        }

        // 7. 瞄准虚线(仅本机玩家): 挂根节点(不随身体旋转), 用绝对角度
        if (isMe) {
            var aimGo = new GameObject("AimLine");
            aimGo.transform.SetParent(transform, false);   // 挂根节点
            aimLine_ = aimGo.AddComponent<LineRenderer>();
            aimLine_.useWorldSpace = false;
            aimLine_.loop = false;
            aimLine_.material = new Material(Shader.Find("Sprites/Default"));
            aimLine_.startColor = new Color(1f, 0.9f, 0.3f, 0.85f);
            aimLine_.endColor = new Color(1f, 0.9f, 0.3f, 0.2f);
            aimLine_.startWidth = 2f;   // 更细
            aimLine_.endWidth = 2f;
            aimLine_.sortingOrder = 6;
            aimLine_.textureMode = LineTextureMode.RepeatPerSegment;
            aimLine_.numCornerVertices = 0;
            aimLine_.positionCount = 2;
            aimLine_.SetPosition(0, new Vector3(0, 45, -0.5f));
            aimLine_.SetPosition(1, new Vector3(0, 45, -0.5f));
        }
    }

    // 瞄准虚线: 沿 absAngleDeg 方向(相对水平, 绝对角度)画短细黄色虚线; 挂根节点(不随身体旋转)
    private void UpdateAimLine(float absAngleDeg) {
        if (aimLine_ == null) return;
        float rad = absAngleDeg * Mathf.Deg2Rad;
        Vector3 start = new Vector3(0, 45, -0.5f);   // 炮口(根节点局部坐标, 身体上部)
        const float len = 150f;                       // 较短
        Vector3 end = start + new Vector3(Mathf.Cos(rad), Mathf.Sin(rad), 0) * len;
        // 虚线: 在 start..end 之间放交替的实/空段(短线段实现虚线效果)
        float totalDist = Vector3.Distance(start, end);
        float dashLen = 8f, gapLen = 6f;             // 实线8 / 空6 (更细碎的虚线)
        var pts = new System.Collections.Generic.List<Vector3>();
        float d = 0f;
        bool dash = true;
        while (d < totalDist) {
            float segEnd = Mathf.Min(d + (dash ? dashLen : gapLen), totalDist);
            if (dash) {
                pts.Add(Vector3.Lerp(start, end, d / totalDist));
                pts.Add(Vector3.Lerp(start, end, segEnd / totalDist));
            }
            d = segEnd;
            dash = !dash;
        }
        if (pts.Count < 2) { aimLine_.positionCount = 2; aimLine_.SetPosition(0, start); aimLine_.SetPosition(1, end); return; }
        aimLine_.positionCount = pts.Count;
        aimLine_.SetPositions(pts.ToArray());
    }

    private void SetName(string name) {
        DisplayName = name;
        if (nameTm_ != null) nameTm_.text = name;
    }

    private void UpdateHp() {
        float ratio = MaxHp > 0 ? (float)Hp / MaxHp : 0f;
        ratio = Mathf.Clamp01(ratio);

        // 重新设计：通过本地缩放与平移，让血条完美实现“从左往右”缩放
        if (hpFillSr_ != null) {
            hpFillSr_.transform.localScale = new Vector3(ratio, 1f, 1f);
            
            // 默认中心在 0，缩放后需要向左平移以对齐左边缘
            float width = 80f; // 宽度为 80 像素
            float offsetX = -width * (1f - ratio) / 2f;
            hpFillSr_.transform.localPosition = new Vector3(offsetX, 90f, 0f);

            // 依据血量百分比变色
            hpFillSr_.color = ratio > 0.5f ? Color.green : (ratio > 0.2f ? Color.yellow : Color.red);
        }

        if (hpTm_ != null) {
            hpTm_.text = $"{Hp}/{MaxHp}";
        }
    }
}
}