#ifndef __DDT_PHYSICS_H__
#define __DDT_PHYSICS_H__

#include <vector>

namespace ddt { class TrajectoryPoint; }

namespace ddt {

struct PhysicsConstants {
    static constexpr double AIR_FACTOR     = 0.89927083;
    static constexpr double WIND_FACTOR    = 5.8709153;
    static constexpr double GRAVITY_FACTOR = -172.06527992;
    static constexpr double FORCE_FACTOR   = 41.0;
    static constexpr double PI             = 3.1415926536;
    static constexpr double E              = 2.718281828;
    static constexpr int    GAME_WIDTH     = 1400;
    static constexpr int    GAME_LENGTH    = 3000;
};

struct ShootResult {
    std::vector<TrajectoryPoint> points;
    float hit_x;
    float hit_y;
    bool  hit_offscreen;
};

class PhysicsEngine {
public:
    // Compute trajectory with heightmap terrain
    static ShootResult computeTrajectory(
        double start_x, double start_y,
        int angle, double force, double wind,
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

    static float generateWind();

private:
    static double computeFlightTime(double start_y, double vy0);
};

} // namespace ddt

#endif
