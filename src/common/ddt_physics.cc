#include "ddt_physics.h"
#include "terrain2d.h"
#include <algorithm>

namespace ddt {

// 坐标系: Y 向上(原点在底部)。
//   - 重力把炮弹向下拉(cy 随时间先升后降)。
//   - 2D 碰撞: terrain.isSolid(ix, iy) 查弹道当前格是否实体。
//   - gravity_factor 取正值(重力大小), 与 Unity 客户端一致。

int PhysicsEngine::calculateDamage(float hit_x, float hit_y,
                                   float target_x, float target_y,
                                   int base_damage, float blast_radius)
{
    double dx = hit_x - target_x;
    double dy = hit_y - target_y;
    double dist = std::sqrt(dx * dx + dy * dy);

    if (dist > blast_radius) return 0;
    double ratio = 1.0 - (dist / blast_radius);
    return static_cast<int>(base_damage * ratio);
}

float PhysicsEngine::generateWind() {
    return static_cast<float>((std::rand() % 201 - 100) / 10.0);
}

// ============================================================
// Terrain2D 版本: 二维体素碰撞
// ============================================================

// 轻量版: 只算落点, 不存轨迹点(服务端用, 避免 fiber 栈上分配大 vector)
ShootResult PhysicsEngine::computeHitPoint2D(
    double start_x, double start_y,
    int angle, double force, double wind,
    const PhysicsParams& params,
    const Terrain2D& terrain,
    int worldWidth, int worldHeight,
    double dt)
{
    ShootResult result;
    double af = params.air_factor;
    double wf = params.wind_factor;
    double g  = 9.8 * params.gravity_factor;
    double ff = params.force_factor;

    double rad = angle * PI / 180.0;
    double vx0 = ff * force * std::cos(rad);
    double vy0 = ff * force * std::sin(rad);

    double total_t = 15.0;

    for (double t = dt; t <= total_t + dt; t += dt) {
        double em = 1.0 - std::pow(E, -af * t);
        double cx = start_x + vx0 * em / af + wind * wf * (t / af - em / (af * af));
        double cy = start_y + vy0 * em / af - g * (t / af - em / (af * af));

        if (cx < 0 || cx > worldWidth) {
            result.hit_x = static_cast<float>(cx);
            result.hit_y = static_cast<float>(cy);
            result.hit_offscreen = true;
            return result;
        }

        // 2D 碰撞: 查弹道当前坐标的格子是否实体
        int ix = static_cast<int>(cx);
        int iy = static_cast<int>(cy);
        if (terrain.isSolid(ix, iy)) {
            result.hit_x = static_cast<float>(cx);
            result.hit_y = static_cast<float>(cy);
            return result;
        }
    }
    result.hit_offscreen = true;
    return result;
}

ShootResult PhysicsEngine::computeTrajectory2D(
    double start_x, double start_y,
    int angle, double force, double wind,
    const PhysicsParams& params,
    const Terrain2D& terrain,
    int worldWidth, int worldHeight,
    double dt)
{
    ShootResult result;
    double af = params.air_factor;
    double wf = params.wind_factor;
    double g  = 9.8 * params.gravity_factor;
    double ff = params.force_factor;

    double rad = angle * PI / 180.0;
    double vx0 = ff * force * std::cos(rad);
    double vy0 = ff * force * std::sin(rad);

    double total_t = 15.0;

    for (double t = dt; t <= total_t + dt; t += dt) {
        double em = 1.0 - std::pow(E, -af * t);
        double cx = start_x + vx0 * em / af + wind * wf * (t / af - em / (af * af));
        double cy = start_y + vy0 * em / af - g * (t / af - em / (af * af));

        TrajPoint pt;
        pt.x = static_cast<float>(cx);
        pt.y = static_cast<float>(cy);
        pt.t = static_cast<float>(t);
        result.points.push_back(pt);

        if (cx < 0 || cx > worldWidth) {
            result.hit_x = static_cast<float>(cx);
            result.hit_y = static_cast<float>(cy);
            result.hit_offscreen = true;
            break;
        }

        // 2D 碰撞
        int ix = static_cast<int>(cx);
        int iy = static_cast<int>(cy);
        if (terrain.isSolid(ix, iy)) {
            result.hit_x = static_cast<float>(cx);
            result.hit_y = static_cast<float>(cy);
            break;
        }
    }

    if (!result.hit_offscreen && !result.points.empty()
        && result.hit_x == 0.0f && result.hit_y == 0.0f) {
        const TrajPoint& last = result.points.back();
        result.hit_x = last.x;
        result.hit_y = last.y;
    }
    return result;
}

float PhysicsEngine::getSlopeAngle2D(float x, const Terrain2D& terrain) {
    int ix = static_cast<int>(x);
    if(ix < 3 || ix >= terrain.width() - 3) return 0.0f;
    // 用 columnHeight 做差分
    float dy = terrain.columnHeight(ix + 3) - terrain.columnHeight(ix - 3);
    return atan2f(dy, 6.0f) * 180.0f / 3.14159265f;
}

} // namespace ddt
