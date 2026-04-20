#ifndef __SYLAR_DAEMON_H__
#define __SYLAR_DAEMON_H__

#include <unistd.h>
#include <functional>
#include "sylar/singleton.h"

namespace sylar {

struct ProcessInfo {
    pid_t parent_id = 0;            // 父进程id
    uint64_t parent_start_time = 0; // 父进程启动时间
    pid_t main_id = 0;              // 主进程id 
    uint64_t main_start_time = 0;   // 主进程启动时间
    uint32_t restart_count = 0;     // 主进程重启的次数
    std::string toString() const;
};

typedef sylar::Singleton<ProcessInfo> ProcessInfoMgr;

// function: 真正要做的主函数
int start_daemon(int argc, char** argv, std::function<int(int argc, char** argv)> main_cb, bool is_daemon);

}

#endif