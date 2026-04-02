#include "sylar/config.h"

namespace sylar {
Config::ConfigVarMap Config::s_datas;   // 静态成员变量必须在类外定义

// 通过名字查找配置项，返回配置项基类指针，返回nullptr表示没有找到
ConfigVarBase::ptr Config::LookupBase(const std::string& name) {
    auto it = s_datas.find(name);
    return it == s_datas.end() ? nullptr : it->second;
}

//"A.B", 10
//A:
//  B: 10
//  C: str


// 把树形YAML展平成 "A.B" 这样的键值对
static void ListAllMember(const std::string& prefix, const YAML::Node& node, std::list<std::pair<std::string, const YAML::Node>>& output) {
    if(prefix.find_first_not_of("abcdefghijklmnopqrstuvwxyz._0123456789") != std::string::npos) {
        SYLAR_LOG_ERROR(SYLAR_LOG_ROOT()) << "Config invalid name: " << prefix << " : " << node;
        return;
    }
    output.push_back(std::make_pair(prefix, node));
    if(node.IsMap()) {
        for(auto it = node.begin();
            it != node.end(); ++it) {
            // 前缀为空，直接用子节点名字；前缀不为空，前缀和子节点名字之间用.连接
            ListAllMember((prefix.empty() ? it->first.Scalar() : prefix + "." + it->first.Scalar()), it->second, output);
        }
    }
}

// 从yaml节点加载配置项
void Config::LoadFromYaml(const YAML::Node& root) {
    std::list<std::pair<std::string, const YAML::Node>> all_nodes;  //  存放(配置名，YAML节点) 
    ListAllMember("", root, all_nodes);

    for(auto& i : all_nodes) {
        std::string key = i.first;
        if(key.empty()) {
            continue;
        }

        // 配置项名称转换为小写，保证配置项名称不区分大小写
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
        ConfigVarBase::ptr var = LookupBase(key);
        
        // 去全局配置 map 里找有没有这个名字的配置项，如果有就把yaml节点的值赋给这个配置项
        if(var) {
            // 如果yaml节点是标量，直接用Scalar()方法获取值并调用fromString()方法设置配置项的值
            if(i.second.IsScalar()) {
                var->fromString(i.second.Scalar());
            // 如果yaml节点不是标量，先把yaml节点转换为字符串，再调用fromString()方法设置配置项的值
            } else {
                std::stringstream ss;
                ss << i.second;
                var->fromString(ss.str());
            }
        }
    }
}

}