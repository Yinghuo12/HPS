#ifndef __SYLAR_IOMANAGER_H__
#define __SYLAR_IOMANAGER_H__

#include "scheduler.h"
#include "timer.h"


namespace sylar {

class IOManager : public Scheduler, public TimerManager {
public:
    typedef std::shared_ptr<IOManager> ptr;
    typedef RWMutex RWMutexType;

    enum Event {
        NONE    = 0x0,
        READ    = 0x1, //EPOLLIN
        WRITE   = 0x4, //EPOLLOUT
    };
private:
    // 句柄上下文结构体，用于保存句柄的读/写事件信息
    struct FdContext {
        typedef Mutex MutexType;
        struct EventContext {
            Scheduler* scheduler = nullptr; //事件执行的scheduler
            Fiber::ptr fiber;               //事件协程
            std::function<void()> cb;       //事件的回调函数
        };

        EventContext& getContext(Event event);
        void resetContext(EventContext& ctx);   // 重置上下文
        void triggerEvent(Event event);

        EventContext read;      //读事件
        EventContext write;     //写事件
        int fd = 0;             //事件关联的句柄
        Event events = NONE;    //已经注册的事件
        MutexType mutex;
    };

public:
    IOManager(size_t threads = 1, bool use_caller = true, const std::string& name = "");
    ~IOManager();

    //0 success, -1 error
    int addEvent(int fd, Event event, std::function<void()> cb = nullptr);
    bool delEvent(int fd, Event event);
    bool cancelEvent(int fd, Event event);  // 先从 epoll 取消监听，再触发协程 / 回调执行

    bool cancelAll(int fd);

    static IOManager* GetThis();

protected:
    void tickle() override;
    bool stopping() override;
    void idle() override;

    // 实现TimerManager的虚函数
    void onTimerInsertedAtFront() override;

public:
    static IOManager* GetInstance();

    void contextResize(size_t size);
    bool stopping(uint64_t& timeout); 
private:
    int m_epfd = 0;
    int m_tickleFds[2];   // 用于唤醒IOManager

    RWMutexType m_mutex;
    std::atomic<size_t> m_pendingEventCount = {0};   // 等待处理的事件数量
    std::vector<FdContext*> m_fdContexts;            // 句柄上下文数组
};

}

#endif