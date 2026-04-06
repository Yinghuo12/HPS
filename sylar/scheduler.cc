#include "scheduler.h"
#include "log.h"
#include "macro.h"

namespace sylar {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

static thread_local Scheduler* t_scheduler = nullptr;    // 当前线程的Scheduler
static thread_local Fiber* t_fiber = nullptr;            // 当前线程的主Fiber

Scheduler::Scheduler(size_t threads, bool use_caller, const std::string& name) :m_name(name) {
    SYLAR_ASSERT(threads > 0);

    // 如果使用调用者线程（让主线程也参与调度）
    if(use_caller) {
        sylar::Fiber::GetThis();  // 如果线程没有Fiber，则创建一个主Fiber, 确保当前线程有一个主协程
        --threads;                // 调用者线程也算一个工作线程

        SYLAR_ASSERT(GetThis() == nullptr);
        t_scheduler = this;       // 当前线程绑定这个调度器

        // 创建根协程，执行调度器的run方法
        m_rootFiber.reset(new Fiber(std::bind(&Scheduler::run, this), 0 , true));
        sylar::Thread::SetName(m_name);

        t_fiber = m_rootFiber.get();            // 当前线程主协程 = 根协程
        m_rootThread = sylar::GetThreadId();    // 根线程id = 当前线程id
        m_threadIds.push_back(m_rootThread);
    } else {
        m_rootThread = -1;
    }
    m_threadCount = threads;
}

Scheduler::~Scheduler() {
    SYLAR_ASSERT(m_stopping);
    if(GetThis() == this) {
        t_scheduler = nullptr;
    }
}

Scheduler* Scheduler::GetThis() {
    return t_scheduler;
}

Fiber* Scheduler::GetMainFiber() {
    return t_fiber;
}

void Scheduler::start() {
    MutexType::Lock lock(m_mutex);

    // 避免重复启动
    if(!m_stopping) {
        return;
    }
    m_stopping = false;    // 调度器开始运行
    SYLAR_ASSERT(m_threads.empty());

    m_threads.resize(m_threadCount);
    for(size_t i = 0; i < m_threadCount; ++i) {
        m_threads[i].reset(new Thread(std::bind(&Scheduler::run, this), m_name + "_" + std::to_string(i)));
        m_threadIds.push_back(m_threads[i]->getId());
    }
    lock.unlock();

    // if(m_rootFiber) {
    //     // m_rootFiber->swapIn();
    //     m_rootFiber->call();
    //     SYLAR_LOG_INFO(g_logger) << "call out " << m_rootFiber->getState();
    // }
}

void Scheduler::stop() {
    m_autoStop = true;
    // 当只有一个线程，且这个线程里面正在执行的协程处于结束或者初始化状态时，则停止调度器
    if(m_rootFiber && m_threadCount == 0 && (m_rootFiber->getState() == Fiber::TERM || m_rootFiber->getState() == Fiber::INIT)) {
        SYLAR_LOG_INFO(g_logger) << this << " stopped";
        m_stopping = true;

        // 防止重复停止
        if(stopping()) {
            return;
        }
    }

    //bool exit_on_this_fiber = false;
    if(m_rootThread != -1) {
        SYLAR_ASSERT(GetThis() == this);
    } else {
        SYLAR_ASSERT(GetThis() != this);
    }

    m_stopping = true;

    // 唤醒所有线程，让它们退出循环
    for(size_t i = 0; i < m_threadCount; ++i) {
        tickle();
    }

    if(m_rootFiber) {
        tickle();
    }

    // 让主线程在此处通过主协程参与调度任务
    if(m_rootFiber){
        // while(!stopping()){
        //     if(m_rootFiber->getState() == Fiber::TERM || m_rootFiber->getState() == Fiber::EXCEPT) {
        //         m_rootFiber.reset(new Fiber(std::bind(&Scheduler::run, this), 0, true));
        //         SYLAR_LOG_INFO(g_logger) << "root fiber is term, reset";
        //         t_fiber = m_rootFiber.get();   // 将主协程设置为当前协程
        //     }
        //     m_rootFiber->call();
        // }
        if(!stopping()){
            m_rootFiber->call();
        }
    }

    std::vector<Thread::ptr> thrs;
    {
        MutexType::Lock lock(m_mutex);
        thrs.swap(m_threads);
    }

    for(auto& i : thrs) {
        i->join();
    }
    //if(exit_on_this_fiber) {
    //}
}

void Scheduler::setThis() {
    t_scheduler = this;
}

void Scheduler::run() {
    SYLAR_LOG_INFO(g_logger) << "run";
    setThis();      // 把当前线程绑定到这个调度器

    // 不是主线程，则创建一个主协程
    if(sylar::GetThreadId() != m_rootThread) {
        t_fiber = Fiber::GetThis().get();
    }

    Fiber::ptr idle_fiber(new Fiber(std::bind(&Scheduler::idle, this)));   // 创建一个空闲协程
    Fiber::ptr cb_fiber;                                                   // 创建一个回调协程
    FiberAndThread ft;                                                     // 一个任务，可以是协程或回调函数


    while(true) {
        ft.reset();               // 重置任务
        bool tickle_me = false;   // 是否需要唤醒其他线程
        bool is_active = false;   // 当前线程是否正在执行任务
 
        {
            MutexType::Lock lock(m_mutex);
            auto it = m_fibers.begin();
           
            // 遍历任务队列，找到能在当前线程执行的任务，找到则跳出循环
            while(it != m_fibers.end()) {
                // 1. 遍历任务
                // 判断任务是不是属于当前线程，如果不是，则跳过这个任务，让其他线程执行
                // 如果任务线程id为-1，则表示任务在所有线程中都可以执行，直接跳过这段； 如果不是-1并且任务线程id与当前线程id不同，则需要唤醒其他线程
                if(it->thread != -1 && it->thread != sylar::GetThreadId()) {
                    ++it;
                    tickle_me = true;    // 有任务，但不是当前线程的，需要唤醒其他线程，继续找到属于当前线程的任务
                    continue;
                }

                // 2. 执行任务
                // 跳出上面if则说明找到了属于当前线程的任务，下面需要判断任务是不是协程或者回调函数，并且协程状态是不是EXEC，如果是，则跳过这个任务，继续找下一个能在当前线程执行的任务
                SYLAR_ASSERT(it->fiber || it->cb);    // 任务要么有协程，要么有回调函数
                if(it->fiber && it->fiber->getState() == Fiber::EXEC) {
                    ++it;
                    continue;
                }
                
                // 否则就是找到了一个能在当前线程尝试执行的任务，取出任务并跳出循环（在后面判断是否为死任务）
                // 下面这几行代码可能会不执行：条件1:任务队列是空的  条件2:队列里有任务，但循环遍历完都没有找到一个能给当前线程执行的协程，is_active 为false，则直接跳出循环，执行后面的空闲协程逻辑
                ft = *it;
                m_fibers.erase(it);     // 取出任务
                ++m_activeThreadCount;
                is_active = true;
                break;
            }
        }

        // 是否需要唤醒其他线程
        if(tickle_me) {
            tickle();
        }

        // 第一种情况：抢到协程任务
        if(ft.fiber && (ft.fiber->getState() != Fiber::TERM && ft.fiber->getState() != Fiber::EXCEPT)) {
            ft.fiber->swapIn();   // 切换到这个携程并执行
            --m_activeThreadCount;

            // 如果协程执行完后，状态是 READY（还想继续跑），则重新扔回队列，等待下次调度
            if(ft.fiber->getState() == Fiber::READY) {
                schedule(ft.fiber);
            // 如果协程执行完没结束，没异常，则挂起
            } else if(ft.fiber->getState() != Fiber::TERM && ft.fiber->getState() != Fiber::EXCEPT) { 
                ft.fiber->m_state = Fiber::HOLD;
            }
            ft.reset();    // 清空任务

        // 第二种情况：抢到一个函数 / 回调任务
        } else if(ft.cb) {
            // 如果有复用的协程，重置函数
            if(cb_fiber) {
                cb_fiber->reset(ft.cb);
            // 如果没有复用的协程，则创建一个
            } else {
                cb_fiber.reset(new Fiber(ft.cb));  // 把函数 / 回调任务包装成协程
            }
            ft.reset();

            cb_fiber->swapIn();    // 切换到协程执行函数
            --m_activeThreadCount;

            // 如果协程执行完后，状态是 READY（还想继续跑），则重新扔回队列，等待下次调度
            if(cb_fiber->getState() == Fiber::READY) {
                schedule(cb_fiber);
                cb_fiber.reset();
            // 如果执行完，结束了或者异常了，则从协程上释放函数/回调任务
            } else if(cb_fiber->getState() == Fiber::EXCEPT || cb_fiber->getState() == Fiber::TERM) {
                cb_fiber->reset(nullptr);
            // 否则挂起
            } else {    //if(cb_fiber->getState() != Fiber::TERM) {
                cb_fiber->m_state = Fiber::HOLD;
                cb_fiber.reset();
            }
        // 第三种情况：case1. 没有拿到任何任务（队列为空/ 有其他线程的任务但是没有本线程可以尝试执行的任务）
        //           case2. 拿到了任务但是不能执行（结束（TERM）或异常（EXCEPT）的死任务）
            /*  如果is_active为false则说明上面while循环遍历完都没有拿到能在本线程尝试执行的任务，则执行空闲协程的逻辑
                如果is_active为true则说明上面while循环中拿到了可以在本线程尝试执行的任务后置为了活跃，但是任务是死任务，不能执行。所以要立刻重新回去抢任务 
            */ 
        } else {
            // case2: 回去重新抢任务的逻辑
            if(is_active){ 
                --m_activeThreadCount;    // 活跃线程数要减回去
                continue;   // 必须直接进入到下一轮循环重新去抢任务，不然执行下面的idle去睡觉，整个调度器就会卡住不干活（抢错了，任务死了，开始摆烂）
            }

            // case1: 空闲协程退出的逻辑
            // 如果idle协程结束，说明调度器要退出了，整个线程的调度循环终止。这是线程正常结束的唯一出口。
            if(idle_fiber->getState() == Fiber::TERM) {
                SYLAR_LOG_INFO(g_logger) << "idle fiber term";
                break;
            }

            // case1: 执行空闲协程的逻辑，让出CPU等待唤醒
            ++m_idleThreadCount;    // 标记本线程空闲协程数+1
            idle_fiber->swapIn();   // 切换到空闲协程，挂起，让出CPU等待唤醒, swapIn()->idle()->Fiber::YieldToHold->swapOut()切回调度器主协程继续跑
            --m_idleThreadCount;    // 线程从挂起中恢复。本线程空闲协程数-1

            // 空闲协程被唤醒后，如果没有结束/异常 就挂起
            if(idle_fiber->getState() != Fiber::TERM && idle_fiber->getState() != Fiber::EXCEPT) {
                idle_fiber->m_state = Fiber::HOLD;
            }
        }
    }
}

void Scheduler::tickle() {
    SYLAR_LOG_INFO(g_logger) << "tickle";
}

bool Scheduler::stopping() {
    MutexType::Lock lock(m_mutex);
    return m_autoStop && m_stopping && m_fibers.empty() && m_activeThreadCount == 0;
}

void Scheduler::idle() {
    SYLAR_LOG_INFO(g_logger) << "idle";
    while(!stopping()) {
        Fiber::YieldToHold();
    }
}

}