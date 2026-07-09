#ifndef __DDT_CONFIG_H__
#define __DDT_CONFIG_H__

#include <string>

namespace ddt {

struct GameConfig {
    static GameConfig& Instance();

    bool load(const std::string& path);

    // physics
    double air_factor     = 0.89927083;
    double wind_factor    = 5.8709153;
    double gravity_factor = -172.06527992;
    double force_factor   = 41.0;
    double physics_dt     = 0.01;

    // game
    int    world_width       = 1400;
    int    world_length      = 3000;
    double move_speed        = 20.0;
    double move_cooldown     = 0.2;
    double max_move_per_turn = 200.0;
    int    turn_timeout      = 30;

    // combat
    int    base_damage  = 25;
    double blast_radius = 50.0;
    double hit_radius   = 30.0;
    double player_hitbox = 25.0;
    int    min_angle    = 20;
    int    max_angle    = 65;
    int    min_force    = 0;
    int    max_force    = 100;

    // terrain
    double terrain_base_height     = 1100.0;
    double terrain_min_height      = 600.0;
    double terrain_max_height      = 1250.0;
    double terrain_valley_amplitude = 2380.0;
    double terrain_valley_scale    = 3.0;
    double terrain_valley_base     = 2860.0;
    double terrain_valley_base_scale = 2.1;
    int    terrain_grass_depth     = 10;
    double terrain_explode_radius  = 30.0;

    // player
    int    start_hp       = 100;
    int    player_size    = 40;
    double red_spawn_x    = 200.0;
    double blue_spawn_x   = 2800.0;
    double spawn_y        = 1100.0;
    double move_boundary  = 2950.0;

    // server
    std::string host       = "0.0.0.0";
    int         port       = 8073;
    std::string db_host    = "127.0.0.1";
    int         db_port    = 3306;
    std::string db_user    = "root";
    std::string db_pass    = "123456";
    std::string db_name    = "ddt_game";
    std::string redis_host = "127.0.0.1";
    int         redis_port = 6379;
    int         db_pool_size = 4;

    // heartbeat
    int heartbeat_timeout        = 45;   // 秒，超时剔除阈值
    int heartbeat_check_interval = 10;   // 秒，扫描周期

private:
    GameConfig() = default;
};

} // namespace ddt

#endif
