#ifndef __SYLAR_ENV_H__
#define __SYLAR_ENV_H__

#include "sylar/singleton.h"
#include "sylar/thread.h"
#include <map>
#include <vector>

namespace sylar {

class Env {
public:
    typedef RWMutex RWMutexType;
    bool init(int argc, char** argv);

    void add(const std::string& key, const std::string& val);
    bool has(const std::string& key);
    void del(const std::string& key);
    std::string get(const std::string& key, const std::string& default_value = "");

    void addHelp(const std::string& key, const std::string& desc);
    void removeHelp(const std::string& key);
    void printHelp();

    const std::string& getExe() const { return m_exe;}
    const std::string& getCwd() const { return m_cwd;}

    bool setEnv(const std::string& key, const std::string& val);
    std::string getEnv(const std::string& key, const std::string& default_value = "");
private:
    RWMutexType m_mutex;                                         // 互斥锁
    std::map<std::string, std::string> m_args;                   // 存储命令行参数的键值对
    std::vector<std::pair<std::string, std::string> > m_helps;   // 存储帮助信息的键值对，key是参数名，value是参数描述

    std::string m_program;       // 程序名称
    std::string m_exe;           // 程序绝对路径
    std::string m_cwd;           // 程序工作目录
};

typedef sylar::Singleton<Env> EnvMgr;

}

#endif