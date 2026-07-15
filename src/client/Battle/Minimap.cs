// Minimap.cs — 右上角小地图(迷你版地形预览 + 玩家圆点 + 可拖动"幕布"视口框)
//
// 参考图3的弹弹堂小地图设计:
//   - 底图: 用服务端 heightMap 画一条迷你地表轮廓(Y 向上), 像一张缩略地图
//   - 玩家: 红/蓝小圆点, 位置 = 玩家世界坐标 × 缩放比
//   - 幕布: 一个半透明白框, 代表当前摄像机看到的区域; 可按住拖动 → 控制摄像机
//           幕布会实时跟随相机移动; 松手后相机进入 Manual 模式, 3 秒自动回退回合跟随
//
// 挂载: BattleController.BuildHudIfMissing 里调 Minimap.Create(canvas, field) 自动创建。
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.UI;

namespace Ddt.Net.Battle {
public class Minimap : MonoBehaviour, IPointerDownHandler, IDragHandler {
    private BattleField field_;
    private RectTransform root_;            // 小地图根(底图)
    private RawImage terrainImg_;           // 迷你地形预览
    private RectTransform viewportRect_;     // 幕布(视口框)
    private readonly List<RectTransform> playerDots_ = new List<RectTransform>();
    private readonly List<Image> playerDotImgs_ = new List<Image>();
    // 复用的存活玩家暂存(避免 RefreshPlayers 每帧 new List → GC 卡顿)
    private readonly List<PlayerEntity> aliveScratch_ = new List<PlayerEntity>();
    // 预生成的红/蓝圆点 sprite(只生成一次, dot 创建时按队赋值, 避免每帧 MakeCircle 哈希查表)
    private Sprite redDotSprite_;
    private Sprite blueDotSprite_;

    private const float MM_W = 240f;
    private const float MM_H = 140f;
    // 迷你地形纹理分辨率(每 x 列采几个点, 越大越细腻但越耗)
    private const int TEX_W = 240;
    private const int TEX_H = 140;

    /// <summary>创建小地图并挂到 canvas 下。返回 Minimap 组件。</summary>
    public static Minimap Create(Canvas canvas, BattleField field) {
        if (canvas == null || field == null) return null;
        var go = new GameObject("Minimap", typeof(RectTransform));
        go.transform.SetParent(canvas.transform, false);
        var rt = go.GetComponent<RectTransform>();
        rt.anchorMin = rt.anchorMax = new Vector2(1f, 1f);   // 右上角
        rt.pivot = new Vector2(0.5f, 0.5f);   // 枢轴居中: local 坐标系以中心为原点, 转换更直观
        rt.anchoredPosition = new Vector2(-MM_W / 2f - 12f, -MM_H / 2f - 12f);
        rt.sizeDelta = new Vector2(MM_W, MM_H);

        // 底框(深色半透明)
        var frame = go.AddComponent<Image>();
        frame.color = new Color(0.10f, 0.11f, 0.15f, 0.85f);

        var mm = go.AddComponent<Minimap>();
        mm.field_ = field;
        mm.root_ = rt;

        // 迷你地形预览(铺满除标题外的区域)
        var texGo = new GameObject("Terrain", typeof(RectTransform));
        texGo.transform.SetParent(rt, false);
        var texImg = texGo.AddComponent<RawImage>();
        texImg.raycastTarget = false;
        var texRt = texGo.GetComponent<RectTransform>();
        texRt.anchorMin = Vector2.zero;
        texRt.anchorMax = Vector2.one;
        texRt.offsetMin = texRt.offsetMax = Vector2.zero;   // 铺满整个小地图
        mm.terrainImg_ = texImg;

        // 标题
        var title = MakeChildText(rt, "Title", "地图", 13, new Color(1, 1, 1, 0.65f));
        title.rectTransform.anchorMin = title.rectTransform.anchorMax = new Vector2(0.5f, 1f);
        title.rectTransform.pivot = new Vector2(0.5f, 1f);
        title.rectTransform.anchoredPosition = new Vector2(0, -2f);
        title.rectTransform.sizeDelta = new Vector2(MM_W, 16);
        title.alignment = TextAnchor.UpperCenter;

        // 幕布(视口框): 半透明白边矩形, 代表当前相机视野
        var vpGo = new GameObject("Viewport", typeof(RectTransform));
        vpGo.transform.SetParent(rt, false);
        var vpImg = vpGo.AddComponent<Image>();
        vpImg.color = new Color(1, 1, 1, 0.12f);   // 半透明白底, 表示"当前画面"
        vpImg.raycastTarget = false;                // 不挡底图拖拽
        var outline = vpGo.AddComponent<Outline>();
        outline.effectColor = new Color(1, 1, 1, 0.9f);
        outline.effectDistance = new Vector2(1.5f, 1.5f);
        mm.viewportRect_ = vpGo.GetComponent<RectTransform>();
        mm.viewportRect_.anchorMin = mm.viewportRect_.anchorMax = new Vector2(0.5f, 0.5f);   // 与根枢轴一致
        mm.viewportRect_.pivot = new Vector2(0, 0);   // 幕布左下角对齐 anchoredPosition

        return mm;
    }

