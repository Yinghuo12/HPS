#ifndef __SYLAR_CONFIG_H__
#define __SYLAR_CONFIG_H__

#include <memory>
#include <string>
#include <sstream>
#include <boost/lexical_cast.hpp>
#include <yaml-cpp/yaml.h>
#include <vector>
#include <list>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <functional>

#include "thread.h"
#include "log.h"


namespace sylar {

// 配置项基类
class ConfigVarBase {
public:
    typedef std::shared_ptr<ConfigVarBase> ptr;
    ConfigVarBase(const std::string& name, const std::string& description = "") : m_name(name), m_description(description) {
        std::transform(m_name.begin(), m_name.end(), m_name.begin(), ::tolower);          // 配置项名称转换为小写
    }
    virtual ~ConfigVarBase() {}

    const std::string& getName() const { return m_name; }
    const std::string& getDescription() const { return m_description; }

    virtual std::string toString() = 0;
    virtual bool fromString(const std::string& val) = 0;
    virtual std::string getTypeName() = 0;       // 获取配置项类型名称
protected:
    std::string m_name;
    std::string m_description;
};


// 类型转换模板类，封装了boost::lexical_cast，实现从类型 F 到类型 T 的词法转换
template<class F, class T>
class LexicalCast {
public:
    T operator()(const F& v) {
        return boost::lexical_cast<T>(v);
    }
};





/***  偏特化版本, 支持STL容器类型转换： 容器<T> 和 容器<string> 之间的转换 ***/
// 1. vector
template<class T>
class LexicalCast<std::string, std::vector<T> > {
public:
    std::vector<T> operator()(const std::string& v) {
        YAML::Node node = YAML::Load(v);
        typename std::vector<T> vec;
        std::stringstream ss;
        for(size_t i = 0; i < node.size(); ++i) {
            ss.str("");
            ss << node[i];
            vec.push_back(LexicalCast<std::string, T>()(ss.str()));
        }
        return vec;
    }
};

template<class T>
class LexicalCast<std::vector<T>, std::string> {
public:
    std::string operator()(const std::vector<T>& v) {
        YAML::Node node;
        for(const auto& i : v) {
            node.push_back(YAML::Load(LexicalCast<T, std::string>()(i)));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};



// 2. list
template<class T>
class LexicalCast<std::string, std::list<T> > {
public:
    std::list<T> operator()(const std::string& v) {
        YAML::Node node = YAML::Load(v);
        typename std::list<T> vec;
        std::stringstream ss;   
        for(size_t i = 0; i < node.size(); ++i) {
            ss.str("");
            ss << node[i];
            vec.push_back(LexicalCast<std::string, T>()(ss.str()));
        }
        return vec;
    }
};


template<class T>
class LexicalCast<std::list<T>, std::string> {
public:
    std::string operator()(const std::list<T>& v) {
        YAML::Node node;
        for(const auto& i : v) {
            node.push_back(YAML::Load(LexicalCast<T, std::string>()(i)));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};


// 3. set
template<class T>
class LexicalCast<std::string, std::set<T> > {
public:
    std::set<T> operator()(const std::string& v) {
        YAML::Node node = YAML::Load(v);
        typename std::set<T> vec;
        std::stringstream ss;   
        for(size_t i = 0; i < node.size(); ++i) {
            ss.str("");
            ss << node[i];
            vec.insert(LexicalCast<std::string, T>()(ss.str()));
        }
        return vec;
    }
};


template<class T>
class LexicalCast<std::set<T>, std::string> {
public:
    std::string operator()(const std::set<T>& v) {
        YAML::Node node;
        for(const auto& i : v) {
            node.push_back(YAML::Load(LexicalCast<T, std::string>()(i)));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

// 3. unordered_set
template<class T>
class LexicalCast<std::string, std::unordered_set<T> > {
public:
    std::unordered_set<T> operator()(const std::string& v) {
        YAML::Node node = YAML::Load(v);
        typename std::unordered_set<T> vec;
        std::stringstream ss;   
        for(size_t i = 0; i < node.size(); ++i) {
            ss.str("");
            ss << node[i];
            vec.insert(LexicalCast<std::string, T>()(ss.str()));
        }
        return vec;
    }
};


template<class T>
class LexicalCast<std::unordered_set<T>, std::string> {
public:
    std::string operator()(const std::unordered_set<T>& v) {
        YAML::Node node;
        for(const auto& i : v) {
            node.push_back(YAML::Load(LexicalCast<T, std::string>()(i)));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};


// 4. map
template<class T>
class LexicalCast<std::string, std::map<std::string, T> > {
public:
    std::map<std::string, T> operator()(const std::string& v) {
        YAML::Node node = YAML::Load(v);
        typename std::map<std::string, T> map;
        std::stringstream ss;
        for(auto it = node.begin();
            it != node.end(); ++it) {
            ss.str("");
            ss << it->second;
            map.insert(std::make_pair(it->first.Scalar(),
                LexicalCast<std::string, T>()(ss.str())));
        }
        return map;
    }
};

template<class T>
class LexicalCast<std::map<std::string, T>, std::string> {
public:
    std::string operator()(const std::map<std::string, T>& v) {
        YAML::Node node;
        for(auto& i : v) {
            node[i.first] = YAML::Load(LexicalCast<T, std::string>()(i.second));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};


// 5. unordered_map
template<class T>
class LexicalCast<std::string, std::unordered_map<std::string, T> > {
public:
    std::unordered_map<std::string, T> operator()(const std::string& v) {
        YAML::Node node = YAML::Load(v);
        typename std::unordered_map<std::string, T> map;
        std::stringstream ss;
        for(auto it = node.begin();
            it != node.end(); ++it) {
            ss.str("");
            ss << it->second;
            map.insert(std::make_pair(it->first.Scalar(),
                LexicalCast<std::string, T>()(ss.str())));
        }
        return map;
    }
};

template<class T>
class LexicalCast<std::unordered_map<std::string, T>, std::string> {
public:
    std::string operator()(const std::unordered_map<std::string, T>& v) {
        YAML::Node node;
        for(auto& i : v) {
            node[i.first] = YAML::Load(LexicalCast<T, std::string>()(i.second));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};


/***  偏特化版本, 支持STL容器类型转换  end ***/



// 模板化配置项类
// FromStr: T operator()(const std::string&)  // 从字符串转换为T类型
// ToStr: std::string operator()(const T&)    // 从T类型转换
template<class T, class FromStr = LexicalCast<std::string, T>, class ToStr = LexicalCast<T, std::string> >  // 特化版本，提供默认的类型转换器
class ConfigVar : public ConfigVarBase {
public:
    typedef RWMutex RWMutexType;
    typedef std::shared_ptr<ConfigVar> ptr;
    typedef std::function<void (const T& old_value, const T& new_value)> on_change_cb;  // 配置项变更回调函数类型，参数是旧值和新值

    ConfigVar(const std::string& name, const T& default_value, const std::string& description = "") : ConfigVarBase(name, description), m_val(default_value) { }

    // 将配置项的值转换为字符串，方便保存到文件中
    std::string toString() override {
        try {
            RWMutexType::ReadLock lock(m_mutex);
            // return boost::lexical_cast<std::string>(m_val);  // T转换为字符串
            return ToStr()(m_val);   // T类型转换为自定义类型
        } catch (std::exception& e) {
            SYLAR_LOG_ERROR(SYLAR_LOG_ROOT()) << "ConfigVar::toString exception" << e.what() << " convert: " << typeid(m_val).name() << " to string";
        }
        return "";
    }

    // 从字符串中解析出配置项的值，方便从文件中加载配置项
    bool fromString(const std::string& val) override {
        try {
            // m_val = boost::lexical_cast<T>(val);  // 字符串转换为T类型
            setValue(FromStr()(val));   // 自定义类型转换为T类型
        } catch (std::exception& e) {
            SYLAR_LOG_ERROR(SYLAR_LOG_ROOT()) << "ConfigVar::toString exception" << e.what() << " convert: string to " << typeid(m_val).name() << " - " << val;
        }
        return false;
    }
    
    const T getValue() const {
        RWMutexType::ReadLock lock(m_mutex);
        return m_val; 
    }
    
    void setValue(const T& v) { 
        {
            // 读
            RWMutexType::ReadLock lock(m_mutex);
            if(v == m_val) {  // 如果新值和旧值相同，则不更新配置项的值，也不调用回调函数
                return;
            }
            // 调用配置项变更回调函数
            for(auto& i : m_cbs) {
                i.second(m_val, v);  // 传入旧值和新值
            }
        }
        // 写
        RWMutexType::WriteLock lock(m_mutex);
        m_val = v;    // 最后更新配置项的值
    }

    std::string getTypeName() override { return typeid(T).name(); } // 返回配置项的类型名

    // 配置项变更回调函数的管理，添加、删除、获取、清空回调函数
    uint64_t addListener(on_change_cb cb) { 
        // 标识回调函数id
        static uint64_t s_fun_id = 0;
        RWMutexType::WriteLock lock(m_mutex);
        ++s_fun_id;
        m_cbs[s_fun_id] = cb;
        return s_fun_id; 
    }

    void delListener(uint64_t key) { 
        RWMutexType::WriteLock lock(m_mutex);
        m_cbs.erase(key); 
    }

    on_change_cb getListener(uint64_t key) {
        RWMutexType::ReadLock lock(m_mutex);
        auto it = m_cbs.find(key);
        return it == m_cbs.end() ? nullptr : it->second;
    }
    void clearListener() {
        RWMutexType::ReadLock lock(m_mutex);
        m_cbs.clear(); 
    }


private:
    T m_val;
    std::map<uint64_t, on_change_cb> m_cbs;  // 配置项变更回调函数列表
    mutable RWMutexType m_mutex;  // 锁要在const成员函数里被修改
};


// 配置管理类
class Config {
public:
    typedef std::map<std::string, ConfigVarBase::ptr> ConfigVarMap;
    typedef RWMutex RWMutexType;

    // 下面实现了两个版本
    // 版本一：配置项查找并创建，返回配置项的智能指针 调用了版本二的重载函数
    template<class T>
    static typename ConfigVar<T>::ptr Lookup(const std::string& name, const T& default_value, const std::string& description = "") {  // 为什么要加typename？因为ConfigVar<T>::ptr是一个依赖于模板参数T的类型，编译器在解析时无法确定它是否是一个类型，所以需要加typename告诉编译器这是一个类型。
        // 1. 查找配置项
        // 优化：检测同名不同类型的配置项，输出错误日志
        RWMutexType::WriteLock lock(GetMutex());
        auto it = GetDatas().find(name);
        if (it != GetDatas().end()) {
            auto tmp = std::dynamic_pointer_cast<ConfigVar<T> >(it->second);
            if (tmp) {
                SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << "Lookup name=" << name << " exists";
                return tmp;
            } else {
                SYLAR_LOG_ERROR(SYLAR_LOG_ROOT()) << "Lookup name=" << name << " exists but type not " << typeid(T).name() << " real_type is " << it->second->getTypeName() << " " << it->second->toString();
                return nullptr;
            }
        }
        // 找不到就直接返回 nullptr，不知道是名字不存在还是类型不对
        // auto tmp = Lookup<T>(name);
        // if (tmp) {
        //     SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << "Lookup name=" << name << " exists";
        //     return tmp;
        // }

        // 2. 配置项合法性检查
        if (name.find_first_not_of("abcdefghijklmnopqrstuvwxyz._0123456789") != std::string::npos) {
            SYLAR_LOG_ERROR(SYLAR_LOG_ROOT()) << "Lookup name invalid " << name;
            throw std::invalid_argument(name);
        }

        // 3. 创建新的配置项
        typename ConfigVar<T>::ptr v(new ConfigVar<T>(name, default_value, description));
        GetDatas()[name] = v;
        return v;
    }

    // 版本二：配置项查找，返回nullptr表示没有找到
    template<class T>
    static typename ConfigVar<T>::ptr Lookup(const std::string& name) {
        RWMutexType::ReadLock lock(GetMutex());
        auto it = GetDatas().find(name);
        if (it == GetDatas().end()) {
            return nullptr;
        }
        // map 里存的是 基类指针 ConfigVarBase  但实际里面都是 子类 ConfigVar<T>
        return std::dynamic_pointer_cast<ConfigVar<T>>(it->second);
    }

    static void LoadFromYaml(const YAML::Node& root);  // 从yaml节点加载配置项
    static ConfigVarBase::ptr LookupBase(const std::string& name);  // 查找配置项基类指针，返回nullptr表示没有找到

    static void Visit(std::function<void(ConfigVarBase::ptr)> cb);   // 遍历配置项，调用回调函数

private:

private:
    // static ConfigVarMap s_datas;  // 存储所有配置项的map，key为配置项名称，value为配置项对象的智能指针
    // 修改bug: 全局变量初始化顺序不确定，项目中大量使用全局配置变量，s_datas可能在还没创建时就去使用它，导致程序崩溃。采用函数内静态变量，第一次调用函数时才初始化
    static ConfigVarMap& GetDatas() {
        static ConfigVarMap s_datas;
        return s_datas;
    }

    // 用静态函数返回静态锁  如果采用定义全局锁变量的方法 则会 初始化顺序崩溃 （配置表可能在锁初始化之前就被使用）
    static RWMutexType& GetMutex(){
        static RWMutexType s_mutex;
        return s_mutex;
    }
};

}


#endif