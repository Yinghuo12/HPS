#include "service_base.h"

#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>

#include <yaml-cpp/yaml.h>

#include "sylar/core/config.h"
#include "sylar/core/env.h"
#include "sylar/core/log.h"
#include "sylar/scheduler/iomanager.h"

namespace ddt {

static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

// ---- ServiceConfig ----

void ServiceConfig::parseHostPort(const std::string& s, std::string& h, int& p, int defPort) {
    auto pos = s.find(':');
    if(pos == std::string::npos) { h = s; p = defPort; return; }
    h = s.substr(0, pos);
    p = std::atoi(s.substr(pos + 1).c_str());
}

bool ServiceConfig::load(const std::string& path) {
    YAML::Node root;
    try {
        root = YAML::LoadFile(path);
    } catch(const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << "load config fail: " << path << ", " << e.what();
        return false;
    }

    auto readStr = [&](const char* key, const char* section, std::string& out) {
        if(root[section] && root[section][key]) out = root[section][key].as<std::string>();
    };
    auto readStrTop = [&](const char* key, std::string& out) {
        if(root[key]) out = root[key].as<std::string>();
    };
    auto readInt = [&](const char* key, const char* section, int& out) {
        if(root[section] && root[section][key]) out = root[section][key].as<int>();
    };
    auto readIntTop = [&](const char* key, int& out) {
        if(root[key]) out = root[key].as<int>();
    };
    auto readDbl = [&](const char* key, const char* section, double& out) {
        if(root[section] && root[section][key]) out = root[section][key].as<double>();
    };

    // 顶层
    readStrTop("service_name", service_name);
    readStrTop("etcd_endpoint", etcd_endpoint);
    readIntTop("etcd_ttl", etcd_ttl);
    readStrTop("host", host);
    readIntTop("port", port);
    readStrTop("advertise_host", advertise_host);
    readIntTop("advertise_port", advertise_port);

    // db
    readStr("host", "db", db_host);
    readInt("port", "db", db_port);
    readStr("user", "db", db_user);
    readStr("pass", "db", db_pass);
    readStr("name", "db", db_name);
    readInt("pool_size", "db", db_pool_size);

    // redis
    readStr("host", "redis", redis_host);
    readInt("port", "redis", redis_port);

    // heartbeat
    readInt("timeout", "heartbeat", heartbeat_timeout);
    readInt("check_interval", "heartbeat", heartbeat_check_interval);

    // physics / game / combat
    readDbl("air_factor",     "physics", air_factor);
    readDbl("wind_factor",    "physics", wind_factor);
    readDbl("gravity_factor", "physics", gravity_factor);
    readDbl("force_factor",   "physics", force_factor);
    readDbl("dt",             "physics", physics_dt);
    readInt("world_width",    "game",    world_width);
    readInt("world_length",   "game",    world_length);
    readDbl("max_move_per_turn", "game", max_move_per_turn);
    readInt("turn_timeout",   "game",    turn_timeout);
    readInt("base_damage",    "combat",  base_damage);
    readDbl("blast_radius",   "combat",  blast_radius);
    readDbl("hit_radius",     "combat",  hit_radius);
    readInt("min_angle",      "combat",  min_angle);
    readInt("max_angle",      "combat",  max_angle);
    readInt("min_force",      "combat",  min_force);
    readInt("max_force",      "combat",  max_force);
    readDbl("red_spawn_x",    "player",  red_spawn_x);
    readDbl("blue_spawn_x",   "player",  blue_spawn_x);
    readDbl("spawn_y",        "player",  spawn_y);
    readInt("start_hp",       "player",  start_hp);
    readInt("min_players",    "game",    min_players);

    return true;
}

std::string ServiceConfig::advertiseAddr() const {
    std::string h = advertise_host.empty() ? host : advertise_host;
    int p = advertise_port ? advertise_port : port;
    if(h == "0.0.0.0") h = "127.0.0.1";   // 注册地址不能是通配
    return h + ":" + std::to_string(p);
}

// ---- ServiceRunner ----

static std::atomic<int> g_force_exit{0};

ServiceRunner::ServiceRunner(const std::string& name)
    : m_name(name) {
    m_config.service_name = name;
}

bool ServiceRunner::init(int argc, char** argv) {
    // 解析 -c 参数; 默认 conf/{name}.yml
    std::string confPath;
    for(int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if((a == "-c" || a == "--conf") && i + 1 < argc) {
            confPath = argv[++i];
        }
    }
    if(confPath.empty()) {
        const char* candidates[] = {
            ("conf/" + m_name + ".yml").c_str(),
            ("../conf/" + m_name + ".yml").c_str(),
            ("../../conf/" + m_name + ".yml").c_str(),
            ("bin/conf/" + m_name + ".yml").c_str(),
            nullptr
        };
        for(int i = 0; candidates[i]; ++i) {
            std::ifstream f(candidates[i]);
            if(f.good()) { confPath = candidates[i]; break; }
        }
    }

    if(confPath.empty() || !m_config.load(confPath)) {
        std::cerr << "[" << m_name << "] no config found, using defaults\n";
    } else {
        std::cerr << "[" << m_name << "] config loaded: " << confPath << "\n";
    }
    // 加载 sylar 全局配置(如 fiber.stack_size)。
    // 从同一个 yml 文件读取: sylar Config 只认它注册过的 key(如 fiber.stack_size),
    // 业务字段(service_name/port/db 等)会被忽略, 互不干扰。
    // 必须在 IOManager(创建 fiber) 之前调用, 否则栈大小不生效。
    if(!confPath.empty()) {
        try {
            YAML::Node root = YAML::LoadFile(confPath);
            sylar::Config::LoadFromYaml(root);
        } catch(const std::exception& e) {
            std::cerr << "[" << m_name << "] sylar config load skip: " << e.what() << "\n";
        }
    }
    // service_name 强制以构造名为准(不被 yml 覆盖)
    m_config.service_name = m_name;

    // 日志: 框架 LoggerMgr 默认 root logger 已输出到 stdout, 这里不再额外配置
    SYLAR_LOG_INFO(g_logger) << "service [" << m_name << "] init, port=" << m_config.port
        << " etcd=" << m_config.etcd_endpoint;
    return true;
}

static void onSignal(int sig) {
    (void)sig;
    if(++g_force_exit >= 2) {
        // 第二次信号: 强制退出
        _exit(0);
    }
    std::cerr << "\n[shutdown] graceful, press again to force exit\n";
    // 触发优雅退出: 各服务若有 server->stop() 可在此调度;
    // 简单起见这里直接退 IOManager: 由 main 中的 iom 析构链路兜底。
    sylar::IOManager::GetThis()->stop();
}

void ServiceRunner::installSignal() {
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT,  onSignal);
    signal(SIGTERM, onSignal);
}

} // namespace ddt
