#include "sylar/sylar.h"
#include <unistd.h>

sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

int count = 0;
// sylar::RWMutex s_mutex;
sylar::Mutex s_mutex;
void fun1() {
    SYLAR_LOG_INFO(g_logger) << "name: " << sylar::Thread::GetName()
                             << " this.name: " << sylar::Thread::GetThis()->getName()
                             << " id: " << sylar::GetThreadId()
                             << " this.id: " << sylar::Thread::GetThis()->getId();
        for(int i = 0; i < 100000; ++i) {
            // sylar::RWMutex::WriteLock lock(s_mutex);  // 写独占，读互斥，安全， 打印符合预期
            // sylar::RWMutex::ReadLock lock(s_mutex);   // 读共享，写互斥，不安全，打印会不符合预期

            sylar::Mutex::Lock lock(s_mutex);  // 写独占，读互斥，安全， 打印符合预期
            ++count;
        }
}

void fun2() {
    for(int i = 0; i < 100; i++) {
        SYLAR_LOG_INFO(g_logger) << "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
    }
}

void fun3(){
    for(int i = 0; i < 100; i++) {
        SYLAR_LOG_INFO(g_logger) << "=============================================";
    }
}

void test1(){
    SYLAR_LOG_INFO(g_logger) << "thread test begin";
    std::vector<sylar::Thread::ptr> threads;
    for(size_t i = 0; i < 5; ++i) {
        sylar::Thread::ptr thread(new sylar::Thread(&fun1, "name_" + std::to_string(i)));
        threads.push_back(thread);
    }

    for(size_t i = 0; i < 5; ++i) {
        threads[i]->join();
    }
    SYLAR_LOG_INFO(g_logger) << "thread test end";
    SYLAR_LOG_INFO(g_logger) << "count= " << count;
    return;
}


void test2_3(){
    YAML::Node root = YAML::LoadFile("../bin/conf/test2.yml");
    sylar::Config::LoadFromYaml(root);

    SYLAR_LOG_INFO(g_logger) << "thread test begin";
    std::vector<sylar::Thread::ptr> threads;
    for(size_t i = 0; i < 2; ++i) {
        sylar::Thread::ptr thread1(new sylar::Thread(&fun2, "name_" + std::to_string(i * 2)));
        sylar::Thread::ptr thread2(new sylar::Thread(&fun3, "name_" + std::to_string(i * 2 + 1)));
        
        threads.push_back(thread1);
        threads.push_back(thread2);
    }

    for(size_t i = 0; i < threads.size(); ++i) {
        threads[i]->join();
    }
    SYLAR_LOG_INFO(g_logger) << "thread test end";

    // 测试visit函数 打印配置信息 
    sylar::Config::Visit([](sylar::ConfigVarBase::ptr var){
        SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << "name=" << var->getName() << " description=" << var->getDescription() << " typename=" << var->getTypeName() << " value=" << var->toString();
    });

    return;
}


int main(int argc, char** argv) {
    // test1();
    test2_3();
    
    return 0;
}