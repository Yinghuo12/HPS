#ifndef __SYLAR_FIBER_SEMAPHORE_H__
#define __SYLAR_FIBER_SEMAPHORE_H__

#include <list>
#include "sylar/core/thread.h"
#include "sylar/scheduler/fiber.h"

namespace sylar {

// 协程级信号量
// wait() 时协程让出(yield)而非阻塞线程; notify() 重新调度等待的协程。
// 基于 Fiber::YieldToHold + IOManager::schedule 实现(与 IOManager::triggerEvent 同机制)。
class FiberSemaphore {
public:
    typedef std::shared_ptr<FiberSemaphore> ptr;

    explicit FiberSemaphore(uint32_t count = 0);
    ~FiberSemaphore();

    // 协程级阻塞: count > 0 则 --count 立即返回, 否则 YieldToHold 让出执行权。
    void wait();

    // 带超时: 超时返回 false, 成功获取返回 true。
    bool wait(uint64_t ms);

    // 唤醒一个等待的协程: 有等待者则 schedule 唤醒, 否则 count++。
    void notify();

    // 唤醒所有等待的协程。
    void notifyAll();

private:
    Spinlock m_mutex;
    int32_t m_count;
    std::list<Fiber::ptr> m_waiters;
};

}

#endif
