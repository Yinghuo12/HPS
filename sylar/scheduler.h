#ifndef __SYLAR_SCHEDULER_H__
#define __SYLAR_SCHEDULER_H__

#include <memory>
#include <vector>
#include <list>
#include "fiber.h"
#include "thread.h"

namespace sylar {

class Scheduler {
public:
    typedef std::shared_ptr<Scheduler> ptr;
    typedef Mutex MutexType;

    Scheduler(size_t threads = 1, bool use_caller = true, const std::string& name = "");
    virtual ~Scheduler();

    const std::string& getName() const { return m_name;}

    static Scheduler* GetThis();
    static Fiber* GetMainFiber();

    void start();
    void stop();

    // 添加协程
    template<class FiberOrCb>
    void schedule(FiberOrCb fc, int thread = -1) {
        bool need_tickle = false;
        {
            MutexType::Lock lock(m_mutex);
            need_tickle = scheduleNoLock(fc, thread);
        }

        if(need_tickle) {
            tickle();
        }
    }

    // 添加多个协程
    template<class InputIterator>
    void schedule(InputIterator begin, InputIterator end) {
        bool need_tickle = false;
        {
            MutexType::Lock lock(m_mutex);
            while(begin != end) {
                need_tickle = scheduleNoLock(&*begin, -1) || need_tickle;
                ++begin;
            }
        }
        if(need_tickle) {
            tickle();
        }
    }


protected:
    virtual void tickle();      // 唤醒线程
    void run();                 // 执行协程调度
    virtual bool stopping();    // 判断是否停止
    virtual void idle();        // 空闲时执行

    void setThis();
    bool hasIdleThreads() { return m_idleThreadCount > 0; }


private:
    // 无锁添加协程
    template<class FiberOrCb>
    bool scheduleNoLock(FiberOrCb fc, int thread) {
        bool need_tickle = m_fibers.empty();
        FiberAndThread ft(fc, thread);
        if(ft.fiber || ft.cb) {
            m_fibers.push_back(ft);
        }
        return need_tickle;  // 如果队列为空，需要唤醒
    }


private:
    struct FiberAndThread {
        Fiber::ptr fiber;            // 协程指针
        std::function<void()> cb;    // 回调函数
        int thread;                  // 线程id

        FiberAndThread(Fiber::ptr f, int thr) :fiber(f), thread(thr) { }
        FiberAndThread(Fiber::ptr* f, int thr) :thread(thr) { fiber.swap(*f); }
        FiberAndThread(std::function<void()> f, int thr) :cb(f), thread(thr) { }
        FiberAndThread(std::function<void()>* f, int thr) :thread(thr) { cb.swap(*f); }
        FiberAndThread() :thread(-1) { }

        void reset() {
            fiber = nullptr;
            cb = nullptr;
            thread = -1;
        }
    };


private:
    MutexType m_mutex;
    std::vector<Thread::ptr> m_threads;  // 线程池
    std::list<FiberAndThread> m_fibers;  // 协程队列
    Fiber::ptr m_rootFiber;              // 主协程
    std::string m_name;                  // 线程池名称


protected:
    std::vector<int> m_threadIds;                   // 线程id
    size_t m_threadCount = 0;                       // 线程数量
    std::atomic<size_t> m_activeThreadCount = {0};  // 活跃线程数量
    std::atomic<size_t> m_idleThreadCount = {0};    // 空闲线程数量
    bool m_stopping = true;                         // 是否停止
    bool m_autoStop = false;                        // 自动停止
    int m_rootThread = 0;                           // 主线程id (use_caller)
};

}

#endif