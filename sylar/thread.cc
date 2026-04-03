#include "thread.h"
#include "log.h"
#include "util.h"

namespace sylar {

// 线程局部存储（TLS）每个线程都有自己独立的一份变量
static thread_local Thread* t_thread = nullptr;              // 线程地址
static thread_local std::string t_thread_name = "UNKNOW";    // 名字

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

// 信号量构造函数：初始化信号量，初始计数为 count
Semaphore::Semaphore(uint32_t count) {
    if (sem_init(&m_semaphore, 0, count)) {
        throw std::logic_error("sem_init error");
    }
}

Semaphore::~Semaphore() {
    sem_destroy(&m_semaphore);
}

// P操作：等待信号量（申请资源）
void Semaphore::wait() {
    if (sem_wait(&m_semaphore)) {
        throw std::logic_error("sem_wait error");
    }
}

// V操作：通知信号量（释放资源）
void Semaphore::notify() {
    if (sem_post(&m_semaphore)) {
        throw std::logic_error("sem_post error");
    }
}


Thread* Thread::GetThis() {
    return t_thread;
}

const std::string& Thread::GetName() {
    return t_thread_name;
}

void Thread::SetName(const std::string& name) {
    if(t_thread) {
        t_thread->m_name = name;  // Thread 对象里的名字（身份证上的名字）
    }
    t_thread_name = name;  // 当前线程自己快速获取用 （自己称呼自己的名字）
}

// 线程函数
void* Thread::run(void* arg) {
    Thread* thread = (Thread*)arg;  // 将参数转换为Thread对象指针，arg传的就是Thread对象自己的地址（this）
    t_thread = thread;
    t_thread_name = thread->m_name;
    thread->m_id = sylar::GetThreadId();
    pthread_setname_np(pthread_self(), thread->m_name.substr(0, 15).c_str());  //  设置线程名称（限制为15个字符，因为POSIX线程名称长度限制）

    std::function<void()> cb = std::move(thread->m_cb);
    thread->m_semaphore.notify();  // 通知线程创建完成

    cb();
    return 0;
}


Thread::Thread(std::function<void()> cb, const std::string& name) :m_cb(cb) ,m_name(name) {
    if(name.empty()) {
        m_name = "UNKNOW";
    }
    int rt = pthread_create(&m_thread, nullptr, &Thread::run, this);  // this作为run的参数
    if(rt) {
        SYLAR_LOG_ERROR(g_logger) << "pthread_create thread fail, rt=" << rt << " name=" << name;
        throw std::logic_error("pthread_create error");
    }
    m_semaphore.wait();  // 等待线程创建完成
}

Thread::~Thread() {
    if(m_thread) {
        pthread_detach(m_thread);
    }
}

void Thread::join() {
    if(m_thread) {
        int rt = pthread_join(m_thread, nullptr);
        if(rt) {
            SYLAR_LOG_ERROR(g_logger) << "pthread_join thread fail, rt=" << rt << " name=" << m_name;
            throw std::logic_error("pthread_join error");
        }
        m_thread = 0;
    }
}


}