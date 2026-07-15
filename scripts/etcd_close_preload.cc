// etcd_close_preload.cc —— 生产安全的 LD_PRELOAD 垫片（不改 sylar 源码）
//
// 问题：libsylar.so 的 syscall hook 里 close_f 等真实函数地址由 sylar::hook_init()
//   在 _HookIniter 静态构造期用 dlsym 解析。但 libsylar.so DT_NEEDED libetcd-cpp-api.so，
//   glibc 按"依赖先初始化"使 libetcd-cpp-api 内 gRPC 的静态初始化器(main 前)先调 close(fd)，
//   此时 close_f 仍为 NULL → sylar hook.cc:376 `return close_f(fd)` 跳 0 → SIGSEGV。
//   这会崩掉任何 sylar(hook)+gRPC 程序，含全部 5 个 ddt_* 服务。
//
// 本垫片（优于"始终 syscall"的简单垫片）：
//   - 读 sylar 导出的 extern "C" BSS 全局 close_f（nm -D libsylar.so: B close_f）。
//   - close_f==NULL（main 前的 gRPC 静态初始化窗口）：直接 syscall(SYS_close)，绝不解引用 NULL。
//   - close_f!=NULL（sylar _HookIniter 已跑完）：转发到 sylar 自己的 close 钩子
//     (dlsym(RTLD_NEXT,"close") 命中 libsylar 的 close)，保留 FdMgr::del + cancelAll
//     的协程 fd 清理逻辑 —— 对启用 hook 的真实服务安全。
//
// 构建: g++ -shared -fPIC -o lib/libetcd_close_fix.so scripts/etcd_close_preload.cc -ldl
// 使用: LD_PRELOAD=/abs/lib/libetcd_close_fix.so ./ddt_*  (任何 sylar+etcd 程序)

#include <dlfcn.h>
#include <sys/syscall.h>
#include <unistd.h>

// 注意：不能写 `extern close_fun close_f;`——会让本 .so 留下未定义符号，
// LD_PRELOAD 在 libsylar 之前加载时解析不到，加载直接失败（exit 127）。
// 改为运行时 dlsym 拿 sylar 的 close_f 变量地址，再读其当前值判断 sylar 是否就绪。

extern "C" int close(int fd) {
    typedef int (*close_fun)(int);

    // close_f 变量的地址（sylar 导出的 extern "C" BSS 全局）。重试解析直到拿到。
    static close_fun* close_f_ptr = nullptr;
    if (!close_f_ptr) {
        close_f_ptr = (close_fun*)dlsym(RTLD_DEFAULT, "close_f");
    }

    // close_f 当前值非空 ⟺ sylar 的 hook_init 已跑 ⟺ 可安全转给 sylar 的 close 钩子。
    // 每次 close() 重读 *close_f_ptr 的值——不能缓存判定，否则首次发生在 gRPC
    // 静态初始化期读到 NULL 会永久走 syscall，退化回不安全的简单垫片。
    if (close_f_ptr && *close_f_ptr) {
        static close_fun sylar_close = nullptr;
        if (!sylar_close) {
            sylar_close = (close_fun)dlsym(RTLD_NEXT, "close");  // 命中 libsylar 的 close
        }
        if (sylar_close) {
            return sylar_close(fd);   // 保留 FdMgr::del + cancelAll 协程 fd 清理
        }
    }

    // sylar 未就绪（main 前 / gRPC 静态初始化窗口）：直接 syscall，绝不解引用 NULL。
    return (int)syscall(SYS_close, fd);
}
