#include "sylar/config.h"
#include "sylar/log.h"
#include <yaml-cpp/yaml.h>

// 注册全局配置项
sylar::ConfigVar<int>::ptr g_int_value_config = sylar::Config::Lookup("system.port", (int)8080, "system port");
// sylar::ConfigVar<float>::ptr g_int_value2_config = sylar::Config::Lookup("system.port", (float)8080, "system port");  // 同名配置项不同类型，输出错误日志

sylar::ConfigVar<float>::ptr g_float_value_config = sylar::Config::Lookup("system.value", (float)10.2f, "system value");
sylar::ConfigVar<std::vector<int>>::ptr g_int_vec_value_config = sylar::Config::Lookup<std::vector<int>>("system.int_vec", std::vector<int>{1, 2}, "system int vec");  // vector
sylar::ConfigVar<std::list<int>>::ptr g_int_list_value_config = sylar::Config::Lookup<std::list<int>>("system.int_list", std::list<int>{1, 2}, "system int list");  // list
sylar::ConfigVar<std::set<int>>::ptr g_int_set_value_config = sylar::Config::Lookup<std::set<int>>("system.int_set", std::set<int>{1, 2}, "system int set");  // set
sylar::ConfigVar<std::unordered_set<int>>::ptr g_int_unordered_set_value_config = sylar::Config::Lookup<std::unordered_set<int>>("system.int_unordered_set", std::unordered_set<int>{1, 2}, "system int unordered set");  // unordered_set
sylar::ConfigVar<std::map<std::string, int>>::ptr g_str_int_map_value_config = sylar::Config::Lookup<std::map<std::string, int>>("system.str_int_map", std::map<std::string, int>{{"key1", 1}, {"key2", 2}}, "system str int map");  // map
sylar::ConfigVar<std::unordered_map<std::string, int>>::ptr g_str_int_unordered_map_value_config = sylar::Config::Lookup<std::unordered_map<std::string, int>>("system.str_int_unordered_map", std::unordered_map<std::string, int>{{"key1", 1}, {"key2", 2}}, "system str int unordered map");  // unordered_map

// 解析yaml文件，输出yaml节点的类型和值
void print_yaml(const YAML::Node &node, int level) {
    // 处理普通值
    if(node.IsScalar()) {
        SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << std::string(level * 4, ' ')
            << node.Scalar() << " - " << node.Type() << " - " << level;
    // 处理空值
    } else if(node.IsNull()) {
        SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << std::string(level * 4, ' ')
            << "NULL - " << node.Type() << " - " << level;
    // 处理map(键值对 / 对象)
    } else if(node.IsMap()) {
        for(auto it = node.begin();
            it != node.end(); ++it) {
            SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << std::string(level * 4, ' ')
                << it->first << " - " << it->second.Type() << " - " << level;
            print_yaml(it->second, level + 1);
        }
    // 处理sequence(列表 / 数组)
    } else if(node.IsSequence()) {
        for(size_t i = 0; i < node.size(); ++i) {
            SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << std::string(level * 4, ' ')
                << i << " - " << node[i].Type() << " - " << level;
            print_yaml(node[i], level + 1);
        }
    }
}


void test_yaml(){
    YAML::Node root = YAML::LoadFile("../bin/conf/log.yml");
    print_yaml(root, 0);
    // SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << root;
}

void test_config(){
    SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << "before: " << g_int_value_config->getValue();  // 通过getValue()方法获取配置项的值，输出8080
    SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << "before: " << g_float_value_config->toString();  // 通过toString()方法将配置项的值转换为字符串，输出10.2


// 宏测试 顺序存储容器
#define XX(g_var, name, prefix) \
    { \
        auto &v = g_var->getValue();  \
        for(auto &i : v) { \
            SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << #prefix " " #name ": " << i; \
        } \
        SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << #prefix " " #name " yaml: " << g_var->toString(); \
    }

// 宏测试 无序存储容器
#define XX_M(g_var, name, prefix) \
    { \
        auto &v = g_var->getValue();  \
        for(auto &i : v) { \
            SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << #prefix " " #name ": {" << i.first << " - " << i.second << "}"; \
        } \
        SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << #prefix " " #name " yaml: " << g_var->toString(); \
    }


    XX(g_int_vec_value_config, int_vec, before);
    XX(g_int_list_value_config, int_list, before);
    XX(g_int_set_value_config, int_set, before);
    XX(g_int_unordered_set_value_config, int_unordered_set, before);
    XX_M(g_str_int_map_value_config, str_int_map, before);
    XX_M(g_str_int_unordered_map_value_config, str_int_unordered_map, before);

    YAML::Node root = YAML::LoadFile("../bin/conf/log.yml");
    sylar::Config::LoadFromYaml(root);  // 从yaml节点加载配置项
    
    SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << "after: " << g_int_value_config->getValue();  // 通过getValue()方法获取配置项的值，输出yaml文件中配置的9900
    SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << "after: " << g_float_value_config->toString();  // 通过toString()方法将配置项的值转换为字符串，输出yaml文件中配置的15

    XX(g_int_vec_value_config, int_vec, after);
    XX(g_int_list_value_config, int_list, after);
    XX(g_int_set_value_config, int_set, after);
    XX(g_int_unordered_set_value_config, int_unordered_set, after);
    XX_M(g_str_int_map_value_config, str_int_map, after);
    XX_M(g_str_int_unordered_map_value_config, str_int_unordered_map, after);
}



