#ifndef __DDT_SERVICE_BASE_H__
#define __DDT_SERVICE_BASE_H__

#include <memory>
#include <string>

#include "sylar/core/log.h"
#include "sylar/core/singleton.h"

namespace ddt {

// ============================================================
// 单服务配置(从 yml 加载; 每个服务一份)
// 字段统一收集, 服务按需读取自己关心的部分。
// ============================================================
struct ServiceConfig {
    typedef sylar::Singleton<ServiceConfig> Instance;

    // 身份
    std::string service_name = "ddt";     // etcd 注册名的一部分
    std::string etcd_endpoint = "http://127.0.0.1:2379";
    int         etcd_ttl      = 30;

    // 本服务监听
    std::string host = "0.0.0.0";
    int         port = 0;                  // 各服务自定默认

    // RPC 注册到 etcd 的对外可达地址(默认用本机 + port)
    std::string advertise_host;            // 空则用 host
    int         advertise_port = 0;        // 0 则用 port

    // 公共依赖
    std::string db_host    = "127.0.0.1";
    int         db_port    = 3306;
    std::string db_user    = "root";
    std::string db_pass    = "123456";
    std::string db_name    = "ddt_game";
    int         db_pool_size = 4;

    std::string redis_host = "127.0.0.1";
    int         redis_port = 6379;

    // 网关专属
    int heartbeat_timeout        = 45;
    int heartbeat_check_interval = 10;

    // 物理/战斗参数(battle 用; 默认与配置一致)
    // 坐标系: Y 向上, gravity_factor 取正值(重力向下)
    double air_factor     = 0.89927083;
    double wind_factor    = 5.8709153;
    double gravity_factor = 172.06527992;
    double force_factor   = 41.0;
    double physics_dt     = 0.01;
    int    world_width    = 1400;
    int    world_length   = 3000;
    double max_move_per_turn = 200.0;
    int    turn_timeout   = 10;
    int    base_damage    = 25;
    double blast_radius   = 50.0;
    double hit_radius     = 30.0;
    int    min_angle      = 20;
    int    max_angle      = 65;
    int    min_force      = 0;
    int    max_force      = 100;
    double red_spawn_x    = 200.0;
    double blue_spawn_x   = 2800.0;
    double spawn_y        = 1100.0;
    int    start_hp       = 100;
    int    min_players    = 2;   // 多人战斗: 最少开局人数

    // 从 yml 加载, 缺失字段保留默认值。返回 false 表示文件无法打开。
    bool load(const std::string& path);

    // 推导对外注册地址
    std::string advertiseAddr() const;

    ServiceConfig() = default;

private:
    // 解析 "host:port" 形式
    static void parseHostPort(const std::string& s, std::string& h, int& p, int defPort);
};

// ============================================================
// 服务启动器: 统一日志/配置/信号/IOManager 启停
//
// 各服务 main 用法:
//   ServiceRunner runner("gate");
//   if(!runner.init(argc, argv)) return 1;     // 加载 conf/{name}.yml + 日志
//   sylar::IOManager iom(4, true, "gate");
//   ... 在 iom.schedule 内启动本服务的 server / RpcProvider ...
//   runner.installSignal();                    // SIGINT/SIGTERM 优雅退出
//   return 0;                                   // iom 在 main 线程跑
// ============================================================
class ServiceRunner {
public:
    ServiceRunner(const std::string& name);

    // 解析参数: [-c conf.yml], 默认 conf/{name}.yml / ../conf/ 等; 初始化日志
    bool init(int argc, char** argv);

    const std::string& name() const { return m_name; }
    const ServiceConfig& config() const { return m_config; }
    ServiceConfig& config() { return m_config; }

    // 安装 SIGINT/SIGTERM: 第一次优雅退出, 第二次强制 _exit(0)
    void installSignal();

private:
    std::string m_name;
    ServiceConfig m_config;
    std::string m_confPath;
};

} // namespace ddt

#endif
