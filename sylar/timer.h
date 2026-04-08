#ifndef __SYLAR_TIMER_H__
#define __SYLAR_TIMER_H__

#include <memory>
#include <vector>
#include <set>
#include "thread.h"

namespace sylar {

class TimerManager;
// 定时器
class Timer : public std::enable_shared_from_this<Timer> {
friend class TimerManager;
public:
    typedef std::shared_ptr<Timer> ptr;
    bool cancel();     // 取消定时器
    bool refresh();    // 刷新定时器
    bool reset(uint64_t ms, bool from_now);   // 重置定时器
private:
    // 不能在类外直接创建，只能通过TimerManager创建
    Timer(uint64_t ms, std::function<void()> cb, bool recurring, TimerManager* manager);   
    Timer(uint64_t next);
private:
    bool m_recurring = false;       //是否循环定时器
    uint64_t m_ms = 0;              //执行周期
    uint64_t m_next = 0;            //精确的执行时间
    std::function<void()> m_cb;
    TimerManager* m_manager = nullptr;
private:
    // 比较函数，用于set排序
    struct Comparator {
        bool operator()(const Timer::ptr& lhs, const Timer::ptr& rhs) const;
    };
};


// 定时器管理器
class TimerManager {
friend class Timer;
public:
    typedef RWMutex RWMutexType;

    TimerManager();
    virtual ~TimerManager();

    Timer::ptr addTimer(uint64_t ms, std::function<void()> cb, bool recurring = false);   // 外部addTimer方法，调用内部addTimer方法
    Timer::ptr addConditionTimer(uint64_t ms, std::function<void()> cb, std::weak_ptr<void> weak_cond, bool recurring = false);  // 条件定时器, 传入一个onTimer的检查条件回调函数，当条件满足时，onTimer才会被调用
    uint64_t getNextTimer();     // 下一个定时器的执行时间
    void listExpiredCb(std::vector<std::function<void()> >& cbs);   // 获取所有已经超过了等待时间的定时器 的回调函数， 需要马上执行
    bool hasTimer();
protected:
    virtual void onTimerInsertedAtFront() = 0;
    void addTimer(Timer::ptr val, RWMutexType::WriteLock& lock);   // 内部addTimer方法，被外部addTimer方法调用
private:
    // 检测是否调了时间
    bool detectClockRollover(uint64_t now_ms);    
private:
    RWMutexType m_mutex;
    std::set<Timer::ptr, Timer::Comparator> m_timers;    // set容器，用于存储定时器(有序)
    bool m_tickled = false;
    uint64_t m_previouseTime = 0;      // 上一次检测的时间
};

}

#endif