    void Update() {
        if (field_ == null || root_ == null) return;
        RefreshTerrain();
        RefreshPlayers();
        RefreshViewport();
    }

    // 迷你地形预览: 从 2D 体素地形(field_.Terrain)采样每列 columnHeight, 画地表轮廓图(Y 向上)
    private void RefreshTerrain() {
        if (terrainImg_ == null) return;
        var terrain = field_.Terrain;
        if (terrain == null) return;
        // 只在地形就绪后重建一次纹理, 避免每帧重画
        if (terrainImg_.texture != null && terrain.Width > 0) return;

        var tex = new Texture2D(TEX_W, TEX_H) { filterMode = FilterMode.Point };
        Color sky = new Color(0.20f, 0.45f, 0.70f, 1f);    // 天空蓝
        Color grass = new Color(0.30f, 0.65f, 0.30f, 1f);  // 草地绿
        Color dirt = new Color(0.45f, 0.36f, 0.28f, 1f);   // 泥土棕
        Color[] px = new Color[TEX_W * TEX_H];
        for (int x = 0; x < TEX_W; x++) {
            // 采样世界列
            int worldX = (int)((float)x / TEX_W * terrain.Width);
            if (worldX >= terrain.Width) worldX = terrain.Width - 1;
            float groundY = Mathf.Clamp01(terrain.ColumnHeight(worldX) / BattleField.WORLD_H);   // 归一化(Y 向上)
            int top = Mathf.RoundToInt(groundY * TEX_H);
            for (int y = 0; y < TEX_H; y++) {
                Color c;
                if (y > top) c = sky;                          // 地面之上: 天空
                else if (top - y < 3) c = grass;               // 表层: 草
                else c = dirt;                                 // 深层: 泥土
                px[y * TEX_W + x] = c;
            }
        }
        tex.SetPixels(px);
        tex.Apply();
        terrainImg_.texture = tex;
    }

    // 刷新玩家点(数量动态增减); 死亡玩家(Hp<=0)不显示。
    // 用复用的 aliveScratch_ 收集存活玩家, 避免每帧 new List 触发 GC(战斗中长时间运行会累积卡顿)。
    // 玩家点 sprite 在创建时按队一次性赋值(预生成红/蓝), 之后每帧只改位置 + SetActive,
    // 不再每帧调 SpriteFactory.MakeCircle(消除哈希查表开销)。
    private void RefreshPlayers() {
        aliveScratch_.Clear();
        foreach (var pp in field_.AllPlayers) {
            if (pp.Hp > 0) aliveScratch_.Add(pp);
        }
        int playerCount = aliveScratch_.Count;
        // 懒生成红/蓝圆点 sprite(只一次)
        if (redDotSprite_ == null) redDotSprite_ = SpriteFactory.MakeCircle(8, new Color(1f, 0.3f, 0.3f));
        if (blueDotSprite_ == null) blueDotSprite_ = SpriteFactory.MakeCircle(8, new Color(0.3f, 0.55f, 1f));
        while (playerDots_.Count < playerCount) {
            var go = new GameObject("Dot", typeof(RectTransform));
            go.transform.SetParent(root_, false);
            var img = go.AddComponent<Image>();
            img.raycastTarget = false;
            img.color = Color.white;   // 贴图已带色
            var drt = go.GetComponent<RectTransform>();
            drt.anchorMin = drt.anchorMax = new Vector2(0.5f, 0.5f);   // 与根枢轴一致(中心原点)
            drt.pivot = new Vector2(0.5f, 0.5f);
            drt.sizeDelta = new Vector2(8, 8);
            playerDots_.Add(drt);
            playerDotImgs_.Add(img);
        }
        for (int i = 0; i < playerDots_.Count; i++) {
            bool active = i < playerCount;
            playerDots_[i].gameObject.SetActive(active);
            if (!active) continue;
            Vector3 world = aliveScratch_[i].transform.position;
            playerDots_[i].anchoredPosition = WorldToMinimapLocal(world);
            // sprite 只在首次或换队时赋值(避免每帧赋值触发 native 调用)
            var target = aliveScratch_[i].team == Ddt.TeamSide.TeamRed ? redDotSprite_ : blueDotSprite_;
            if (playerDotImgs_[i].sprite != target) playerDotImgs_[i].sprite = target;
        }
    }

