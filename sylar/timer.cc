#include "timer.h"
#include "util.h"

namespace sylar {

// 比较器
bool Timer::Comparator::operator()(const Timer::ptr& lhs, const Timer::ptr& rhs) const {
    if(!lhs && !rhs) return false;
    if(!lhs) return true;
    if(!rhs) return false;
    if(lhs->m_next < rhs->m_next) return true;
    if(rhs->m_next < lhs->m_next) return false;
    return lhs.get() < rhs.get();  // 比较两个Timer对象的内存地址
}


Timer::Timer(uint64_t ms, std::function<void()> cb, bool recurring, TimerManager* manager)
    :m_recurring(recurring)
    ,m_ms(ms)
    ,m_cb(cb)
    ,m_manager(manager) {
    m_next = sylar::GetCurrentMS() + m_ms;
}

Timer::Timer(uint64_t next)
    :m_next(next) {
}

bool Timer::cancel() {
    TimerManager::RWMutexType::WriteLock lock(m_manager->m_mutex);
    if(m_cb) {
        m_cb = nullptr;
        auto it = m_manager->m_timers.find(shared_from_this());
        m_manager->m_timers.erase(it);
        return true;
    }
    return false;
}

bool Timer::refresh() {
    TimerManager::RWMutexType::WriteLock lock(m_manager->m_mutex);
    if(!m_cb) {
        return false;
    }
    auto it = m_manager->m_timers.find(shared_from_this());
    if(it == m_manager->m_timers.end()) {
        return false;
    }
    // 先移除再插入 不能直接修改，因为set重载了比较运算符，会影响排序, 导致迭代器失效
    m_manager->m_timers.erase(it);
    m_next = sylar::GetCurrentMS() + m_ms;
    m_manager->m_timers.insert(shared_from_this());
    return true;
}

bool Timer::reset(uint64_t ms, bool from_now) {
    // 无需设置
    if(ms == m_ms && !from_now) {
        return true;
    }
    TimerManager::RWMutexType::WriteLock lock(m_manager->m_mutex);
    if(!m_cb) {
        return false;
    }
    auto it = m_manager->m_timers.find(shared_from_this());
    if(it == m_manager->m_timers.end()) {
        return false;
    }
    m_manager->m_timers.erase(it);
    uint64_t start = 0;
    // 如果from_now为true，则使用当前系统时间作为起始时间，否则使用原来起始时间
    if(from_now) {
        start = sylar::GetCurrentMS();
    } else {
        start = m_next - m_ms;   // 获取原来的起始时间
    }
    m_ms = ms;
    m_next = start + m_ms;   // 计算新的到期时间
    m_manager->addTimer(shared_from_this(), lock);
    return true;

}

TimerManager::TimerManager() {
    m_previouseTime = sylar::GetCurrentMS();
}

TimerManager::~TimerManager() {
}

// 外部addTimer方法，调用内部addTimer方法
Timer::ptr TimerManager::addTimer(uint64_t ms, std::function<void()> cb ,bool recurring) {
    Timer::ptr timer(new Timer(ms, cb, recurring, this));
    RWMutexType::WriteLock lock(m_mutex);
    addTimer(timer, lock);
    return timer;
}

// 检查某个对象是否仍然存活,如果存活则执行特定的逻辑
static void OnTimer(std::weak_ptr<void> weak_cond, std::function<void()> cb) {
    // 尝试将弱指针 weak_cond 提升为强指针 tmp。如果提升成功（即 weak_cond 指向的对象仍然存活），则执行回调函数 cb()；否则，不执行任何操作。
    std::shared_ptr<void> tmp = weak_cond.lock();
    if(tmp) {
        cb();
    }
}

Timer::ptr TimerManager::addConditionTimer(uint64_t ms, std::function<void()> cb, std::weak_ptr<void> weak_cond, bool recurring) {
    return addTimer(ms, std::bind(&OnTimer, weak_cond, cb), recurring);
}

uint64_t TimerManager::getNextTimer() {
    RWMutexType::ReadLock lock(m_mutex);
    m_tickled = false;
    if(m_timers.empty()) {
        return ~0ull;   // 0取反是最大的:没有定时器需要处理，你可以无限期地等待下去
    }

    const Timer::ptr& next = *m_timers.begin();
    uint64_t now_ms = sylar::GetCurrentMS();
    // 如果定时器已经到期，则返回0，否则返回距离下一个定时器到期的时间
    if(now_ms >= next->m_next) {
        return 0;
    } else {
        return next->m_next - now_ms;
    }
}

void TimerManager::listExpiredCb(std::vector<std::function<void()> >& cbs) {
    uint64_t now_ms = sylar::GetCurrentMS();
    std::vector<Timer::ptr> expired;
    {
        RWMutexType::ReadLock lock(m_mutex);
        if(m_timers.empty()) {
            return;
        }
    }
    RWMutexType::WriteLock lock(m_mutex);

    // 检测系统时间是否重置
    bool rollover = detectClockRollover(now_ms);
    if(!rollover && ((*m_timers.begin())->m_next > now_ms)) {
        return;
    }

    // 创建一个当前时间的定时器用于比较
    Timer::ptr now_timer(new Timer(now_ms));
    // 根据是否重置决定查找起点
    auto it = rollover ? m_timers.end() : m_timers.lower_bound(now_timer);
    // 找到所有当前时间到期的定时器
    while(it != m_timers.end() && (*it)->m_next == now_ms) {
        ++it;
    }
    // 将已过期的定时器从容器中移除并存入expired
    expired.insert(expired.begin(), m_timers.begin(), it);
    m_timers.erase(m_timers.begin(), it);
    // 为回调函数预分配空间
    cbs.reserve(expired.size());

    // 处理每个已过期的定时器
    for(auto& timer : expired) {
        // 将定时器的回调函数存入cbs
        cbs.push_back(timer->m_cb);
        // 如果是周期性定时器，重置下次执行时间并重新加入容器
        if(timer->m_recurring) {
            timer->m_next = now_ms + timer->m_ms;
            m_timers.insert(timer);
        } else {
            // 如果是一次性定时器，清除回调函数
            timer->m_cb = nullptr;
        }
    }
}

// 内部addTimer方法， 被外部addTImer方法调用
void TimerManager::addTimer(Timer::ptr val, RWMutexType::WriteLock& lock) {
    auto it = m_timers.insert(val).first;   // 插入val，并返回一个迭代器 set::insert返回一个pair，first是插入的迭代器，second是是否插入成功
    bool at_front = (it == m_timers.begin()) && !m_tickled;   // 判断是否插入到最前面
    if(at_front) {
        m_tickled = true;
    }
    lock.unlock();

    if(at_front) {
        onTimerInsertedAtFront();
    }
}

bool TimerManager::detectClockRollover(uint64_t now_ms) {
    bool rollover = false;
    // 如果传递的时间小于上次记录的时间，并且比上次记录时间减去1小时还要小
    // 则认为发生了系统时间重置
    if(now_ms < m_previouseTime && now_ms < (m_previouseTime - 60 * 60 * 1000)) {
        rollover = true;
    }
    // 更新上次记录的时间为当前时间
    m_previouseTime = now_ms;
    return rollover;
}

bool TimerManager::hasTimer() {
    RWMutexType::ReadLock lock(m_mutex);
    return !m_timers.empty();
}

}