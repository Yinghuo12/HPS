#ifndef __COMMON_DDT_PHYSICS_H__
#define __COMMON_DDT_PHYSICS_H__

#include <vector>
#include <cmath>
#include <cstdlib>
#include <algorithm>

namespace ddt {

class Terrain2D;   // 前置声明

// 共享轨迹点（不依赖 Protobuf，避免与 proto 生成的 TrajectoryPoint 类冲突）
struct TrajPoint {
    float x, y, t;
};

// 物理常量参数（从 YAML 配置读取，前后端共享）
// 坐标系约定: Y 向上(原点在底部)。
// gravity_factor 取正值(重力大小, 把炮弹向下拉)。
struct PhysicsParams {
    double air_factor     = 0.89927083;
    double wind_factor    = 5.8709153;
    double gravity_factor = 172.06527992;   // 正: 重力向下(Y 向上坐标系)
    double force_factor   = 41.0;
};

// 射击计算结果
struct ShootResult {
    std::vector<TrajPoint> points;
    float hit_x = 0;
    float hit_y = 0;
    bool  hit_offscreen = false;
};

class PhysicsEngine {
public:
    // AOE 伤害: 按落点到目标的距离衰减算伤害(base_damage 满伤, blast_radius 外为 0)。
    static int calculateDamage(float hit_x, float hit_y,
                               float target_x, float target_y,
                               int base_damage = 25,
                               float blast_radius = 50.0f);

    // ---- Terrain2D 版本(二维体素碰撞, 彻底解决穿地形) ----
    // 碰撞判定: terrain.isSolid(ix, iy) 替代 cy <= heightMap[ix]
    // 爆炸在下方挖坑后, 上方平台仍能挡住炮弹(1D heightMap 做不到)

    /// 轻量版: 只算落点(hit_x/hit_y/hit_offscreen), 不存轨迹点。
    /// 用于服务端(不需要回放动画), 避免在 fiber 栈上分配 1500+ 点的 vector 导致栈溢出。
    static ShootResult computeHitPoint2D(
        double start_x, double start_y,
        int angle, double force, double wind,
        const PhysicsParams& params,
        const Terrain2D& terrain,
        int worldWidth, int worldHeight,
        double dt = 0.01);

    static ShootResult computeTrajectory2D(
        double start_x, double start_y,
        int angle, double force, double wind,
        const PhysicsParams& params,
        const Terrain2D& terrain,
        int worldWidth, int worldHeight,
        double dt = 0.01);

    /// 计算地形在 x 处的坡度角（度）
    /// 返回值 > 0 表示地形向右上倾斜（对朝右为上坡）
    /// 返回值 < 0 表示地形向右下倾斜（对朝右为下坡）
    static float getSlopeAngle2D(float x, const Terrain2D& terrain);

    static float generateWind();

private:
    static constexpr double PI = 3.1415926536;
    static constexpr double E  = 2.718281828;
};

} // namespace ddt

#endif
