#include "fiber_semaphore.h"
#include "sylar/core/log.h"
#include "sylar/scheduler/iomanager.h"
#include "sylar/scheduler/timer.h"

namespace sylar {

static Logger::ptr g_logger = SYLAR_LOG_NAME("system");

FiberSemaphore::FiberSemaphore(uint32_t count)
    : m_count(count) {
}

FiberSemaphore::~FiberSemaphore() {
}

void FiberSemaphore::wait() {
    Fiber::ptr self = Fiber::GetThis();
    {
        Spinlock::Lock lk(m_mutex);
        if(m_count > 0) {
            --m_count;
            return;
        }
        m_waiters.push_back(self);
    }
    Fiber::YieldToHold();
}

bool FiberSemaphore::wait(uint64_t ms) {
    if(ms == 0) {
        wait();
        return true;
    }

    Fiber::ptr self = Fiber::GetThis();
    IOManager* iom = IOManager::GetThis();
    Timer::ptr timer;
    bool timedOut = false;

    {
        Spinlock::Lock lk(m_mutex);
        if(m_count > 0) {
            --m_count;
            return true;
        }
        m_waiters.push_back(self);
    }

    // 超时定时器: 超时后把当前协程从等待列表移除并重新调度
    std::weak_ptr<Fiber> weakSelf = self;
    Spinlock* mutexPtr = &m_mutex;
    std::list<Fiber::ptr>* waitersPtr = &m_waiters;

    timer = iom->addTimer(ms, [weakSelf, mutexPtr, waitersPtr, &timedOut]() {
        auto fiber = weakSelf.lock();
        if(!fiber) {
            return;
        }
        {
            Spinlock::Lock lk(*mutexPtr);
            waitersPtr->remove(fiber);
            timedOut = true;
        }
        IOManager::GetThis()->schedule(fiber);
    });

    Fiber::YieldToHold();

    // 恢复后取消定时器(如果尚未超时)
    timer->cancel();
    return !timedOut;
}

void FiberSemaphore::notify() {
    Fiber::ptr fiber;
    {
        Spinlock::Lock lk(m_mutex);
        if(m_waiters.empty()) {
            ++m_count;
            return;
        }
        fiber = m_waiters.front();
        m_waiters.pop_front();
    }
    if(fiber) {
        IOManager::GetThis()->schedule(fiber);
    }
}

void FiberSemaphore::notifyAll() {
    std::list<Fiber::ptr> waiters;
    {
        Spinlock::Lock lk(m_mutex);
        waiters.swap(m_waiters);
    }
    IOManager* iom = IOManager::GetThis();
    for(auto& f : waiters) {
        if(f) {
            iom->schedule(f);
        }
    }
}

}
