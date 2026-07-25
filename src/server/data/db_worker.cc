#include "db_worker.h"
#include "sylar/scheduler/iomanager.h"
#include "sylar/core/log.h"

namespace ddt {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("rpc");

DbWorkerPool::DbWorkerPool(size_t threadCount, sylar::IOManager* iom)
    : m_running(true)
    , m_iom(iom) {
    for(size_t i = 0; i < threadCount; ++i) {
        m_threads.emplace_back([this]() {
            while(m_running) {
                Task task;
                {
                    std::unique_lock<std::mutex> lk(m_mutex);
                    m_cv.wait(lk, [this]() {
                        return !m_running || !m_queue.empty();
                    });
                    if(!m_running && m_queue.empty()) {
                        return;
                    }
                    task = std::move(m_queue.front());
                    m_queue.pop();
                }
                // DB 线程执行 dbTask(可阻塞 mysql_query / redisCommand)
                try {
                    task.dbTask();
                } catch(const std::exception& e) {
                    SYLAR_LOG_ERROR(g_logger) << "DbWorker task exception: " << e.what();
                } catch(...) {
                    SYLAR_LOG_ERROR(g_logger) << "DbWorker task unknown exception";
                }
                // Async 通道(onComplete==nullptr): 静默结束
                // Synch 通道(onComplete!=nullptr): 投回 IOManager 协程执行
                if(task.onComplete && m_iom) {
                    auto cb = std::move(task.onComplete);
                    m_iom->schedule([cb]() { cb(); });
                }
            }
        });
    }
    SYLAR_LOG_INFO(g_logger) << "DbWorkerPool started, threads=" << threadCount;
}

DbWorkerPool::~DbWorkerPool() {
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_running = false;
    }
    m_cv.notify_all();
    for(auto& t : m_threads) {
        if(t.joinable()) {
            t.join();
        }
    }
}

void DbWorkerPool::execute(std::function<void()> dbTask) {
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_queue.push({std::move(dbTask), nullptr});
    }
    m_cv.notify_one();
}

void DbWorkerPool::query(std::function<void()> dbTask, std::function<void()> onComplete) {
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_queue.push({std::move(dbTask), std::move(onComplete)});
    }
    m_cv.notify_one();
}

}