    // 刷新幕布: 框住相机当前看到的世界区域
    private void RefreshViewport() {
        if (viewportRect_ == null) return;
        Vector3 cam = field_.CamCenter;
        float halfW = field_.CamViewportHalfW;
        float halfH = field_.CamViewportHalfH;
        Vector2 bl = WorldToMinimapLocal(new Vector3(cam.x - halfW, cam.y - halfH, 0));
        Vector2 tr = WorldToMinimapLocal(new Vector3(cam.x + halfW, cam.y + halfH, 0));
        viewportRect_.anchoredPosition = bl;
        viewportRect_.sizeDelta = new Vector2(Mathf.Max(4, tr.x - bl.x), Mathf.Max(4, tr.y - bl.y));
    }

    // 世界坐标 → 小地图内 local 坐标(枢轴居中, local 范围 -MM/2..+MM/2)
    private Vector2 WorldToMinimapLocal(Vector3 world) {
        float sx = MM_W / BattleField.WORLD_W;
        float sy = MM_H / BattleField.WORLD_H;
        // local 以中心为原点: 中心 = (0,0), 所以减去半宽半高
        return new Vector2(world.x * sx - MM_W / 2f, world.y * sy - MM_H / 2f);
    }

    // 小地图内 local 坐标(中心原点) → 世界坐标(严格互逆)
    private Vector3 MinimapLocalToWorld(Vector2 local) {
        float sx = BattleField.WORLD_W / MM_W;
        float sy = BattleField.WORLD_H / MM_H;
        return new Vector3((local.x + MM_W / 2f) * sx, (local.y + MM_H / 2f) * sy, 0);
    }

    // ---- 拖拽交互(复刻旧 C++ m_minimapDragging: 按住拖动, 实时把鼠标对应世界点喂给相机) ----
    public void OnPointerDown(PointerEventData e) {
        HandleDrag(e);
    }

    public void OnDrag(PointerEventData e) {
        HandleDrag(e);
    }

    private void HandleDrag(PointerEventData e) {
        if (field_ == null || root_ == null) return;
        // Overlay Canvas 下相机参数应为 null; pivot 居中, local 范围 -MM/2..+MM/2
        if (!RectTransformUtility.ScreenPointToLocalPointInRectangle(root_, e.position, null, out Vector2 local)) return;
        local.x = Mathf.Clamp(local.x, -MM_W / 2f, MM_W / 2f);
        local.y = Mathf.Clamp(local.y, -MM_H / 2f, MM_H / 2f);
        Vector3 world = MinimapLocalToWorld(local);
        field_.ManualCamera(world);
    }

    private static Text MakeChildText(Transform parent, string name, string content, int size, Color c) {
        var go = new GameObject(name, typeof(RectTransform));
        go.transform.SetParent(parent, false);
        go.AddComponent<CanvasRenderer>();
        var t = go.AddComponent<Text>();
        t.font = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
        t.fontSize = size;
        t.alignment = TextAnchor.MiddleCenter;
        t.color = c;
        t.raycastTarget = false;
        t.text = content;
        return t;
    }
}
}
