#include "sylar/scheduler/timewheel.h"

#include "sylar/core/log.h"
#include "sylar/core/sys_util.h"      // GetCurrentMS
#include "sylar/scheduler/iomanager.h"
#include "sylar/scheduler/timer.h"

namespace sylar {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

TimeWheel::~TimeWheel() {
    // 停止底层驱动 Timer(若已 start)
    auto drv = m_driver.lock();
    if(drv) drv->cancel();
}

void TimeWheel::init(int steps, int maxMin) {
    if(steps <= 0 || 1000 % steps != 0) {
        SYLAR_LOG_ERROR(g_logger) << "TimeWheel init: invalid steps=" << steps
            << " (must divide 1000)";
        return;
    }
    if(maxMin <= 0) {
        SYLAR_LOG_ERROR(g_logger) << "TimeWheel init: invalid maxMin=" << maxMin;
        return;
    }
    m_steps = steps;
    m_firstLevelCount = 1000 / steps;
    m_secondLevelCount = 60;
    m_thirdLevelCount = maxMin;
    m_slots.resize(m_firstLevelCount + m_secondLevelCount + m_thirdLevelCount);
    m_nowMs = GetCurrentMS();   // 以系统当前毫秒为起点, 但后续只靠 tick 累加(单调)
    m_inited = true;
    SYLAR_LOG_INFO(g_logger) << "TimeWheel init: steps=" << steps
        << " msSlots=" << m_firstLevelCount
        << " secSlots=" << m_secondLevelCount
        << " minSlots=" << m_thirdLevelCount;
}

TimeWheel::Timer::ptr TimeWheel::addTimer(uint64_t ms, std::function<void()> cb, bool recurring) {
    if(!m_inited) {
        SYLAR_LOG_ERROR(g_logger) << "TimeWheel not inited";
        return nullptr;
    }
    // 校验: interval 必须为步长整数倍, 且在可表达范围内(总容量 = 分槽数 × 60s)
    uint64_t capacity = (uint64_t)m_thirdLevelCount * 60u * 1000u;
    if(ms < (uint64_t)m_steps || ms % m_steps != 0 || ms >= capacity) {
        SYLAR_LOG_ERROR(g_logger) << "TimeWheel addTimer: invalid interval=" << ms
            << " (must be multiple of " << m_steps << " and < " << capacity << ")";
        return nullptr;
    }
    auto t = std::make_shared<Timer>();
    t->id = m_nextId++;
    t->intervalMs = recurring ? ms : 0;
    t->cb = std::move(cb);
    // 绝对到期时间(基于单调逻辑时钟, 避免漂移): 修复原型 bug#3
    // 原型反复用会变的 m_timePos 做基准算下一次位置导致漂移, 这里用绝对 nextMs。
    t->nextMs = m_nowMs + ms;

    RWMutexType::WriteLock lock(m_mutex);
    insertLocked(t);
    return t;
}

bool TimeWheel::cancel(Timer::ptr t) {
    if(!t) return false;
    RWMutexType::WriteLock lock(m_mutex);
    if(t->cancelled) return false;
    t->cancelled = true;
    t->cb = nullptr;   // 释放回调资源
    return true;
}

// 把事件按其 nextMs 绝对时间插入对应层级槽位。
// 层级选择(关键, 修复原型 bug#2): 事件应放在"能被未来某个 tick 精确命中的最高层级"。
//   - 若 next 与当前不在同一分钟 -> 分槽
//   - 同分钟不同秒             -> 秒槽
//   - 同秒不同 ms 槽           -> ms 槽(最终执行层)
void TimeWheel::insertLocked(Timer::ptr t) {
    uint64_t nxt = t->nextMs;
    int curMin = minSlotOf(m_nowMs);
    int curSec = secSlotOf(m_nowMs);
    int curMs = msSlotOf(m_nowMs);
    int nxtMin = minSlotOf(nxt);
    int nxtSec = secSlotOf(nxt);
    int nxtMs = msSlotOf(nxt);

    // 用"逻辑时间差"判断层级, 而非简单比较槽索引(避免跨周期回绕误判)
    // 距离 < 1 秒 -> ms 槽; < 1 分钟 -> 秒槽; 否则 -> 分槽
    // 注意: nextMs 一定 >= nowMs(addTimer 保证), 差值非负。
    uint64_t delta = nxt - m_nowMs;
    int idx;
    if(delta < 1000u) {
        idx = slotIndexMs(nxtMs);
    } else if(delta < 60000u) {
        idx = slotIndexSec(nxtSec);
    } else {
        idx = slotIndexMin(nxtMin);
    }
    (void)curMin; (void)curSec; (void)curMs;   // 仅用于调试参考, 实际以 delta 判层
    m_slots[idx].push_back(t);
}

void TimeWheel::processSlotLocked(std::vector<Timer::ptr>& slot,
                                  std::vector<std::function<void()>>& fire) {
    if(slot.empty()) return;
    // 取出整槽(交换为空)
    std::vector<Timer::ptr> pending;
    pending.swap(slot);

    for(auto& t : pending) {
        if(!t || t->cancelled) continue;
        // 到期判断(修复原型 bug#1): 用绝对时间 <= 比较, 而非精确相等, 避免漏触发。
        if(t->nextMs <= m_nowMs) {
            if(t->cb) fire.push_back(t->cb);
            // 周期性: 算下一次到期(基于自身 nextMs 累加 interval, 不漂移)
            if(t->intervalMs > 0) {
                // 若已滞后多个周期, 对齐到当前时间之后的下一个周期点
                while(t->nextMs <= m_nowMs) t->nextMs += t->intervalMs;
                insertLocked(t);   // 重新插入(可能降级到低层槽)
            }
            // 一次性事件: 不重新插入, 自然销毁(shared_ptr 引用归零)
        } else {
            // 未到期(从高层降级下来但还没轮到): 重新插入到更精确的低层槽
            insertLocked(t);
        }
    }
}

void TimeWheel::tick() {
    if(!m_inited) return;

    // 推进逻辑时钟一个步长
    m_nowMs += m_steps;

    // 计算本 tick 前后的秒/分位置, 判断是否跨边界
    uint64_t prev = m_nowMs - m_steps;
    int prevSec = secSlotOf(prev);
    int prevMin = minSlotOf(prev);
    int curSec = secSlotOf(m_nowMs);
    int curMin = minSlotOf(m_nowMs);
    int curMs = msSlotOf(m_nowMs);

    std::vector<std::function<void()>> fire;
    {
        RWMutexType::WriteLock lock(m_mutex);
        // 修复原型 bug#4: 原型一次 tick 只三选一处理, 跨边界会漏处理低层。
        // 正确顺序: 先处理分槽(到期事件降级到秒槽), 再秒槽(降级到 ms 槽), 最后 ms 槽(执行)。
        if(curMin != prevMin) {
            processSlotLocked(m_slots[slotIndexMin(curMin)], fire);
        }
        if(curSec != prevSec) {
            processSlotLocked(m_slots[slotIndexSec(curSec)], fire);
        }
        // ms 槽: 每个 tick 都处理当前 ms 槽
        processSlotLocked(m_slots[slotIndexMs(curMs)], fire);
    } // 释放锁

    // 锁外: 把收集的回调投递到 IOManager 协程执行(改造#2: 不在裸线程直调)
    if(!fire.empty()) {
        IOManager* iom = IOManager::GetThis();
        if(iom) {
            for(auto& f : fire) iom->schedule(std::move(f));
        } else {
            for(auto& f : fire) f();   // 兜底: 无 IOManager 时同步执行
        }
    }
}

sylar::Timer::ptr TimeWheel::start(IOManager* iom) {
    if(!m_inited || !iom) return nullptr;
    auto self = shared_from_this();
    // 底层 sylar Timer 周期触发 tick; recurring=true
    auto drv = iom->addTimer(m_steps, [self]() { self->tick(); }, true);
    m_driver = drv;
    SYLAR_LOG_INFO(g_logger) << "TimeWheel started, tick every " << m_steps << "ms";
    return drv;
}

} // namespace sylar