// 自定义类 的配置项
class Person {
public:
    Person() {}

    std::string m_name = "";
    int m_age = 0;
    bool m_sex = 0;

    std::string toString() const {
        std::stringstream ss;
        ss << "[Person name= " << m_name << " age= " << m_age << " sex= " << m_sex << "]";
        return ss.str();
    }

    bool operator==(const Person& other) const {
        return m_name == other.m_name && m_age == other.m_age && m_sex == other.m_sex;
    }
};

// 自定义类的序列化和反序列化
namespace sylar {
template<>
class LexicalCast<std::string, Person> {
public:
    Person operator()(const std::string& v) {
        YAML::Node node = YAML::Load(v);
        Person p;
        // as<Type>()(yaml.cpp) 等价于 boost::lexical_cast
        p.m_name = node["name"].as<std::string>();   
        p.m_age = node["age"].as<int>();
        p.m_sex = node["sex"].as<bool>();
        return p;
    }
};

// 全特化：Person → std::string
template<>
class LexicalCast<Person, std::string> {
public:
    std::string operator()(const Person& p) {
        YAML::Node node;
        node["name"] = p.m_name;
        node["age"] = p.m_age;
        node["sex"] = p.m_sex;
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

}


// 测试自定义类的配置项，输出yaml文件中配置的Person对象的值
sylar::ConfigVar<Person>::ptr g_person = sylar::Config::Lookup<Person>("class.person", Person(), "system person");  // 自定义类的配置项
sylar::ConfigVar<std::map<std::string, Person>>::ptr g_person_map = sylar::Config::Lookup<std::map<std::string, Person>>("class.map", std::map<std::string, Person>(), "system person");  // map类型的自定义类配置项
sylar::ConfigVar<std::map<std::string, std::vector<Person> > >::ptr g_person_vec_map = sylar::Config::Lookup<std::map<std::string, std::vector<Person> > >("class.vec_map", std::map<std::string, std::vector<Person> >(), "system person");  // map类型的自定义类配置项

void test_class() {
// 宏测试 map类型的自定义类配置项
#define XX_PM(g_var, prefix) \
    { \
        auto m = g_person_map->getValue(); \
        for(auto& i : m) { \
            SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << prefix << ": " << i.first << " - " << i.second.toString(); \
        } \
        SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << prefix << ": size=" << m.size(); \
    } \

    // 测试配置项变更回调函数 第一个参数是优先级 优先级数字越小，监听器越先执行  10：普通日志   20: 业务级   30:系统级
    g_person->addListener(10, [](const Person& old_value, const Person& new_value){ 
        SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << "old_value=" << old_value.toString() << " new_value=" << new_value.toString();  // 触发回调函数，输出旧值和新值
    });

    SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << "before: " << g_person->getValue().toString() << " - " << g_person->toString();
    XX_PM(g_person_map, "class.map before");
    SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << "before: " << g_person_vec_map->toString();
    

    YAML::Node root = YAML::LoadFile("../bin/conf/log.yml");
    sylar::Config::LoadFromYaml(root);

    SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << "after: " << g_person->getValue().toString() << " - " << g_person->toString();
    XX_PM(g_person_map, "class.map after");
    SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << "after: " << g_person_vec_map->toString();


}


int main(int argc, char** argv) {
    
    // test_yaml();
    // test_config();
    test_class();

    return 0;
}