#include "util.h"
#include <execinfo.h>
#include <sys/time.h>

#include "log.h"
#include "fiber.h"

namespace sylar {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

pid_t GetThreadId() {
    return syscall(SYS_gettid);
}

uint32_t GetFiberId() {
    return sylar::Fiber::GetFiberId();
}

void Backtrace(std::vector<std::string> &bt, int size, int skip) {
    void** array = (void**)malloc(sizeof(void*) * size);  // 生成一个指向栈的每一层的指针
    size_t s = ::backtrace(array, size);                  // 获取当前栈的每一层

    char** strings = backtrace_symbols(array, s);         // 将每一层的地址转换成字符串
    if (strings == NULL) {
        SYLAR_LOG_ERROR(g_logger) << "backtrace_symbols error";
        return;
    }

    for (size_t i = skip; i < s; ++i) {
        bt.push_back(strings[i]);           // 将每一层的字符串存入vector中
    }

    free(strings);
    free(array);
}
std::string BacktraceToString(int size, int skip, const std::string& prefix){
    std::vector<std::string> bt;  // 生成一个vector来存储每一层的字符串
    Backtrace(bt, size, skip);    // 获取每一层调用栈的信息
    std::stringstream ss;
    for (size_t i = 0; i < bt.size(); ++i) {
        ss << prefix << bt[i] << std::endl;  // 逐层打印出每一层的调用栈信息
        
    }
    return ss.str();
}

uint64_t GetCurrentMS() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000ul + tv.tv_usec / 1000;
}

uint64_t GetCurrentUS() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000 * 1000ul + tv.tv_usec;
}

}