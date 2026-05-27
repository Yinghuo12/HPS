#include "ddt_physics.h"
#include "ddt_config.h"
#include "ddt.pb.h"
#include <cmath>
#include <cstdlib>
#include <algorithm>

namespace ddt {

ShootResult PhysicsEngine::computeTrajectory(
    double start_x, double start_y,
    int angle, double force, double wind,
    const std::vector<float>& heightMap,
    int worldWidth, int worldHeight,
    double dt)
{
    ShootResult result;
    result.hit_x = 0;
    result.hit_y = 0;
    result.hit_offscreen = false;

    auto& cfg = GameConfig::Instance();
    double af = cfg.air_factor;
    double wf = cfg.wind_factor;
    double gf = cfg.gravity_factor;
    double ff = cfg.force_factor;
    double pi = 3.1415926536;
    double e  = 2.718281828;

    double rad = angle * pi / 180.0;
    double vx0 = ff * force * std::cos(rad);
    double vy0 = ff * force * std::sin(rad);

    double total_t = computeFlightTime(start_y, vy0);

    for (double t = 0.0; t <= total_t; t += dt) {
        double exp_neg_af_t = std::pow(e, -af * t);

        double cx = start_x + ((wind * wf - af * vx0) * exp_neg_af_t
                    + wind * wf * af * t - wind * wf + af * vx0) / (af * af);

        double cy = start_y - ((9.8 * gf - af * vy0) * exp_neg_af_t
                    + 9.8 * gf * af * t - 9.8 * gf + af * vy0) / (af * af);

        TrajectoryPoint pt;
        pt.set_x(static_cast<float>(cx));
        pt.set_y(static_cast<float>(cy));
        pt.set_t(static_cast<float>(t));
        result.points.push_back(pt);

        // Out of bounds
        if (cx < 0 || cx > worldWidth || cy > worldHeight) {
            result.hit_x = static_cast<float>(cx);
            result.hit_y = static_cast<float>(cy);
            result.hit_offscreen = true;
            break;
        }

        // Terrain collision using heightmap
        int ix = static_cast<int>(cx);
        if (ix >= 0 && ix < (int)heightMap.size() && cy >= heightMap[ix]) {
            result.hit_x = static_cast<float>(cx);
            result.hit_y = static_cast<float>(cy);
            break;
        }
    }

    return result;
}

double PhysicsEngine::computeFlightTime(double start_y, double vy0) {
    auto& cfg = GameConfig::Instance();
    double af = cfg.air_factor;
    double gf = cfg.gravity_factor;
    double e  = 2.718281828;

    for (double t = 0.0; t < 30.0; t += 0.01) {
        double cy = start_y - ((9.8 * gf - af * vy0) * std::pow(e, -af * t)
                    + 9.8 * gf * af * t - 9.8 * gf + af * vy0) / (af * af);
        if (cy > cfg.world_width) {
            return t + 0.5;
        }
    }
    return 10.0;
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

float PhysicsEngine::generateWind() {
    return static_cast<float>((std::rand() % 201 - 100) / 10.0);
}

} // namespace ddt
