// test_timewheel.cc — 三级时间轮(TimeWheel)测试
//
// 验证 TimeWheel 在 sylar 协程模型下的工作情况:
//   - tick=100ms, maxMin=10 (支持最长约 10 分钟定时)
//   - 注册四档周期事件: 100ms / 500ms / 2000ms / 65000ms(1分5秒)
//   - 65000ms 用于验证"跨分钟"场景(分槽 -> 秒槽 -> ms槽 的 cascade 降级)
//
// 对比 test_timer.cc: 本测试验证的是 sylar 新增的 TimeWheel 组件,
// 它与现有 TimerManager(std::set 最小堆) 并存互补:
//   - 大量短周期/高频定时器(如多房间回合超时、海量心跳) -> TimeWheel 更优(O(1))
//   - 稀疏/长周期/需精确取消 -> TimerManager 更灵活
//
// 运行: bin/test_timewheel  (观察四档事件按预期周期触发)
#include "sylar/core/log.h"
#include "sylar/scheduler/iomanager.h"
#include "sylar/scheduler/timewheel.h"

static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

// 全局持有 TimeWheel, 避免局部对象析构导致回调访问悬空(参考 test_timer.cc 的陷阱)
static sylar::TimeWheel::ptr g_tw;

void test_timewheel() {
    sylar::IOManager iom(2);

    g_tw = std::make_shared<sylar::TimeWheel>();
    g_tw->init(100, 10);          // tick=100ms, 最长 10 分钟
    g_tw->start(&iom);            // 用 iom 周期驱动 tick()

    SYLAR_LOG_INFO(g_logger) << "==== TimeWheel Test Start ====";
    SYLAR_LOG_INFO(g_logger) << "tick=100ms, max support 10 min timer";

    // 100ms 周期事件(每秒约 10 次, 验证 ms 槽直接触发)
    g_tw->addTimer(100, []() {
        static int cnt = 0;
        SYLAR_LOG_INFO(g_logger) << "[100ms] trigger, count=" << ++cnt;
    }, true);

    // 500ms 周期事件(每秒 2 次)
    g_tw->addTimer(500, []() {
        static int cnt = 0;
        SYLAR_LOG_INFO(g_logger) << "    [500ms] trigger, count=" << ++cnt;
    }, true);

    // 2000ms 周期事件(每 2 秒, 验证秒槽 -> ms槽 降级)
    g_tw->addTimer(2000, []() {
        static int cnt = 0;
        SYLAR_LOG_INFO(g_logger) << "        [2000ms] trigger, count=" << ++cnt;
    }, true);

    // 65000ms 周期事件(1分5秒, 验证分槽 -> 秒槽 -> ms槽 三级 cascade)
    g_tw->addTimer(65000, []() {
        static int cnt = 0;
        SYLAR_LOG_INFO(g_logger) << "            [65000ms] trigger, count=" << ++cnt;
    }, true);

    // 额外: 验证一次性定时器(recurring=false) 与 cancel
    auto oneShot = g_tw->addTimer(3000, []() {
        SYLAR_LOG_INFO(g_logger) << "[one-shot 3000ms] fired once";
    }, false);

    auto toCancel = g_tw->addTimer(4000, []() {
        SYLAR_LOG_INFO(g_logger) << "[should NOT appear] cancelled timer fired!";
    }, true);
    // 1 秒后取消它(演示 cancel)
    iom.addTimer(1000, [toCancel]() {
        bool ok = g_tw->cancel(toCancel);
        SYLAR_LOG_INFO(g_logger) << "[cancel] a 4000ms recurring timer, ok=" << ok;
    }, false);
}

int main(int argc, char** argv) {
    test_timewheel();
    return 0;
}
