#include "fiber.h"
#include "config.h"
#include "macro.h"
#include "log.h"
#include "scheduler.h"
#include <atomic>

namespace sylar {
static Logger::ptr g_logger = SYLAR_LOG_NAME("system");

// 属于整个进程，std::atomic保证操作的原子性
static std::atomic<uint64_t> s_fiber_id {0};        // 全局原子变量协程id标识符
static std::atomic<uint64_t> s_fiber_count {0};     // 全局原子变量协程计数器

// 属于单个线程
static thread_local Fiber* t_fiber = nullptr;    //  当前线程正在执行的协程
static thread_local Fiber::ptr t_threadFiber = nullptr;  // 主协程

// 协程栈大小
static ConfigVar<uint32_t>::ptr g_fiber_stack_size =
    Config::Lookup<uint32_t>("fiber.stack_size", 1024 * 1024, "fiber stack size");


// 内存分配器
class MallocStackAllocator {
public:
    static void* Alloc(size_t size) {
        return malloc(size);
    }

    static void Dealloc(void* vp, size_t size) {
        // size 暂时没有使用，预留接口
        return free(vp);
    }
};

using StackAllocator = MallocStackAllocator;


// 获取当前协程id，用于打印日志
uint64_t Fiber::GetFiberId(){
    if(t_fiber) {
        return t_fiber->getId();
    }
    return 0;
}

// 主协程
Fiber::Fiber() {
    m_state = EXEC;
    SetThis(this);

    if(getcontext(&m_ctx)) {
        SYLAR_ASSERT2(false, "getcontext");
    }

    ++s_fiber_count;
    SYLAR_LOG_DEBUG(g_logger) << "Fiber::Fiber";
}

Fiber::Fiber(std::function<void()> cb, size_t stacksize, bool use_caller) : m_id(++s_fiber_id), m_cb(cb) {
    ++s_fiber_count;
    m_stacksize = stacksize ? stacksize : g_fiber_stack_size->getValue();  // 如果不为0，则使用传入的栈大小，否则使用配置文件中的栈大小

    m_stack = StackAllocator::Alloc(m_stacksize);
    if(getcontext(&m_ctx)) {
        SYLAR_ASSERT2(false, "getcontext");
    }
    m_ctx.uc_link = nullptr;   // 结束后不链接到其他上下文
    m_ctx.uc_stack.ss_sp = m_stack;
    m_ctx.uc_stack.ss_size = m_stacksize;

    if(!use_caller) {
        makecontext(&m_ctx, &Fiber::MainFunc, 0);
    } else {
        makecontext(&m_ctx, &Fiber::CallerMainFunc, 0);
    }

    SYLAR_LOG_DEBUG(g_logger) << "Fiber::Fiber id=" << m_id;
}

Fiber::~Fiber() {
    --s_fiber_count;
    // 如果不是主协程，则释放栈空间
    if(m_stack) {
        // 协程是INIT或者TERM才能被回收
        SYLAR_ASSERT(m_state == TERM || m_state == EXCEPT || m_state == INIT);  

        StackAllocator::Dealloc(m_stack, m_stacksize);
    } else {
        // 是主协程
        SYLAR_ASSERT(!m_cb);             // 主协程不能有回调函数
        SYLAR_ASSERT(m_state == EXEC);   // 主协程必须处于执行状态

        Fiber* cur = t_fiber;
        if(cur == this) {
            SetThis(nullptr);  // 设置当前协程为空
        }
    }
    SYLAR_LOG_DEBUG(g_logger) << "Fiber::~Fiber id=" << m_id;
}

// 重置协程函数，并重置状态
//INIT，TERM
void Fiber::reset(std::function<void()> cb) {
    // 如果当前协程不是INIT或者TERM状态或者不是主协程，则不能重置
    SYLAR_ASSERT(m_stack);
    SYLAR_ASSERT(m_state == TERM || m_state == EXCEPT || m_state == INIT);
    m_cb = cb;
    if(getcontext(&m_ctx)) {
        SYLAR_ASSERT2(false, "getcontext");
    }

    m_ctx.uc_link = nullptr;
    m_ctx.uc_stack.ss_sp = m_stack;
    m_ctx.uc_stack.ss_size = m_stacksize;

    makecontext(&m_ctx, &Fiber::MainFunc, 0);
    m_state = INIT;
}

void Fiber::call() {
    SetThis(this);
    m_state = EXEC;
    SYLAR_LOG_ERROR(g_logger) << getId();
    if(swapcontext(&t_threadFiber->m_ctx, &m_ctx)) {
        SYLAR_ASSERT2(false, "swapcontext");
    }
}

void Fiber::back() {
    SetThis(t_threadFiber.get());
    if(swapcontext(&m_ctx, &t_threadFiber->m_ctx)) {
        SYLAR_ASSERT2(false, "swapcontext");
    }
}

// 切换到当前协程执行
void Fiber::swapIn() {
    SetThis(this);
    SYLAR_ASSERT(m_state != EXEC);
    m_state = EXEC;
    if(swapcontext(&Scheduler::GetMainFiber()->m_ctx, &m_ctx)) {
        SYLAR_ASSERT2(false, "swapcontext");
    }
}

// 切换到后台执行
void Fiber::swapOut(){
    // if(t_fiber != Scheduler::GetMainFiber()){
        SetThis(Scheduler::GetMainFiber());
        if(swapcontext(&m_ctx, &Scheduler::GetMainFiber()->m_ctx)) {
            SYLAR_ASSERT2(false, "swapcontext");
        }
    // } else { 
        // SetThis(t_threadFiber.get());
        // if(swapcontext(&m_ctx, &t_threadFiber->m_ctx)) {
            // SYLAR_ASSERT2(false, "swapcontext");
        // }
    // }
}

// 设置当前协程
void Fiber::SetThis(Fiber* f){
    t_fiber = f;
}

// 返回当前协程, 如果还没有就初始化主协程
/* 因为主线程初始化时，就设置了t_fiber，如果这里t_fiber是空的，说明当前线程还没有初始化主协程 */
Fiber::ptr Fiber::GetThis(){
    if(t_fiber) {
        return t_fiber->shared_from_this();      // 返回当前协程的智能指针
    }
    Fiber::ptr main_fiber(new Fiber);
    SYLAR_ASSERT(t_fiber == main_fiber.get());  // 当前协程必须是主协程
    t_threadFiber = main_fiber;                  // 主协程的指针
    return t_fiber->shared_from_this();
}

// 协程切换到后台，并且设置为Ready状态
void Fiber::YieldToReady(){
    Fiber::ptr cur = Fiber::GetThis();
    cur->m_state = READY;
    cur->swapOut();
}

// 协程切换到后台，并且设置为Hold状态
void Fiber::YieldToHold(){
    Fiber::ptr cur = Fiber::GetThis();
    cur->m_state = HOLD;
    cur->swapOut();
}

// 总协程数
uint64_t Fiber::TotalFibers(){
    return s_fiber_count;
}

// 协程主函数
void Fiber::MainFunc(){
    Fiber::ptr cur = GetThis();
    SYLAR_ASSERT(cur);
    try {
        cur->m_cb();
        cur->m_cb = nullptr;
        cur->m_state = TERM;
    } catch (std::exception& ex) {
        cur->m_state = EXCEPT;
        SYLAR_LOG_ERROR(g_logger) << "Fiber Except: " << ex.what() << " fiber_id=" << cur->getId() << std::endl << sylar::BacktraceToString();
    } catch(...) {
        cur->m_state = EXCEPT;
        SYLAR_LOG_ERROR(g_logger) << "Fiber Except" << " fiber_id=" << cur->getId() << std::endl << sylar::BacktraceToString();
    }

    // cur->swapOut();  // 切换到主协程  有bug: 当跳转到主协程，处理完事情后，可能认为这个子协程已经结束了。但实际上，MainFunc 函数并没有结束，只是被挂起了。cur的引用计数依然为1，这个子协程无法销毁，导致内存泄漏。
        /*  使用 raw_ptr 是为了在调用 swapOut 之前，显式地减少当前栈帧对 Fiber 对象的引用计数。cur 是一个智能指针（shared_ptr），它在 MainFunc 函数栈上持有了当前协程对象的引用。
            如果不先释放它就直接调用 swapOut，会导致引用计数无法归零，从而引发内存泄漏，或者在特定场景下导致对象无法被正确复用/销毁。*/
    auto raw_ptr = cur.get();
    cur.reset();  // 释放当前协程的智能指针
    raw_ptr->swapOut();
    SYLAR_ASSERT2(false, "never reach fiber_id= " + std::to_string(raw_ptr->getId()));
}

void Fiber::CallerMainFunc(){
    Fiber::ptr cur = GetThis();   // 调度器本身有一个持有一个指向子协程的引用计数=1，这里cur再次增加了引用计数=2
    SYLAR_ASSERT(cur);
    try {
        cur->m_cb();
        cur->m_cb = nullptr;
        cur->m_state = TERM;
    } catch (std::exception& ex) {
        cur->m_state = EXCEPT;
        SYLAR_LOG_ERROR(g_logger) << "Fiber Except: " << ex.what() << " fiber_id=" << cur->getId() << std::endl << sylar::BacktraceToString();
    } catch(...) {
        cur->m_state = EXCEPT;
        SYLAR_LOG_ERROR(g_logger) << "Fiber Except" << " fiber_id=" << cur->getId() << std::endl << sylar::BacktraceToString();
    }
    auto raw_ptr = cur.get();
    cur.reset();       // 释放当前协程的智能指针, 先显式地减少当前栈帧对 Fiber 对象的引用计数，2-1=1
    raw_ptr->back();   // 再用裸指针返回到调度器协程，调度器发现任务做完了，移除的时候，此时引用计数数为1，释放智能指针可以正常被销毁
    SYLAR_ASSERT2(false, "never reach fiber_id= " + std::to_string(raw_ptr->getId()));
}

}