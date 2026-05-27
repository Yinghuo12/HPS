#include "ddt_config.h"
#include <yaml-cpp/yaml.h>
#include <iostream>

namespace ddt {

GameConfig& GameConfig::Instance() {
    static GameConfig inst;
    return inst;
}

bool GameConfig::load(const std::string& path) {
    YAML::Node root;
    try {
        root = YAML::LoadFile(path);
    } catch (const YAML::Exception& e) {
        std::cerr << "Failed to load config " << path << ": " << e.what() << std::endl;
        return false;
    }

    if (auto n = root["physics"]) {
        if (n["air_factor"])     air_factor     = n["air_factor"].as<double>();
        if (n["wind_factor"])    wind_factor    = n["wind_factor"].as<double>();
        if (n["gravity_factor"]) gravity_factor = n["gravity_factor"].as<double>();
        if (n["force_factor"])   force_factor   = n["force_factor"].as<double>();
        if (n["dt"])             physics_dt     = n["dt"].as<double>();
    }

    if (auto n = root["game"]) {
        if (n["world_width"])       world_width       = n["world_width"].as<int>();
        if (n["world_length"])      world_length      = n["world_length"].as<int>();
        if (n["move_speed"])        move_speed        = n["move_speed"].as<double>();
        if (n["move_cooldown"])     move_cooldown     = n["move_cooldown"].as<double>();
        if (n["max_move_per_turn"]) max_move_per_turn = n["max_move_per_turn"].as<double>();
        if (n["turn_timeout"])      turn_timeout      = n["turn_timeout"].as<int>();
    }

    if (auto n = root["combat"]) {
        if (n["base_damage"])   base_damage   = n["base_damage"].as<int>();
        if (n["blast_radius"])  blast_radius  = n["blast_radius"].as<double>();
        if (n["hit_radius"])    hit_radius    = n["hit_radius"].as<double>();
        if (n["player_hitbox"]) player_hitbox = n["player_hitbox"].as<double>();
        if (n["min_angle"])     min_angle     = n["min_angle"].as<int>();
        if (n["max_angle"])     max_angle     = n["max_angle"].as<int>();
        if (n["min_force"])     min_force     = n["min_force"].as<int>();
        if (n["max_force"])     max_force     = n["max_force"].as<int>();
    }

    if (auto n = root["terrain"]) {
        if (n["base_height"])        terrain_base_height      = n["base_height"].as<double>();
        if (n["min_height"])         terrain_min_height       = n["min_height"].as<double>();
        if (n["max_height"])         terrain_max_height       = n["max_height"].as<double>();
        if (n["valley_amplitude"])   terrain_valley_amplitude = n["valley_amplitude"].as<double>();
        if (n["valley_scale"])       terrain_valley_scale     = n["valley_scale"].as<double>();
        if (n["valley_base"])        terrain_valley_base      = n["valley_base"].as<double>();
        if (n["valley_base_scale"])  terrain_valley_base_scale = n["valley_base_scale"].as<double>();
        if (n["grass_depth"])        terrain_grass_depth      = n["grass_depth"].as<int>();
        if (n["explode_radius"])     terrain_explode_radius   = n["explode_radius"].as<double>();
    }

    if (auto n = root["player"]) {
        if (n["start_hp"])      start_hp      = n["start_hp"].as<int>();
        if (n["size"])           player_size   = n["size"].as<int>();
        if (n["red_spawn_x"])    red_spawn_x   = n["red_spawn_x"].as<double>();
        if (n["blue_spawn_x"])   blue_spawn_x  = n["blue_spawn_x"].as<double>();
        if (n["spawn_y"])        spawn_y       = n["spawn_y"].as<double>();
        if (n["move_boundary"])  move_boundary = n["move_boundary"].as<double>();
    }

    if (auto n = root["server"]) {
        if (n["host"])       host       = n["host"].as<std::string>();
        if (n["port"])       port       = n["port"].as<int>();
        if (n["db_host"])    db_host    = n["db_host"].as<std::string>();
        if (n["db_port"])    db_port    = n["db_port"].as<int>();
        if (n["db_user"])    db_user    = n["db_user"].as<std::string>();
        if (n["db_pass"])    db_pass    = n["db_pass"].as<std::string>();
        if (n["db_name"])    db_name    = n["db_name"].as<std::string>();
        if (n["redis_host"])    redis_host    = n["redis_host"].as<std::string>();
        if (n["redis_port"])    redis_port    = n["redis_port"].as<int>();
        if (n["db_pool_size"])  db_pool_size  = n["db_pool_size"].as<int>();
    }

    std::cout << "Config loaded from " << path << std::endl;
    return true;
}

} // namespace ddt
