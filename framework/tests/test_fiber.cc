#include "sylar/sylar.h"
// #include <vector>

sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

void run_in_fiber() {
    SYLAR_LOG_INFO(g_logger) << "run_in_fiber begin";
    sylar::Fiber::YieldToHold();   // 切换到主线程的Fiber
    SYLAR_LOG_INFO(g_logger) << "run_in_fiber end";
    sylar::Fiber::YieldToHold();   // 从主线程的Fiber切换回来
}


void test_fiber() {
    SYLAR_LOG_INFO(g_logger) << "main begin -1";
    {
        sylar::Fiber::GetThis();   // 初始化主线程的Fiber
        SYLAR_LOG_INFO(g_logger) << "main begin";
        sylar::Fiber::ptr fiber(new sylar::Fiber(run_in_fiber));
        fiber->swapIn();     // 切换到子线程的Fiber执行
        SYLAR_LOG_INFO(g_logger) << "main after swapIn";
        fiber->swapIn();     // 切换回主线程的Fiber执行
        SYLAR_LOG_INFO(g_logger) << "main after end";
        fiber->swapIn();     // 切换回子线程的Fiber执行
    }
    SYLAR_LOG_INFO(g_logger) << "main after end2";

}
int main(int argc, char** argv) {
    sylar::Thread::SetName("main");

    std::vector<sylar::Thread::ptr> threads;
    for (int i = 0 ; i < 3; ++i) {
        threads.push_back(sylar::Thread::ptr(new sylar::Thread(&test_fiber, "name_" + std::to_string(i))));
    }
    for(auto i : threads){
        i->join();
    }
    return 0;
}