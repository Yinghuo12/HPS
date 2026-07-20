// LD_PRELOAD 垫片: 规避 sylar hook × gRPC 静态构造期 close() 空指针崩溃

#include <dlfcn.h>
#include <sys/syscall.h>
#include <unistd.h>

// 不写 extern close_fun close_f;(会留未定义符号致 LD_PRELOAD 加载失败), 改运行时 dlsym 取地址
extern "C" int close(int fd) {
    typedef int (*close_fun)(int);

    // close_f 变量的地址(sylar 导出的 extern "C" BSS 全局), 重试解析直到拿到
    static close_fun* close_f_ptr = nullptr;
    if (!close_f_ptr) {
        close_f_ptr = (close_fun*)dlsym(RTLD_DEFAULT, "close_f");
    }

    // close_f 非空 ⟺ sylar hook 就绪; 每次重读 *close_f_ptr(不能缓存, 否则 gRPC 静态期 NULL 永久走 syscall)
    if (close_f_ptr && *close_f_ptr) {
        static close_fun sylar_close = nullptr;
        if (!sylar_close) {
            sylar_close = (close_fun)dlsym(RTLD_NEXT, "close");  // 命中 libsylar 的 close
        }
        if (sylar_close) {
            return sylar_close(fd);  // 保留 FdMgr::del + cancelAll 协程 fd 清理
        }
    }

    // sylar 未就绪(main 前 / gRPC 静态初始化窗口): 直接 syscall, 绝不解引用 NULL
    return (int)syscall(SYS_close, fd);
}
