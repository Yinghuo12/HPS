// PhysicsSim.cs — C# 物理引擎, 精确复刻服务端 src/common/ddt_physics.cc 的 computeTrajectory2D
//
// 用途: 收到 ShootResultNotify 后, 客户端用同一份公式本地复算弹道点, 供视觉回放。
// HP/命中/伤害一律用服务端权威字段, 本地轨迹仅用于动画, 即使有微小偏差也不影响逻辑。
//
// 2D 体素碰撞(方案 F): 炮弹当前坐标所在格子 terrain.IsSolid(ix, iy) 即视为命中。
// 与旧 1D "cy <= heightMap[ix]" 不同: 2D 模型下, 坑底发射穿过已挖空的格不误碰,
// 而残余平台仍能挡住炮弹——彻底解决穿地形问题。
// 公式(与 ddt_physics.cc computeHitPoint2D 逐行一致): 含空气阻力解析解 E^(-af*t), dt=0.01。
using System.Collections.Generic;
using UnityEngine;

namespace Ddt.Net.Battle {
public static class PhysicsSim {
    private const double PI = 3.1415926536;
    private const double E = 2.718281828;

    public struct PhysicsParams {
        public double airFactor;      // air_factor(>0)
        public double windFactor;     // wind_factor
        public double gravityFactor;  // gravity_factor(>0, 重力向下, Y 向上坐标系)
        public double forceFactor;    // force_factor
    }

    public struct TrajPoint {
        public float x, y, t;
    }

    public struct ShootResult {
        public List<TrajPoint> points;
        public float hitX, hitY;
        public bool hitOffscreen;
    }

    /// <summary>
    /// 二维体素地形抽象(供弹道碰撞/坡度计算), 由 TerrainRenderer 实现。
    /// 与服务端 Terrain2D 的 isSolid/columnHeight 语义一致。
    /// </summary>
    public interface ITerrain {
        /// <summary>查坐标 (x, y) 处是否有地形实体。越界返回 false。</summary>
        bool IsSolid(int x, int y);
        /// <summary>该列最高实体格的 y(等价旧 heightMap[x])。全空返回 0。</summary>
        float ColumnHeight(int x);
        /// <summary>X 维度。</summary>
        int Width { get; }
    }

    /// <summary>计算弹道轨迹(复刻 ddt_physics.cc computeTrajectory2D, Y 向上坐标系)。
    /// angle: 物理绝对角度(度)。direction=-1(朝左)时调用方应传 180-baseAngle。
    /// terrain: 2D 体素地形; 炮弹当前格 terrain.IsSolid(ix, iy) 视为命中。
    /// gravityFactor 取正值(重力向下)。</summary>
    public static ShootResult ComputeTrajectory(
        double startX, double startY,
        int angle, double force, double wind,
        PhysicsParams param,
        ITerrain terrain,
        int worldWidth, int worldHeight,
        double dt = 0.01) {

        var result = new ShootResult { points = new List<TrajPoint>() };
        double af = param.airFactor;       // 空气阻力系数(>0)
        double wf = param.windFactor;      // 风力系数
        double g  = 9.8 * param.gravityFactor;   // 重力大小(>0, 向下拉)
        double ff = param.forceFactor;     // 力度系数

        double rad = angle * PI / 180.0;
        double vx0 = ff * force * System.Math.Cos(rad);
        double vy0 = ff * force * System.Math.Sin(rad);

        // 最大飞行时间: 固定较长(15s), 让炮弹飞出上方天空后能落回来(完整抛物线)。
        double totalT = 15.0;

        // 从 dt 开始采样: 跳过 t=0 发射点, 避免"起点贴地"误判命中
        for (double t = dt; t <= totalT + dt; t += dt) {
            double em = 1.0 - System.Math.Pow(E, -af * t);   // 1 - e^(-af t)

            double cx = startX + vx0 * em / af + wind * wf * (t / af - em / (af * af));
            double cy = startY + vy0 * em / af - g * (t / af - em / (af * af));

            result.points.Add(new TrajPoint { x = (float)cx, y = (float)cy, t = (float)t });

            // 超出左右两侧: 标记出界并终止(被忽略, 不挖坑/不传送); 上方天空不终止会落回
            if (cx < 0 || cx > worldWidth) {
                result.hitX = (float)cx; result.hitY = (float)cy; result.hitOffscreen = true;
                break;
            }
            // 2D 地形碰撞: 查弹道当前坐标的格子是否实体(精确到格子, 不穿透)
            int ix = (int)cx;
            int iy = (int)cy;
            if (terrain.IsSolid(ix, iy)) {
                result.hitX = (float)cx; result.hitY = (float)cy;
                break;
            }
        }
        // 兜底: 若循环走完仍没命中, 用最后一个采样点作为落点(避免坑出现在世界原点)
        if (!result.hitOffscreen && result.points.Count > 0
            && result.hitX == 0f && result.hitY == 0f) {
            var last = result.points[result.points.Count - 1];
            result.hitX = last.x; result.hitY = last.y;
        }
        return result;
    }

    /// <summary>把基准角度(20-65)按方向转成物理绝对角度(复刻旧客户端 game_network.cc:206-209)。</summary>
    public static int BaseAngleToPhysics(int baseAngle, int direction) {
        // direction: 1=朝右, -1=朝左
        return direction >= 0 ? baseAngle : 180 - baseAngle;
    }

    /// <summary>计算地形在 x 处的坡度角(度), 复刻服务端 getSlopeAngle2D(Y 向上)。
    /// 用 x 附近 ±3 列的 columnHeight 差分: 右边高则坡度为正(朝右为上坡)。</summary>
    public static float GetSlopeAngle(float x, ITerrain terrain) {
        int ix = (int)x;
        if (ix < 3 || ix >= terrain.Width - 3) return 0f;
        float dy = terrain.ColumnHeight(ix + 3) - terrain.ColumnHeight(ix - 3);   // Y 向上: 右高则 dy>0
        float rad = Mathf.Atan2(dy, 6f);
        return rad * 180f / Mathf.PI;
    }
}
}
