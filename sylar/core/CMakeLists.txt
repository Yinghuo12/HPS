# 核心基础层：日志 / 配置 / 线程同步原语 / 系统运行时工具(sys_util) / 环境与守护进程
# 依赖方向：util/(叶) ← core/ ← scheduler/ ← net/ ← http/orm/rpc/
sylar_add_sources(
    log.cc
    asynclog.cc
    config.cc
    sys_util.cc
    env.cc
    daemon.cc
    thread.cc
)
