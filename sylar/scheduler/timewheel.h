#ifndef __SYLAR_TIMEWHEEL_H__
#define __SYLAR_TIMEWHEEL_H__

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <vector>

#include "sylar/core/thread.h"        // RWMutex / Spinlock
#include "sylar/scheduler/timer.h"    // sylar::Timer (底层驱动定时器)

namespace sylar {

class IOManager;

// ============================================================
// 三级时间轮 (Hierarchical Timing Wheel)
//
// 与 sylar 现有 TimerManager(std::set 最小堆) 的区别与定位:
//   - 最小堆: 插入/取消 O(log n), 适合"稀疏、长周期、需精确取消"的场景。
//   - 时间轮: 插入 O(1), 到期摊还 O(1), 适合"大量短周期/高频"定时器
//             (如多房间回合超时、海量客户端心跳)。
//   两者并存互补, 不替换 TimerManager。
//
// 三级结构(沿用经典分层时间轮设计):
//   第1级 ms 槽 : 每 tick 步长一个槽, 共 (1000/step) 个, 覆盖 1 秒。
//   第2级 秒槽  : 固定 60 个, 覆盖 1 分钟。
//   第3级 分槽  : maxMin 个, 覆盖 maxMin 分钟。
// 高层槽到期后, 其内事件 cascade(降级) 到低层槽, 直至第1级被 tick 触发执行。
//
// 驱动: 不自建线程(避免与 sylar 协程模型冲突)。
//   tick() 由外部调度器(IOManager) 按 m_steps 毫秒周期调用;
//   到期回调经 IOManager::schedule 投递到协程执行, 与 TimerManager
//   的 listExpiredCb -> Scheduler::schedule 模式一致, 回调内可安全
//   使用协程 API(addTimer / yield / socket IO 等)。
//
// 线程安全: 内部用 sylar RWMutex 保护槽位, 锁内不做任何会阻塞/yield 的操作。
// ============================================================
class TimeWheel : public std::enable_shared_from_this<TimeWheel> {
public:
    typedef std::shared_ptr<TimeWheel> ptr;
    typedef RWMutex RWMutexType;

    // 事件句柄: 调用方持有以 cancel
    struct Timer {
        typedef std::shared_ptr<Timer> ptr;
        uint64_t id = 0;                 // 事件唯一 ID
        uint64_t intervalMs = 0;         // 周期(0=一次性)
        uint64_t nextMs = 0;             // 下次到期绝对毫秒时间戳
        std::function<void()> cb;        // 回调
        bool cancelled = false;          // 取消标志
    };

    TimeWheel() = default;
    ~TimeWheel();

    // 初始化时间轮。
    //   steps : tick 步长(毫秒), 必须整除 1000。
    //   maxMin: 分钟槽数量, 决定可表达的最长定时(约 maxMin 分钟)。
    // 初始化后需由外部(IOManager) 周期性调用 tick() 推进。
    void init(int steps, int maxMin);

    // 添加定时事件。
    //   ms       : 间隔(毫秒), 必须为 steps 的整数倍且 < 最大容量。
    //   cb       : 回调(在协程里执行)。
    //   recurring: true=周期性, false=一次性。
    // 返回事件句柄(可用于 cancel)。
    Timer::ptr addTimer(uint64_t ms, std::function<void()> cb, bool recurring = false);

    // 取消事件(标记取消, 下次到期时不触发)。
    bool cancel(Timer::ptr t);

    // 推进一个 tick(由 IOManager 周期调用)。处理所有到期槽位并 cascade。
    void tick();

    // 当前逻辑时间(自 init 起累计毫秒, 单调递增, 不受系统时钟回退影响)。
    uint64_t nowMs() const { return m_nowMs; }

    // 启动: 用给定 IOManager 创建一个周期性底层 sylar::Timer 驱动 tick()。
    // 之后再通过该 IOManager 调度到期回调。返回底层驱动 Timer(可 cancel 停止)。
    // 这里返回的是 sylar::Timer(scheduler/timer.h), 注意与上面本类的 TimeWheel::Timer 区分。
    sylar::Timer::ptr start(IOManager* iom);

private:
    // 槽位索引计算(绝对 ms 时间 -> 各层位置)
    int msSlotOf(uint64_t ms) const { return (int)((ms % 1000) / m_steps); }
    int secSlotOf(uint64_t ms) const { return (int)((ms % 60000) / 1000); }
    int minSlotOf(uint64_t ms) const { return (int)((ms / 60000) % m_thirdLevelCount); }

    // 把事件按其 nextMs 插入合适层级槽位(锁外计算, 锁内 push)
    void insertLocked(Timer::ptr t);
    // 处理一个槽位链表: 到期事件的回调收集到 fire, 周期事件/未到期事件重新插入(降级)
    void processSlotLocked(std::vector<Timer::ptr>& slot, std::vector<std::function<void()>>& fire);
    // 索引换算: 毫秒槽 [0, m_firstLevelCount), 秒槽, 分槽
    int slotIndexMs(int s) const { return s; }
    int slotIndexSec(int s) const { return m_firstLevelCount + s; }
    int slotIndexMin(int m) const { return m_firstLevelCount + m_secondLevelCount + m; }

private:
    bool m_inited = false;
    int m_steps = 0;              // tick 步长(毫秒)
    int m_firstLevelCount = 0;    // 毫秒槽数 = 1000/steps
    int m_secondLevelCount = 60;  // 秒槽数(固定 60)
    int m_thirdLevelCount = 0;    // 分钟槽数 = maxMin

    uint64_t m_nowMs = 0;         // 单调逻辑时钟(累计 tick × steps)
    uint64_t m_nextId = 1;        // 事件 ID 自增

    // 扁平槽位数组: [ms 槽 | 秒槽 | 分槽], 每槽一个事件链表
    std::vector<std::vector<Timer::ptr>> m_slots;
    RWMutexType m_mutex;

    // 驱动用底层 sylar Timer(start 时创建), 析构时取消
    std::weak_ptr<sylar::Timer> m_driver;
};

} // namespace sylar

#endif
