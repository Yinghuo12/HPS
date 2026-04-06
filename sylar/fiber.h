#ifndef __SYLAR_FIBER_H__
#define __SYLAR_FIBER_H__

#include <memory>
#include <functional>
#include <ucontext.h>
#include "thread.h"

namespace sylar {

class Scheduler;
class Fiber : public std::enable_shared_from_this<Fiber> {
friend class Scheduler;
public:
    typedef std::shared_ptr<Fiber> ptr;
    
    // 协程状态
    enum State {
        INIT,  // 初始化
        HOLD,  // 挂起
        EXEC,  // 执行中
        TERM,  // 结束
        READY, // 就绪
        EXCEPT  // 异常
    };
private:
    Fiber();

public:
    Fiber(std::function<void()> cb, size_t stacksize = 0, bool use_caller = false);
    ~Fiber();

    void reset(std::function<void()> cb);  // 重置协程函数，并重置状态 //INIT，TERM
    void swapIn();      // 切换到当前协程执行
    void swapOut();     // 切换到后台执行
    void call();        // 强行把当前协程切换到前台执行
    void back();        // 强行把当前协程切换到后台执行
    uint64_t getId() const { return m_id; }   // 获取协程id
    State getState() const { return m_state; } // 获取协程状态

public:
    // 静态方法
    static void SetThis(Fiber* f);   // 设置当前协程
    static Fiber::ptr GetThis();     // 返回当前协程
    static void YieldToReady();      // 协程切换到后台，并且设置为Ready状态
    static void YieldToHold();       // 协程切换到后台，并且设置为Hold状态
    static uint64_t TotalFibers();   // 总协程数
    static void MainFunc();          // 协程函数
    static void CallerMainFunc();    // 调用者协程函数
    static uint64_t GetFiberId();    // 获取协程id

private:
    uint64_t m_id = 0;
    uint32_t m_stacksize = 0;
    State m_state = INIT;

    ucontext_t m_ctx;
    void* m_stack = nullptr;

    std::function<void()> m_cb;
};

}

#endif