#ifndef __COMMON_DDT_PHYSICS_H__
#define __COMMON_DDT_PHYSICS_H__

#include <vector>
#include <cmath>
#include <cstdlib>
#include <algorithm>

namespace ddt {

// 共享轨迹点（不依赖 Protobuf，避免与 proto 生成的 TrajectoryPoint 类冲突）
struct TrajPoint {
    float x, y, t;
};

// 物理常量参数（从 YAML 配置读取，前后端共享）
struct PhysicsParams {
    double air_factor     = 0.89927083;
    double wind_factor    = 5.8709153;
    double gravity_factor = -172.06527992;
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
    /**
     * 计算弹道轨迹
     * @param start_x   发射点 X
     * @param start_y   发射点 Y
     * @param angle     发射角度（度）
     * @param force     发射力度
     * @param wind      风力
     * @param params    物理常量
     * @param heightMap 地形高度图
     * @param worldWidth  世界宽度（越界判定）
     * @param worldHeight 世界高度（越界判定）
     * @param dt        物理步长
     */
    static ShootResult computeTrajectory(
        double start_x, double start_y,
        int angle, double force, double wind,
        const PhysicsParams& params,
        const std::vector<float>& heightMap,
        int worldWidth, int worldHeight,
        double dt = 0.01);

    static int calculateDamage(float hit_x, float hit_y,
                               float target_x, float target_y,
                               int base_damage = 25,
                               float blast_radius = 50.0f);

    static bool checkHit(float hit_x, float hit_y,
                         float target_x, float target_y,
                         float radius = 25.0f);

    /**
     * 计算地形在 x 处的坡度角（度）
     * 返回值 > 0 表示地形向右上倾斜（对朝右为上坡）
     * 返回值 < 0 表示地形向右下倾斜（对朝右为下坡）
     */
    static float getSlopeAngle(float x, const std::vector<float>& heightMap);

    static float generateWind();

private:
    static constexpr double PI = 3.1415926536;
    static constexpr double E  = 2.718281828;
};

} // namespace ddt

#endif
