#ifndef __SYLAR_ASYNC_SOCKET_STREAM_H__
#define __SYLAR_ASYNC_SOCKET_STREAM_H__

#include <atomic>
#include <list>
#include <string>
#include <unordered_map>
#include <functional>

#include "sylar/core/thread.h"
#include "sylar/net/socket_stream.h"
#include "sylar/scheduler/fiber.h"
#include "sylar/scheduler/fiber_semaphore.h"
#include "sylar/scheduler/iomanager.h"
#include "sylar/scheduler/scheduler.h"
#include "sylar/scheduler/timer.h"

namespace sylar {

// 通用异步 Socket 流
// - 异步发送: 多协程 enqueue, 单 doWrite 协程串行消费(FiberSemaphore 驱动)
// - 异步接收: doRead 协程阻塞读, 收到完整响应后按 sn 匹配唤醒等待者
// - 请求-响应配对: Ctx + sn(request_id) + m_ctxs map
// - 超时: onTimeOut 定时器, 超时后唤醒等待者并标 TIMEOUT
// - autoConnect: 连接断开自动重连
// - waitFiber: 双 FiberSemaphore 等待 doRead + doWrite 都退出后才能重连
class AsyncSocketStream : public SocketStream, public std::enable_shared_from_this<AsyncSocketStream> {
public:
    typedef std::shared_ptr<AsyncSocketStream> ptr;
    typedef sylar::RWMutex RWMutexType;
    typedef std::function<bool(AsyncSocketStream::ptr)> connect_callback;
    typedef std::function<void(AsyncSocketStream::ptr)> disconnect_callback;

    AsyncSocketStream(Socket::ptr sock, bool owner = true);
    virtual ~AsyncSocketStream() {}

    // 启动 doRead + doWrite 协程。返回 true 表示成功启动。
    virtual bool start();
    virtual void close() override;

    enum Error {
        OK = 0,
        TIMEOUT = -1,
        IO_ERROR = -2,
        NOT_CONNECT = -3,
    };

public:
    // 发送上下文基类(public: 子类 RpcStream::RpcCtx 需要继承)
    struct SendCtx {
        typedef std::shared_ptr<SendCtx> ptr;
        virtual ~SendCtx() {}
        virtual bool doSend(AsyncSocketStream::ptr stream) = 0;
    };

    // 便捷发送: 把原始字节包装成 SendCtx 入队。gate 等只需异步发送的场景用。
    void send(const std::string& data);
    void send(const void* data, size_t len);

    // 请求-响应配对上下文
    struct Ctx : public SendCtx {
        typedef std::shared_ptr<Ctx> ptr;
        Ctx();

        uint32_t sn;           // 序列号(request_id)
        uint32_t timeout;      // 超时毫秒
        uint32_t result;       // 结果码(OK/TIMEOUT/IO_ERROR)
        bool timed;            // 是否已超时

        // 原子唤醒: 用 CAS 保证只唤醒一次(替代 777continue 的 Atomic::compareAndSwapBool)
        std::atomic<Scheduler*> scheduler;
        Fiber::ptr fiber;
        Timer::ptr timer;

        // 唤醒等待的协程
        virtual void doRsp();
    };

public:
    // 读写协程
    void doRead();
    void doWrite();
    void startRead();
    void startWrite();

    // 超时处理
    void onTimeOut(Ctx::ptr ctx);

    // Ctx 管理(按 sn 查找/添加/删除)
    Ctx::ptr getCtx(uint32_t sn);
    Ctx::ptr getAndDelCtx(uint32_t sn);
    bool addCtx(Ctx::ptr ctx);

    // 入队发送
    bool enqueue(SendCtx::ptr ctx);

    // 等待 doRead + doWrite 退出(重连前调)
    bool waitFiber();

    // 关闭并唤醒所有等待者
    bool innerClose();

    // 子类必须实现: doRead 里收到完整消息后解析出 sn 并返回对应 Ctx
    virtual Ctx::ptr doRecv() = 0;

    // 配置
    void setWorker(IOManager* v) { m_worker = v; }
    IOManager* getWorker() const { return m_worker; }
    void setIOManager(IOManager* v) { m_iomanager = v; }
    IOManager* getIOManager() const { return m_iomanager; }

    bool isAutoConnect() const { return m_autoConnect; }
    void setAutoConnect(bool v) { m_autoConnect = v; }

    connect_callback getConnectCb() const { return m_connectCb; }
    disconnect_callback getDisconnectCb() const { return m_disconnectCb; }
    void setConnectCb(connect_callback v) { m_connectCb = v; }
    void setDisconnectCb(disconnect_callback v) { m_disconnectCb = v; }

protected:
    FiberSemaphore m_sem;         // doWrite 等待队列有数据
    FiberSemaphore m_waitSem;     // 等待 doRead+doWrite 退出(初始 0, 各 wait 一次)
    RWMutexType m_queueMutex;     // 保护 m_queue
    std::list<SendCtx::ptr> m_queue;
    RWMutexType m_mutex;          // 保护 m_ctxs
    std::unordered_map<uint32_t, Ctx::ptr> m_ctxs;

    std::atomic<uint32_t> m_sn;   // 序列号生成器
    bool m_autoConnect;
    Timer::ptr m_timer;           // autoConnect 重连定时器
    IOManager* m_iomanager;
    IOManager* m_worker;

    connect_callback m_connectCb;
    disconnect_callback m_disconnectCb;
};

// 多 AsyncSocketStream 轮询管理器
class AsyncSocketStreamManager {
public:
    typedef sylar::RWMutex RWMutexType;
    typedef AsyncSocketStream::connect_callback connect_callback;
    typedef AsyncSocketStream::disconnect_callback disconnect_callback;

    AsyncSocketStreamManager();
    virtual ~AsyncSocketStreamManager() {}

    void add(AsyncSocketStream::ptr stream);
    void clear();
    void setConnection(const std::vector<AsyncSocketStream::ptr>& streams);
    AsyncSocketStream::ptr get();

    connect_callback getConnectCb() const { return m_connectCb; }
    disconnect_callback getDisconnectCb() const { return m_disconnectCb; }
    void setConnectCb(connect_callback v);
    void setDisconnectCb(disconnect_callback v);

private:
    RWMutexType m_mutex;
    uint32_t m_size;
    std::atomic<uint32_t> m_idx;
    std::vector<AsyncSocketStream::ptr> m_datas;
    connect_callback m_connectCb;
    disconnect_callback m_disconnectCb;
};

}

#endif
