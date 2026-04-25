#ifndef __SYLAR_UTIL_H__
#define __SYLAR_UTIL_H__

#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <stdio.h>
#include <stdint.h>
#include <vector>
#include <string>
#include "sylar/util/hash_util.h"

namespace sylar {

pid_t GetThreadId();
uint32_t GetFiberId();

void Backtrace(std::vector<std::string> &bt, int size = 64, int skip = 1);   // 获取每层调用栈信息 skip是跳过前面多少层，1层指跳过自己
std::string BacktraceToString(int size = 64, int skip = 2, const std::string &prefix = "");  // 将调用栈信息逐层转换为字符串 skip是跳过前面的多少层 2层指跳过自己和自己调用的函数Backtrace()

// 获取时间戳
uint64_t GetCurrentMS();
uint64_t GetCurrentUS();

std::string Time2Str(time_t ts = time(0), const std::string& format = "%Y-%m-%d %H:%M:%S");

}

#endif // __SYLAR_UTIL_H__