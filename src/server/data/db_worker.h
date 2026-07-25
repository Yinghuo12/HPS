#ifndef __DDT_DB_WORKER_H__
#define __DDT_DB_WORKER_H__

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace sylar { class IOManager; }

namespace ddt {

// 双通道 DB 线程池(参考 TrinityCore DatabaseWorkerPool)
// - Async 通道(execute): 写操作 fire-and-forget, 不等结果
// - Synch 通道(query): 读操作 submit + 协程等 onComplete 回调
class DbWorkerPool {
public:
    DbWorkerPool(size_t threadCount, sylar::IOManager* iom);
    ~DbWorkerPool();

    // Async 通道: 入队, 不等结果(INSERT/UPDATE/DELETE/SetOnline/SetOffline)。
    // dbTask 在 DB 线程执行, 执行完静默结束(不回调 IOManager)。
    void execute(std::function<void()> dbTask);

    // Synch 通道: 入队 + 协程等结果(SELECT/Redis 操作)。
    // dbTask 在 DB 线程执行, 执行完后 onComplete 投回 IOManager 协程。
    // 调用方应在 submit 后 Fiber::YieldToHold 等待 onComplete 唤醒。
    void query(std::function<void()> dbTask, std::function<void()> onComplete);

private:
    struct Task {
        std::function<void()> dbTask;
        std::function<void()> onComplete;   // nullptr = async(fire-and-forget)
    };

    std::vector<std::thread> m_threads;
    std::queue<Task> m_queue;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_running;
    sylar::IOManager* m_iom;
};

}

#endif
