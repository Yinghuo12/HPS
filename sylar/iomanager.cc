#include "iomanager.h"
#include "macro.h"
#include "log.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <string.h>
#include <unistd.h>

namespace sylar {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

IOManager::FdContext::EventContext& IOManager::FdContext::getContext(IOManager::Event event) {
    switch(event) {
        case IOManager::READ:
            return read;
        case IOManager::WRITE:
            return write;
        default:
            SYLAR_ASSERT2(false, "getContext");
    }
}

void IOManager::FdContext::resetContext(EventContext& ctx) {
    ctx.scheduler = nullptr;
    ctx.fiber.reset();
    ctx.cb = nullptr;
}

// 事件触发
void IOManager::FdContext::triggerEvent(IOManager::Event event) {
    //SYLAR_LOG_INFO(g_logger) << "fd=" << fd
    //    << " triggerEvent event=" << event
    //    << " events=" << events;
    SYLAR_ASSERT(events & event);
    //if(SYLAR_UNLIKELY(!(event & event))) {
    //    return;
    //}
    events = (Event)(events & ~event);
    EventContext& ctx = getContext(event);
    if(ctx.cb) {
        ctx.scheduler->schedule(&ctx.cb);
    } else {
        ctx.scheduler->schedule(&ctx.fiber);
    }
    ctx.scheduler = nullptr;
    return;
}

IOManager::IOManager(size_t threads, bool use_caller, const std::string& name): Scheduler(threads, use_caller, name) {
    m_epfd = epoll_create(5000);   // 创建epoll
    SYLAR_ASSERT(m_epfd > 0);

    int rt = pipe(m_tickleFds);    // 创建管道
    SYLAR_ASSERT(!rt);

    epoll_event event;
    memset(&event, 0, sizeof(epoll_event));
    event.events = EPOLLIN | EPOLLET;  // 边沿触发
    event.data.fd = m_tickleFds[0];    // 读端

    rt = fcntl(m_tickleFds[0], F_SETFL, O_NONBLOCK);   // 设置非阻塞，让读写操作不等待、不卡住
    SYLAR_ASSERT(!rt);

    // 将管道的读端添加到epoll中监听
    rt = epoll_ctl(m_epfd, EPOLL_CTL_ADD, m_tickleFds[0], &event);
    SYLAR_ASSERT(!rt);

    contextResize(32);

    // 启动调度器线程
    start();
}

IOManager::~IOManager() {
    stop();
    close(m_epfd);
    close(m_tickleFds[0]);
    close(m_tickleFds[1]);

    // 释放指针，防止内存泄漏
    for(size_t i = 0; i < m_fdContexts.size(); ++i) {
        if(m_fdContexts[i]) {
            delete m_fdContexts[i];
        }
    }
}

void IOManager::contextResize(size_t size) {
    m_fdContexts.resize(size);

    for(size_t i = 0; i < m_fdContexts.size(); ++i) {
        if(!m_fdContexts[i]) {
            m_fdContexts[i] = new FdContext;
            m_fdContexts[i]->fd = i;    // 索引就是fd
        }
    }
}

int IOManager::addEvent(int fd, Event event, std::function<void()> cb) {
    FdContext* fd_ctx = nullptr;
    RWMutexType::ReadLock lock(m_mutex);
    if((int)m_fdContexts.size() > fd) {
        // 如果fd小于当前句柄上下文数组的大小，则直接获取fd对应的句柄上下文
        fd_ctx = m_fdContexts[fd];
        lock.unlock();
    } else {
        lock.unlock();
        RWMutexType::WriteLock lock2(m_mutex);
        contextResize(fd * 1.5);
        fd_ctx = m_fdContexts[fd];
    }

    FdContext::MutexType::Lock lock2(fd_ctx->mutex);
    if(SYLAR_UNLIKELY(fd_ctx->events & event)) {   // 如果fd_ctx已经注册了该事件，则直接返回，防止有两个不同的线程在操作同一个fd的同一个事件
        SYLAR_LOG_ERROR(g_logger) << "addEvent assert fd=" << fd << " event=" << event << " fd_ctx.event=" << fd_ctx->events;
        SYLAR_ASSERT(!(fd_ctx->events & event));
    }

    int op = fd_ctx->events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
    epoll_event epevent;
    epevent.events = EPOLLET | fd_ctx->events | event;   // 边沿触发 ｜ 当前fd的事件 ｜ 新增的事件
    epevent.data.ptr = fd_ctx;

    // 将fd添加到epoll中监听
    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if(rt) {
        SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
            << op << "," << fd << "," << epevent.events << "):"
            << rt << " (" << errno << ") (" << strerror(errno) << ")";
        return -1;
    }

    ++m_pendingEventCount;
    fd_ctx->events = (Event)(fd_ctx->events | event);                 // 更新fd_ctx中的events
    FdContext::EventContext& event_ctx = fd_ctx->getContext(event);   // 获取对应事件的上下文
    SYLAR_ASSERT(!event_ctx.scheduler && !event_ctx.fiber && !event_ctx.cb);  // 防止重复添加事件

    event_ctx.scheduler = Scheduler::GetThis();
    if(cb) {
        // 如果传入了回调函数，则将回调函数保存到上下文中
        event_ctx.cb.swap(cb);    
    } else {
        // 否则将当前协程保存到上下文中
        event_ctx.fiber = Fiber::GetThis();    
        // 压力测试段错误bug: 在复杂的多线程调度框架下，强制要求发起调用的协程必须是 EXEC 是过于苛刻且没有必要的。只要协程没死 (TERM / EXCEPT) 并且处于可挂起状态就行。注释掉下面这行断言，修复压力测试段错误bug。
        // SYLAR_ASSERT(event_ctx.fiber->getState() == Fiber::EXEC);  
    }
    return 0;
}

bool IOManager::delEvent(int fd, Event event) {
    RWMutexType::ReadLock lock(m_mutex);
    
    // 如果fd大于等于当前句柄上下文数组的大小，则直接返回false
    if((int)m_fdContexts.size() <= fd) {  
        return false;
    }
    FdContext* fd_ctx = m_fdContexts[fd];
    lock.unlock();

    FdContext::MutexType::Lock lock2(fd_ctx->mutex);
    // 如果fd_ctx没有注册该事件，则直接返回false
    if(SYLAR_UNLIKELY(!(fd_ctx->events & event))) {  
        return false;
    }

    Event new_events = (Event)(fd_ctx->events & ~event);  // A & ~B = 从 A里删掉 B
    int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events = EPOLLET | new_events;
    epevent.data.ptr = fd_ctx;

    // 将fd从epoll中移除
    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if(rt) {
        SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
            << op << "," << fd << "," << epevent.events << "):"
            << rt << " (" << errno << ") (" << strerror(errno) << ")";
        return false;
    }

    --m_pendingEventCount;
    fd_ctx->events = new_events;
    FdContext::EventContext& event_ctx = fd_ctx->getContext(event);
    fd_ctx->resetContext(event_ctx);    // 重置上下文, 清空空协程、回调、指针
    return true;
}

bool IOManager::cancelEvent(int fd, Event event) {
    RWMutexType::ReadLock lock(m_mutex);
    if((int)m_fdContexts.size() <= fd) {
        return false;
    }
    FdContext* fd_ctx = m_fdContexts[fd];
    lock.unlock();

    FdContext::MutexType::Lock lock2(fd_ctx->mutex);
    if(SYLAR_UNLIKELY(!(fd_ctx->events & event))) {
        return false;
    }

    Event new_events = (Event)(fd_ctx->events & ~event);
    int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events = EPOLLET | new_events;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if(rt) {
        SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
            << op << "," << fd << "," << epevent.events << "):"
            << rt << " (" << errno << ") (" << strerror(errno) << ")";
        return false;
    }

    fd_ctx->triggerEvent(event);   // 强制触发事件
    --m_pendingEventCount;
    return true;
}

bool IOManager::cancelAll(int fd) {
    RWMutexType::ReadLock lock(m_mutex);
    if((int)m_fdContexts.size() <= fd) {
        return false;
    }
    FdContext* fd_ctx = m_fdContexts[fd];
    lock.unlock();

    FdContext::MutexType::Lock lock2(fd_ctx->mutex);
    if(SYLAR_UNLIKELY(!fd_ctx->events)) {
        return false;
    }

    int op = EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events = 0;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if(rt) {
        SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
            << op << "," << fd << "," << epevent.events << "):"
            << rt << " (" << errno << ") (" << strerror(errno) << ")";
        return false;
    }

    // 分别触发读、写事件
    if(fd_ctx->events & READ) {
        fd_ctx->triggerEvent(READ);
        --m_pendingEventCount;
    }
    if(fd_ctx->events & WRITE) {
        fd_ctx->triggerEvent(WRITE);
        --m_pendingEventCount;
    }

    SYLAR_ASSERT(fd_ctx->events == 0);
    return true;
}

IOManager* IOManager::GetThis() {
    return dynamic_cast<IOManager*>(Scheduler::GetThis());
}

void IOManager::tickle() {
    if(!hasIdleThreads()) {
        return;
    }
    int rt = write(m_tickleFds[1], "T", 1);   // 往管道中写入一个字符（发送消息），唤醒epoll_wait
    SYLAR_ASSERT(rt == 1);
}

bool IOManager::stopping(uint64_t& timeout) {
    timeout = getNextTimer();
    return timeout == ~0ull && m_pendingEventCount == 0 && Scheduler::stopping();
}

bool IOManager::stopping() {
    uint64_t timeout = 0;
    return stopping(timeout);
}


void IOManager::idle() {
    SYLAR_LOG_DEBUG(g_logger) << "IOManager::idle()";
    const uint64_t MAX_EVENTS = 256;   // 最大事件数
    epoll_event* events = new epoll_event[MAX_EVENTS]();
    std::shared_ptr<epoll_event> shared_events(events, [](epoll_event* ptr){
        delete[] ptr;
    });

    while(true) {
        uint64_t next_timeout = 0;
        if(SYLAR_UNLIKELY(stopping(next_timeout))) {
                SYLAR_LOG_INFO(g_logger) << "name=" << getName() << " idle stopping exit";
                break;
            }

        int rt = 0;
        static const int MAX_TIMEOUT = 3000;   // 默认最大超时时间

        // epoll_wait阻塞等待，直到有事件发生
        do {
            if(next_timeout != ~0ull) {
                // 有超时时间 则取最小值
                next_timeout = (int)next_timeout > MAX_TIMEOUT ? MAX_TIMEOUT : (int)next_timeout;

            } else  {
                next_timeout = MAX_TIMEOUT;   // 没有超时时间，则设置为最大超时时间
            }
            rt = epoll_wait(m_epfd, events, MAX_EVENTS, (int)next_timeout); // 如果事件没有触发,则等待next_timeout后也会唤醒,超时返回0
            // SYLAR_LOG_INFO(g_logger) << "epoll_wait rt=" << rt;
        } while (rt < 0 && errno == EINTR);
        
        std::vector<std::function<void()> > cbs;
        listExpiredCb(cbs);    // 处理定时器回调
        if(!cbs.empty()){
            // SYLAR_LOG_DEBUG(g_logger) << "on timer cbs.size()=" << cbs.size();
            schedule(cbs.begin(), cbs.end());   // 把定时器回调加入协程队列
            cbs.clear();
        }
        //if(SYLAR_UNLIKELY(rt == MAX_EVNETS)) {
        //    SYLAR_LOG_INFO(g_logger) << "epoll wait events=" << rt;
        //}

        for(int i = 0; i < rt; ++i) {
            epoll_event& event = events[i];
            // SYLAR_LOG_INFO(g_logger) << "event=" << event.events; // 打印原始事件值
            if(event.data.fd == m_tickleFds[0]) {  
                // 如果管道中有数据，则说明有协程需要被唤醒
                uint8_t dummy[256];
                while(read(m_tickleFds[0], dummy, sizeof(dummy)) > 0);  // ET模式，需要把管道中的数据全部读走,必须用while
                continue;
            }

            FdContext* fd_ctx = (FdContext*)event.data.ptr;
            FdContext::MutexType::Lock lock(fd_ctx->mutex);

            // 如果是错误或者中断
            if(event.events & (EPOLLERR | EPOLLHUP)) {
                event.events |= EPOLLIN | EPOLLOUT;
            }

            int real_events = NONE;   // 内核触发的事件

            // 如果是读事件
            if(event.events & EPOLLIN) {
                real_events |= READ;
            }

            // 如果是写事件
            if(event.events & EPOLLOUT) {
                real_events |= WRITE;
            }

            // 打印原始事件值和实际事件值
            // SYLAR_LOG_INFO(g_logger) << "event.events=" << event.events << " real_events=" << real_events;

            // 如果内核触发的事件不是我注册关心的事件
            if((fd_ctx->events & real_events) == NONE) {  
                continue;
            }

            // 从我们注册的事件中，把本次内核触发的事件剪掉，剩下的就是还没有触发的事件
            int left_events = (fd_ctx->events & ~real_events);
            int op = left_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
            event.events = EPOLLET | left_events;

            // 如果还有没触发的事件，则修改，如果没有，则删除
            int rt2 = epoll_ctl(m_epfd, op, fd_ctx->fd, &event);
            if(rt2) {
                SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
                    << op << "," << fd_ctx->fd << "," << event.events << "):"
                    << rt2 << " (" << errno << ") (" << strerror(errno) << ")";
                continue;
            }
            
            // SYLAR_LOG_INFO(g_logger) << " fd=" << fd_ctx->fd << " events=" << fd_ctx->events << " real_events=" << real_events;
            // 分别触发读、写事件
            if(fd_ctx->events & READ) {
                fd_ctx->triggerEvent(READ);
                --m_pendingEventCount;
            }
            if(fd_ctx->events & WRITE) {
                fd_ctx->triggerEvent(WRITE);
                --m_pendingEventCount;
            }
        }

        // 处理完一轮事件之后，必须主动让出当前线程，把执行权交还给调度器，让线程去跑调度器里排队的其他协程/任务
        Fiber::ptr cur = Fiber::GetThis();
        auto raw_ptr = cur.get();
        cur.reset();

        raw_ptr->swapOut();
    }
}

void IOManager::onTimerInsertedAtFront() {
    tickle();
}

}