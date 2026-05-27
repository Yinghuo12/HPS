#include "sylar/sylar.h"
#include "sylar/iomanager.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <sys/epoll.h>

sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();


/* timer 是 test_timer () 函数内的局部智能指针
    如果用了 [&timer]() 按引用捕获
    test_timer() 执行完，局部变量 timer 被销毁
    定时器还在后台运行，回调执行时访问悬空引用
    直接段错误 Segmentation fault
    这是 C++ 异步 / 定时器场景里最经典的悬空引用 bug
*/ 

// 不要用引用捕获，改用值捕获 + 智能指针值传递
sylar::Timer::ptr s_timer;

void test_timer(){
    sylar::IOManager iom(2); // 创建一个IOManager，线程池大小为2
    
    s_timer = iom.addTimer(1000, [](){  // 添加一个定时器，间隔1000ms(1秒)
        static int i = 0;
        SYLAR_LOG_INFO(g_logger) << "hello timer i=" << i;
        // 执行次数++
        if(++i == 3){
            s_timer->reset(2000, true);   // 重置定时器，间隔2000ms(2秒)
            // s_timer->cancel();              // 取消定时器
        }
    }, true); // true表示是周期性定时器
}
int main(int argc, char** argv) {
    test_timer();
    return 0;
}