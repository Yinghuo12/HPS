#include "ddt_physics.h"

namespace ddt {

ShootResult PhysicsEngine::computeTrajectory(
    double start_x, double start_y,
    int angle, double force, double wind,
    const PhysicsParams& params,
    const std::vector<float>& heightMap,
    int worldWidth, int worldHeight,
    double dt)
{
    ShootResult result;

    double af = params.air_factor;
    double wf = params.wind_factor;
    double gf = params.gravity_factor;
    double ff = params.force_factor;

    double rad = angle * PI / 180.0;
    double vx0 = ff * force * std::cos(rad);
    double vy0 = ff * force * std::sin(rad);

    // 估算最大飞行时间
    double total_t = 10.0;
    for (double t = 0.0; t < 30.0; t += 0.01) {
        double cy = start_y - ((9.8 * gf - af * vy0) * std::pow(E, -af * t)
                    + 9.8 * gf * af * t - 9.8 * gf + af * vy0) / (af * af);
        if (cy > worldHeight) {
            total_t = t + 0.5;
            break;
        }
    }

    for (double t = 0.0; t <= total_t; t += dt) {
        double exp_neg_af_t = std::pow(E, -af * t);

        double cx = start_x + ((wind * wf - af * vx0) * exp_neg_af_t
                    + wind * wf * af * t - wind * wf + af * vx0) / (af * af);

        double cy = start_y - ((9.8 * gf - af * vy0) * exp_neg_af_t
                    + 9.8 * gf * af * t - 9.8 * gf + af * vy0) / (af * af);

        TrajPoint pt;
        pt.x = static_cast<float>(cx);
        pt.y = static_cast<float>(cy);
        pt.t = static_cast<float>(t);
        result.points.push_back(pt);

        // 出界
        if (cx < 0 || cx > worldWidth || cy > worldHeight) {
            result.hit_x = static_cast<float>(cx);
            result.hit_y = static_cast<float>(cy);
            result.hit_offscreen = true;
            break;
        }

        // 地形碰撞
        int ix = static_cast<int>(cx);
        if (ix >= 0 && ix < (int)heightMap.size() && cy >= heightMap[ix]) {
            result.hit_x = static_cast<float>(cx);
            result.hit_y = static_cast<float>(cy);
            break;
        }
    }

    return result;
}

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

bool PhysicsEngine::checkHit(float hit_x, float hit_y,
                             float target_x, float target_y,
                             float radius)
{
    double dx = hit_x - target_x;
    double dy = hit_y - target_y;
    return std::sqrt(dx * dx + dy * dy) <= radius;
}

float PhysicsEngine::getSlopeAngle(float x, const std::vector<float>& heightMap) {
    int ix = static_cast<int>(x);
    if (ix < 3 || ix >= (int)heightMap.size() - 3) return 0.0f;
    float dy = heightMap[ix - 3] - heightMap[ix + 3];
    return atan2f(dy, 6.0f) * 180.0f / 3.14159265f; 
}

float PhysicsEngine::generateWind() {
    return static_cast<float>((std::rand() % 201 - 100) / 10.0);
}

} // namespace ddt
