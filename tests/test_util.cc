#include "sylar/sylar.h"
#include <assert.h>

sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

void test_assert(){
    SYLAR_LOG_INFO(g_logger) << sylar::BacktraceToString(10);  // 参数：栈最大深度，跳过层数，前缀
    // SYLAR_ASSERT(false);
    SYLAR_ASSERT2(0 == 1, "0 == 1");
}

int main(){
    test_assert();
    return 0;
}
