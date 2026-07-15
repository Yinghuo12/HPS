// TerrainRenderer.cs — 用 2D 体素位图渲染可破坏地形
//
// 方案 F: 底层存储从 1D heightMap 改为 2D 体素位图 solid_[x + y*w]。
//   - Build: 解析服务端下发的 terrain_bitmap(行优先 bit) → solid_ → 画纹理
//   - RemoveCircle: 只清除圆内格子的 solid_(不再整列降低) → 平台保留, 站着不掉
//   - IsSolid/ColumnHeight: 实现 PhysicsSim.ITerrain, 供弹道碰撞/坡度/站位查询
//
// 性能优化:
//   - 纹理用 RGBA32 格式(4字节/像素, 内存 1/4), 复用 Color32[] 缓冲(不再每次 Build 分配 64MB)
//   - RemoveCircle 用 Apply(false) 关闭 mipmap 更新(避免全纹理 GPU 上传)
//
// 与服务端 Terrain2D(removeCircle/isSolid/columnHeight) 语义完全一致,
// 彻底解决旧 1D 模型"整列降低丢失平台 / 客户端 2D 与服务端 1D 判定不一致"的问题。
//
// 坐标: 世界 x∈[0,worldW], y∈[0,worldH]; 纹理像素 x 对应世界 x(1:1), 纹理 y=0 在底部。
using UnityEngine;

namespace Ddt.Net.Battle {
[RequireComponent(typeof(SpriteRenderer))]
public class TerrainRenderer : MonoBehaviour, PhysicsSim.ITerrain {
    public int worldW = 3000;
    public int worldH = 1400;

    private Texture2D tex_;
    private SpriteRenderer sr_;

    // 2D 体素位图: solid_[x + y * terrainW_]。true=有地形实体。
    // 仅覆盖地形层(terrainH_, ~420), 天空区域(y>=terrainH_)不存(恒空)。
    private bool[] solid_;
    private int terrainW_;
    private int terrainH_;

    // 复用的像素缓冲(RGBA32, 4字节/像素), 避免 Build 每次分配 worldW*worldH Color 数组(~64MB)
    private Color32[] pixelsBuf_;

    // 颜色
    private static readonly Color32 GRASS = new Color32(34, 139, 34, 255);
    private static readonly Color32 DIRT  = new Color32(139, 119, 101, 255);
    private static readonly Color32 CLEAR = new Color32(0, 0, 0, 0);
    private const int GRASS_DEPTH = 10;

    void Awake() { sr_ = GetComponent<SpriteRenderer>(); }

    public int Width => terrainW_;

    /// <summary>查坐标 (x, y) 处是否有地形实体(实现 ITerrain)。越界返回 false。</summary>
    public bool IsSolid(int x, int y) {
        if (solid_ == null || x < 0 || x >= terrainW_ || y < 0 || y >= terrainH_) return false;
        return solid_[x + y * terrainW_];
    }

    /// <summary>该列最高实体格的 y(实现 ITerrain, 等价旧 heightMap[x])。全空返回 0。</summary>
    public float ColumnHeight(int x) {
        if (solid_ == null || x < 0 || x >= terrainW_) return 0f;
        // 从顶部往下找第一个实体格
        for (int y = terrainH_ - 1; y >= 0; --y) {
            if (solid_[x + y * terrainW_]) return y + 1;   // 格的顶部 = y+1
        }
        return 0f;
    }

    /// <summary>用服务端下发的 2D 体素位图初始化地形。
    /// bitmap: 行优先 bit(bit[x + y*w]), 1=实体。w/h: X/Y 维度。</summary>
    public void Build(byte[] bitmap, int w, int h) {
        terrainW_ = w;
        terrainH_ = h;
        solid_ = new bool[w * h];
        // 解析位图: 每 byte 8 格, 行优先
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                int bitIdx = x + y * w;
                int byteIdx = bitIdx >> 3;
                int bitOff = bitIdx & 7;
                if (byteIdx < bitmap.Length && ((bitmap[byteIdx] >> bitOff) & 1) != 0) {
                    solid_[x + y * w] = true;
                }
            }
        }
        // 复用像素缓冲(RGBA32): 仅在首次或尺寸变化时分配
        if (pixelsBuf_ == null || pixelsBuf_.Length != worldW * worldH) {
            pixelsBuf_ = new Color32[worldW * worldH];
        }
        tex_ = new Texture2D(worldW, worldH, TextureFormat.RGBA32, false) { filterMode = FilterMode.Point };
        Redraw();
        Sprite sp = Sprite.Create(tex_, new Rect(0, 0, worldW, worldH), new Vector2(0.5f, 0.5f), 1f);
        sr_.sprite = sp;
        // 放到世界中点
        transform.position = new Vector3(worldW / 2f, worldH / 2f, 0f);
    }

    private void Redraw() {
        // 天空区域先全部置透明(只需做一次, 之后地形区域覆盖)
        Color32[] px = pixelsBuf_;
        for (int i = 0; i < px.Length; i++) px[i] = CLEAR;
        // 逐列填土: 该列最高实体格以下的像素填草/泥色
        for (int x = 0; x < worldW; x++) {
            int top = Mathf.RoundToInt(ColumnHeight(x));
            if (top > worldH) top = worldH;
            for (int y = 0; y < top; y++) {
                px[y * worldW + x] = (top - y <= GRASS_DEPTH) ? GRASS : DIRT;
            }
        }
        tex_.SetPixels32(px);
        tex_.Apply(false);   // false: 不更新 mipmap(地形无 mipmap, 省一次 GPU 上传)
    }

    /// <summary>在命中点挖一个圆坑(爆炸效果)。只清除圆内格子的 solid_——
    /// 圆外平台完整保留(彻底修复旧 1D "整列降低丢失平台")。同时清纹理像素。</summary>
    public void RemoveCircle(float cx, float cy, float radius) {
        if (solid_ == null) return;
        int ix = Mathf.RoundToInt(cx), iy = Mathf.RoundToInt(cy);
        int r = Mathf.RoundToInt(radius);
        int r2 = r * r;
        for (int dx = -r; dx <= r; dx++) {
            for (int dy = -r; dy <= r; dy++) {
                if (dx * dx + dy * dy > r2) continue;
                int px = ix + dx, py = iy + dy;
                if (px < 0 || px >= terrainW_ || py < 0 || py >= terrainH_) continue;
                // 只清圆内格子(不再整列降低)
                solid_[px + py * terrainW_] = false;
                if (px < worldW && py < worldH) tex_.SetPixel(px, py, CLEAR);
            }
        }
        tex_.Apply(false);   // false: 关闭 mipmap 更新, 避免每次爆炸全纹理 GPU 上传
    }
}
}
