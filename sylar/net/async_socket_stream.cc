#include "async_socket_stream.h"
#include "sylar/core/log.h"
#include "sylar/scheduler/iomanager.h"

namespace sylar {

static Logger::ptr g_logger = SYLAR_LOG_NAME("system");

// ---- 便捷发送 ----

// RawSendCtx: 包装原始字节为 SendCtx
struct RawSendCtx : public AsyncSocketStream::SendCtx {
    std::string data;
    bool doSend(AsyncSocketStream::ptr stream) override {
        int64_t rt = stream->write(data.data(), data.size());
        return rt > 0;
    }
};

void AsyncSocketStream::send(const std::string& data) {
    auto ctx = std::make_shared<RawSendCtx>();
    ctx->data = data;
    enqueue(ctx);
}

void AsyncSocketStream::send(const void* data, size_t len) {
    send(std::string((const char*)data, len));
}

// ---- Ctx ----

AsyncSocketStream::Ctx::Ctx()
    : sn(0)
    , timeout(0)
    , result(0)
    , timed(false)
    , scheduler(nullptr) {
}

void AsyncSocketStream::Ctx::doRsp() {
    // CAS 保证只唤醒一次(替代 777continue 的 Atomic::compareAndSwapBool)
    Scheduler* expected = scheduler.load();
    if(!scheduler.compare_exchange_strong(expected, nullptr)) {
        return;
    }
    if(!expected || !fiber) {
        return;
    }
    if(timer) {
        timer->cancel();
        timer = nullptr;
    }
    if(timed) {
        result = TIMEOUT;
    }
    expected->schedule(fiber);
}

// ---- AsyncSocketStream ----

AsyncSocketStream::AsyncSocketStream(Socket::ptr sock, bool owner)
    : SocketStream(sock, owner)
    , m_waitSem(2)   // 等待 doRead + doWrite 退出
    , m_sn(0)
    , m_autoConnect(false)
    , m_iomanager(nullptr)
    , m_worker(nullptr) {
}

bool AsyncSocketStream::start() {
    if(!m_iomanager) {
        m_iomanager = IOManager::GetThis();
    }
    if(!m_worker) {
        m_worker = IOManager::GetThis();
    }

    do {
        waitFiber();

        if(m_timer) {
            m_timer->cancel();
            m_timer = nullptr;
        }

        if(!isConnected()) {
            if(!m_socket->reconnect()) {
                innerClose();
                m_waitSem.notify();
                m_waitSem.notify();
                break;
            }
        }

        if(m_connectCb) {
            if(!m_connectCb(shared_from_this())) {
                innerClose();
                m_waitSem.notify();
                m_waitSem.notify();
                break;
            }
        }

        startRead();
        startWrite();
        return true;
    } while(false);

    if(m_autoConnect) {
        if(m_timer) {
            m_timer->cancel();
            m_timer = nullptr;
        }
        auto self = shared_from_this();
        m_timer = m_iomanager->addTimer(2 * 1000,
            [self]() { self->start(); });
    }
    return false;
}

void AsyncSocketStream::doRead() {
    try {
        while(isConnected()) {
            auto ctx = doRecv();
            if(ctx) {
                ctx->doRsp();
            } else {
                // doRecv 返回 nullptr = 读失败(连接断开/EOF) 或 无匹配 Ctx。
                // 不能继续空转(旧实现忽略 null 会 busy-loop → 非抢占式协程下独占线程):
                //   - 连接断开: 退出循环, 由 innerClose + autoConnect 重连
                //   - 无匹配 Ctx(偶发): 退出后 start 重启 doRead(代价是一次重连, 远好于 100% CPU 空转)
                break;
            }
        }
    } catch(...) {
        SYLAR_LOG_WARN(g_logger) << "AsyncSocketStream doRead exception";
    }

    SYLAR_LOG_DEBUG(g_logger) << "AsyncSocketStream doRead exit";
    innerClose();
    m_waitSem.notify();

    if(m_autoConnect) {
        auto self = shared_from_this();
        m_iomanager->addTimer(10, [self]() { self->start(); });
    }
}

void AsyncSocketStream::doWrite() {
    try {
        while(isConnected()) {
            m_sem.wait();
            std::list<SendCtx::ptr> ctxs;
            {
                RWMutexType::WriteLock lock(m_queueMutex);
                m_queue.swap(ctxs);
            }
            auto self = shared_from_this();
            for(auto& i : ctxs) {
                if(!i->doSend(self)) {
                    innerClose();
                    break;
                }
            }
        }
    } catch(...) {
        SYLAR_LOG_WARN(g_logger) << "AsyncSocketStream doWrite exception";
    }

    SYLAR_LOG_DEBUG(g_logger) << "AsyncSocketStream doWrite exit";
    {
        RWMutexType::WriteLock lock(m_queueMutex);
        m_queue.clear();
    }
    m_waitSem.notify();
}

void AsyncSocketStream::startRead() {
    auto self = shared_from_this();
    m_iomanager->schedule([self]() { self->doRead(); });
}

void AsyncSocketStream::startWrite() {
    auto self = shared_from_this();
    m_iomanager->schedule([self]() { self->doWrite(); });
}

void AsyncSocketStream::onTimeOut(Ctx::ptr ctx) {
    {
        RWMutexType::WriteLock lock(m_mutex);
        m_ctxs.erase(ctx->sn);
    }
    ctx->timed = true;
    ctx->doRsp();
}

AsyncSocketStream::Ctx::ptr AsyncSocketStream::getCtx(uint32_t sn) {
    RWMutexType::ReadLock lock(m_mutex);
    auto it = m_ctxs.find(sn);
    return it != m_ctxs.end() ? it->second : nullptr;
}

AsyncSocketStream::Ctx::ptr AsyncSocketStream::getAndDelCtx(uint32_t sn) {
    Ctx::ptr ctx;
    RWMutexType::WriteLock lock(m_mutex);
    auto it = m_ctxs.find(sn);
    if(it != m_ctxs.end()) {
        ctx = it->second;
        m_ctxs.erase(it);
    }
    return ctx;
}

bool AsyncSocketStream::addCtx(Ctx::ptr ctx) {
    RWMutexType::WriteLock lock(m_mutex);
    m_ctxs.insert(std::make_pair(ctx->sn, ctx));
    return true;
}

bool AsyncSocketStream::enqueue(SendCtx::ptr ctx) {
    RWMutexType::WriteLock lock(m_queueMutex);
    bool empty = m_queue.empty();
    m_queue.push_back(ctx);
    lock.unlock();
    if(empty) {
        m_sem.notify();
    }
    return empty;
}

bool AsyncSocketStream::innerClose() {
    if(isConnected() && m_disconnectCb) {
        m_disconnectCb(shared_from_this());
    }
    SocketStream::close();
    m_sem.notify();
    std::unordered_map<uint32_t, Ctx::ptr> ctxs;
    {
        RWMutexType::WriteLock lock(m_mutex);
        ctxs.swap(m_ctxs);
    }
    {
        RWMutexType::WriteLock lock(m_queueMutex);
        m_queue.clear();
    }
    for(auto& i : ctxs) {
        i.second->result = IO_ERROR;
        i.second->doRsp();
    }
    return true;
}

bool AsyncSocketStream::waitFiber() {
    m_waitSem.wait();
    m_waitSem.wait();
    return true;
}

void AsyncSocketStream::close() {
    m_autoConnect = false;
    if(m_timer) {
        m_timer->cancel();
    }
    SocketStream::close();
}

// ---- AsyncSocketStreamManager ----

AsyncSocketStreamManager::AsyncSocketStreamManager()
    : m_size(0)
    , m_idx(0) {
}

void AsyncSocketStreamManager::add(AsyncSocketStream::ptr stream) {
    RWMutexType::WriteLock lock(m_mutex);
    m_datas.push_back(stream);
    ++m_size;
    if(m_connectCb) {
        stream->setConnectCb(m_connectCb);
    }
    if(m_disconnectCb) {
        stream->setDisconnectCb(m_disconnectCb);
    }
}

void AsyncSocketStreamManager::clear() {
    RWMutexType::WriteLock lock(m_mutex);
    for(auto& i : m_datas) {
        i->close();
    }
    m_datas.clear();
    m_size = 0;
}

void AsyncSocketStreamManager::setConnection(const std::vector<AsyncSocketStream::ptr>& streams) {
    auto cs = streams;
    RWMutexType::WriteLock lock(m_mutex);
    cs.swap(m_datas);
    m_size = m_datas.size();
    if(m_connectCb || m_disconnectCb) {
        for(auto& i : m_datas) {
            if(m_connectCb) {
                i->setConnectCb(m_connectCb);
            }
            if(m_disconnectCb) {
                i->setDisconnectCb(m_disconnectCb);
            }
        }
    }
    lock.unlock();
    for(auto& i : cs) {
        i->close();
    }
}

AsyncSocketStream::ptr AsyncSocketStreamManager::get() {
    RWMutexType::ReadLock lock(m_mutex);
    for(uint32_t i = 0; i < m_size; ++i) {
        uint32_t idx = m_idx.fetch_add(1);
        if(m_datas[idx % m_size]->isConnected()) {
            return m_datas[idx % m_size];
        }
    }
    return nullptr;
}

void AsyncSocketStreamManager::setConnectCb(connect_callback v) {
    m_connectCb = v;
    RWMutexType::WriteLock lock(m_mutex);
    for(auto& i : m_datas) {
        i->setConnectCb(m_connectCb);
    }
}

void AsyncSocketStreamManager::setDisconnectCb(disconnect_callback v) {
    m_disconnectCb = v;
    RWMutexType::WriteLock lock(m_mutex);
    for(auto& i : m_datas) {
        i->setDisconnectCb(m_disconnectCb);
    }
}

}
