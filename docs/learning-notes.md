# TinyDDT 学习笔记 — 从 sylar 协程框架到五服务微服务游戏

> 这是一份「边写代码边学习」的笔记。不是 API 手册，而是**带着设计动机讲清楚为什么**：每段都回答「它解决了什么问题、踩过什么坑、为什么这样取舍」。
>
> 范围（按由浅入深）：
> - **sylar 基础层**：ucontext 协程、IOManager、Hook、时间轮、ORM、etcd RPC
> - **业务共享层**：帧/物理/地形/路由/msg_id
> - **五服务业务层**：gate / login / lobby / battle / data
> - **运维与排障**：fleet.sh、LD_PRELOAD 垫片、core dump、踩坑总结
>
> 阅读建议：每章开头先看「为什么需要它」，再看代码片段。所有代码片段均为真实源码精简摘录，行号对应仓库当前版本。涉及本会话的几处关键改动用 **§N** 标识，便于回看 git diff。

## 目录

- [第 0 章 项目概览](#第-0-章-项目概览)
- [第 1 章 sylar 协程模型](#第-1-章-sylar-协程模型)
- [第 2 章 RPC 框架（etcd + traceId 透传）](#第-2-章-rpc-框架etcd--traceid-透传)
- [第 3 章 五服务微服务架构](#第-3-章-五服务微服务架构)
- [第 4 章 协议设计（proto + frame + msg_id）](#第-4-章-协议设计proto--frame--msg_id)
- [第 5 章 网关 Gate（集中分发 + 注册式 + 安全删除）](#第-5-章-网关-gate集中分发--注册式--安全删除)
- [第 6 章 大厅 Lobby（房间状态机 + tryStart 范式）](#第-6-章-大厅-lobby房间状态机--trystart-范式)
- [第 7 章 战斗 Battle（房间锁 + 回合状态机）](#第-7-章-战斗-battle房间锁--回合状态机)
- [第 8 章 权威物理（解析弹道 + 2D 体素地形）](#第-8-章-权威物理解析弹道--2d-体素地形)
- [第 9 章 数据层（MySQL ORM + Redis 池化 + 事务）](#第-9-章-数据层mysql-orm--redis-池化--事务)
- [第 9.5 章 Redis 进阶（在线状态 Set + 世界聊天 Pub/Sub）](#第-95-章-redis-进阶在线状态-set--世界聊天-pubsub)
- [第 10 章 战绩落库（拆主子表 + 累计伤害 + 异步 schedule）](#第-10-章-战绩落库拆主子表--累计伤害--异步-schedule)
- [第 11 章 客户端架构（Unity + 主线程队列 + DebugLog）](#第-11-章-客户端架构unity--主线程队列--debuglog)
- [第 12 章 运维（fleet.sh + LD_PRELOAD + core dump）](#第-12-章-运维fleetsh--ld_preload--core-dump)
- [第 12.5 章 代码风格统一（注释精简 + K&R 重格式化）](#第-125-章-代码风格统一注释精简--kr-重格式化)
- [第 13 章 踩坑总结](#第-13-章-踩坑总结)
- [第 14 章 贯穿全局的设计原则](#第-14-章-贯穿全局的设计原则)

---

# 第 0 章 项目概览

## 0.1 TinyDDT 是什么

TinyDDT 是仿弹弹堂的回合制多人弹道射击游戏：玩家在 2D 体素地形上轮流开炮，考虑风向、角度、力度、地形坡度，命中对方扣血，先把对方 HP 归零的队伍获胜。

服务端是 **sylar C++ 协程框架**写的一套微服务，5 个独立可执行：

| 服务 | 二进制 | 端口 | 职责 |
|------|--------|------|------|
| gate | `ddt_gate` | TCP 8100 / RPC 8101 | 客户端唯一 TCP 入口；按 msg_id 转发；实现 PushService 反向推送 |
| login | `ddt_login` | HTTP 8200 / RPC 8201 | 账号、密码、token 签发与校验 |
| lobby | `ddt_lobby` | RPC 8300 | 房间、匹配、好友、聊天 |
| battle | `ddt_battle` | RPC 8400 | 战斗房间、权威物理、回合调度 |
| data | `ddt_data` | RPC 8500 | 唯一持久层（MySQL + Redis） |

客户端是 Unity (C#)，与服务端运行在同一份**物理公式**和**地形位图**协议上——服务端权威算结果，客户端用相同闭式解做视觉复放。

## 0.2 关键路径

```
                    ┌──── etcd (服务发现) ────┐
                    │                          │
   Unity(C#) ──TCP──► gate ──RPC──► login/lobby/battle ──RPC──► data
                    ▲                                    │
                    │                                    ▼
                    └──── PushService 反向推送 ◄── MySQL / Redis
```

- 客户端只跟 gate 说 TCP；
- gate 用 msg_id 路由到 login/lobby/battle；
- battle/lobby 不持客户端连接，反向推送走 gate 暴露的 `PushService` RPC；
- data 是所有持久化的唯一入口（token 存 Redis，账号/战绩存 MySQL）。

## 0.3 版本演进

git log 显示这条主线：
- v0.x：单进程 OpenGL 客户端 + WebSocket 服务端；
- v0.4：sylar ORM 模块化 + 项目重组；
- **v1.0**：客户端重写为 Unity(C#)，服务端五服务微服务化 + etcd 服务发现 + Protobuf RPC；
- 本会话：在 v1.0 基础上做了 DB 密码环境变量化、traceId 端到端透传、Redis 连接池、战绩主子表落库、gate 注册式分发、login 栈溢出修复、fleet.sh 路径匹配修复、客户端 DebugLog。

下面从最底层的协程模型讲起，一路向上到业务、到运维。

---

# 第 1 章 sylar 协程模型

> 文件：`sylar/scheduler/fiber.{h,cc}`、`sylar/scheduler/iomanager.{h,cc}`、`sylar/scheduler/hook.{h,cc}`、`sylar/scheduler/scheduler.{h,cc}`

理解 sylar 的关键是三个词：**ucontext 协程**、**IOManager 调度**、**Hook 把阻塞 IO 变让出**。整章就是为了把这三个词讲透。

## 1.1 为什么是协程，不是线程

多人游戏服要扛海量 TCP 连接（gate 几千路同时在线很常见）。线程模型下一个连接一个线程，每个线程栈 8MB，1000 连接 = 8GB 内存，且线程切换要走内核（模式切换 + TLB 失效），代价高。

协程是**用户态**的轻量"线程"：
- 切换不进内核（只是寄存器 + 栈指针的 `swapcontext`）；
- 栈很小（sylar 默认 1MB，可以更小）；
- 由用户代码（IOManager）决定什么时候切换，不是抢占式。

代价：协程必须**主动让出**，不能像线程那样被时钟打断。所以"什么时候让出"是 sylar 的核心设计——靠 Hook。

## 1.2 Fiber：ucontext 的薄封装

`Fiber`（`sylar/scheduler/fiber.h`）就是 ucontext 的 C++ 包装。状态机 6 个：

```cpp
enum State { INIT, HOLD, EXEC, TERM, READY, EXCEPT };
```

构造一个新协程（`fiber.cc:61`）就是经典三件套：

```cpp
Fiber::Fiber(std::function<void()> cb, size_t stacksize, bool use_caller)
    : m_id(++s_fiber_id), m_cb(cb) {
    m_stacksize = stacksize ? stacksize : g_fiber_stack_size->getValue();
    m_stack = StackAllocator::Alloc(m_stacksize);   // malloc 一块栈
    getcontext(&m_ctx);                              // 拿当前上下文模板
    m_ctx.uc_link = nullptr;                         // 结束后不链接
    m_ctx.uc_stack.ss_sp = m_stack;
    m_ctx.uc_stack.ss_size = m_stacksize;
    makecontext(&m_ctx, &Fiber::MainFunc, 0);        // 入口指向 MainFunc
}
```

注意这行：

```cpp
static ConfigVar<uint32_t>::ptr g_fiber_stack_size =
    Config::Lookup<uint32_t>("fiber.stack_size", 1024 * 1024, "fiber stack size");
```

**栈大小是配置项 `fiber.stack_size`，默认 1MB**。这条信息贯穿整个项目，**几乎所有"stack smashing"bug 都跟它有关**。比如本会话修复的 login 服栈溢出，就是把 `fiber.stack_size` 从 1MB 调到 2MB（详见第 13 章）。

`MainFunc` 是协程入口的跳板：

```cpp
static void MainFunc() {
    Fiber::ptr cur = GetThis();   // 自身 smart ptr, 防止运行中被析构
    try {
        cur->m_cb();              // 真正的业务函数
        cur->m_cb = nullptr;
        cur->m_state = TERM;
    } catch(...) { cur->m_state = EXCEPT; }
    swapOut();                    // 结束, 切回调度协程
}
```

为什么 `cur` 要持 `shared_ptr`？因为业务函数可能调用各种异步逻辑让出，期间外部可能 reset 这个 fiber，没有自引用就 UAF 了。

## 1.3 IOManager：epoll + Scheduler 合体

`IOManager` 继承自 `Scheduler`（N 线程跑 M 协程）和 `TimerManager`（最小堆定时器）。它做两件事：

1. **跑协程**：内部线程池，每个线程在 `idle()` 里 epoll_wait，有事件就唤醒对应协程；
2. **管 fd 事件**：`addEvent(fd, READ/WRITE)` 注册到 epoll，事件就绪时把对应协程 `Ready` 重新入队。

关键数据结构（`iomanager.h:22`）：

```cpp
struct FdContext {
    struct EventContext {
        Scheduler* scheduler = nullptr;
        Fiber::ptr fiber;            // 谁在等这个事件
        std::function<void()> cb;
    };
    EventContext read, write;
    int fd = 0;
    Event events = NONE;             // 当前注册的事件 bitmask
    Mutex mutex;
};
```

`addEvent`（`iomanager.cc:102`）里有一行**项目踩过的最坑的断言**：

```cpp
if(SYLAR_UNLIKELY(fd_ctx->events & event)) {
    SYLAR_LOG_ERROR(g_logger) << "addEvent assert fd=" << fd << " event=" << event;
    SYLAR_ASSERT(!(fd_ctx->events & event));   // 触发 → abort() → 进程崩
}
```

意思是：**同一个 fd 不能注册两次同样的 event**。如果两个协程并发对同一 fd `addEvent(WRITE)`，第二个直接让进程 abort。这是 gate 发送队列设计的根因（第 5.2 节）。

注释里还提到另一个修复：

```cpp
// 压力测试段错误bug: 在复杂的多线程调度框架下, 强制要求发起调用的协程必须是 EXEC 是过于苛刻且没有必要的。
// 只要协程没死 (TERM / EXCEPT) 并且处于可挂起状态就行。注释掉下面这行断言。
// SYLAR_ASSERT(event_ctx.fiber->getState() == Fiber::EXEC);
```

——压测时这个过严的断言会误杀，得放开。

## 1.4 Hook：把阻塞 IO 变协程让出（最关键）

这是整套模型的心脏。如果不 Hook，业务代码里写 `read(fd)` 就会**真的阻塞线程**，整个 IOManager 卡死。

`hook.cc` 用 `dlsym(RTLD_NEXT, ...)` 拿到系统库原函数指针，再定义同名符号"覆盖"libc：

```cpp
#define HOOK_FUN(XX) \
    XX(sleep) XX(usleep) XX(nanosleep) \
    XX(socket) XX(connect) XX(accept) \
    XX(read) XX(readv) XX(recv) XX(recvfrom) XX(recvmsg) \
    XX(write) XX(writev) XX(send) XX(sendto) XX(sendmsg) \
    XX(close) XX(fcntl) XX(ioctl) XX(getsockopt) XX(setsockopt)
```

被 Hook 的 `read/write/recv/send/...` 都走 `do_io` 模板（`hook.cc:82`）：

```cpp
template<typename OriginFun, typename... Args>
static ssize_t do_io(int fd, OriginFun fun, const char* hookname,
                     uint32_t event, int timeout_so, Args&&... args) {
    // 1. 没 hook / 非 socket / 用户显式 nonblock → 直接调原函数
    if(!t_hook_enable) return fun(fd, forward<Args>(args)...);
    FdCtx::ptr ctx = FdMgr::GetInstance()->get(fd);
    if(!ctx || !ctx->isSocket() || ctx->getUserNonblock())
        return fun(fd, forward<Args>(args)...);

retry:
    ssize_t n = fun(fd, forward<Args>(args)...);     // 先试一次（fd 已被设 nonblock）
    while(n == -1 && errno == EINTR) n = fun(fd, ...);

    if(n == -1 && errno == EAGAIN) {
        // 没数据 → 注册 epoll, 让出协程
        iom->addEvent(fd, event);                     // 把当前 fiber 挂到 fd 的 event
        Fiber::YieldToHold();                          // ★ 让出, 线程去跑别的协程
        // 唤醒回来: epoll 已就绪
        goto retry;
    }
    return n;
}
```

理解这段代码就理解了整个 sylar：
- **业务代码里看起来在阻塞 `read`，实际只是让出协程**，线程立刻去跑别的协程；
- 等 epoll 通知 fd 可读了，IOManager 把这个协程重新 `Ready`，业务代码从 `read` 返回继续跑；
- 所以 sylar 程序看起来像同步代码，实际是异步 IO。

**Hook 是把双刃剑**：它把任何 libc 同步 IO 都"协程化"，包括 `mysql_real_query`、`redisCommand`、`connect`、`gethostbyname`……但 Hook 也带来风险——比如本会话踩的 `RpcChannelPool × hook` 死锁、gRPC 静态初始化期 close 解引用 NULL（第 13 章）。

## 1.5 时序铁律：栈大小 + 必须先加载配置

`fiber.stack_size` 是 Config 配置项（`fiber.cc:20`）。它有个**铁律**：

```cpp
m_stacksize = stacksize ? stacksize : g_fiber_stack_size->getValue();
```

读的是 `g_fiber_stack_size->getValue()` 的**当前值**。所以必须在创建第一个协程**之前**调 `Config::LoadFromYaml`，否则新建协程都用默认值 1MB。

`service_base.cc:166` 严格遵守了这个时序：

```cpp
// 加载 sylar 全局配置(如 fiber.stack_size)。
// 必须在 IOManager(创建 fiber) 之前调用, 否则栈大小不生效。
if(!confPath.empty()) {
    YAML::Node root = YAML::LoadFile(confPath);
    sylar::Config::LoadFromYaml(root);
}
```

login.yml 里 `fiber.stack_size: 2097152`（2MB）能生效，靠的就是这个时序。

## 1.6 小结

读完这一章，你应该建立这几个直觉：
1. **协程是用户态的**，靠主动让出（Hook + YieldToHold）实现并发；
2. **协程栈很浅（1MB）**，深调用链要小心爆栈；
3. **同一个 fd 不能并发 addEvent**，否则断言 abort；
4. **`fiber.stack_size` 配置必须在第一个协程之前生效**；
5. 业务代码看起来同步，实际被 Hook 改写成事件驱动。

这些直觉会在后面的每一章里反复出现。

---

# 第 2 章 RPC 框架（etcd + traceId 透传）

> 文件：`sylar/rpc/`（`etcd_client` / `rpc_provider` / `rpc_channel` / `rpc_channel_pool` / `rpc_controller` + `rpcheader.proto`）

sylar 自带的 RPC 框架。服务端叫 `RpcProvider`，客户端叫 `RpcChannel`，注册发现用 etcd v3，协议基于 protobuf `cc_generic_services = true`。本章重点讲本会话新增的 **traceId 端到端透传**。

## 2.1 为什么从 ZooKeeper 迁到 etcd

v0.4 用 ZooKeeper 做服务发现。v1.0 迁到 etcd，原因（写在 `etcd_client.h:16`）：
- etcd v3 的 **lease + KeepAlive** 天然对应"服务存活=键存活"，比 ZK 临时节点语义更清晰；
- etcd 的 **Watch 是服务端推送**（gRPC stream），不需要轮询；
- etcd-cpp-apiv3 生态更现代（gRPC 底层）。

## 2.2 三层封装（PImpl 隔离 C++17）

`etcd_client.h` 用 PImpl 把 etcd/gRPC 的 C++17 头隔离在 `.cc`：

```cpp
class EtcdClient {
    bool put(const std::string& key, const std::string& value, int64_t lease_id = 0);
    bool get(const std::string& key, KV& out);
    bool getPrefix(const std::string& prefix, std::vector<KV>& out);
    int64_t leaseGrant(int ttl);           // 申请租约
    int64_t leaseKeepalive(int64_t id);    // 续租
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;          // PImpl
};
```

好处：core/net/http/util 等模块**完全不需要 include etcd/gRPC 头**，框架主体保持 C++11 干净。

三个角色：

| 类 | 职责 |
|----|------|
| `EtcdClient` | KV/Lease 原语（put/get/getPrefix/del/leaseGrant/keepalive） |
| `EtcdRegistrar` | 服务注册（RAII，每服务申请租约+绑定键，KeepAlive 自动续租，失败自愈重注册） |
| `EtcdWatcher` | 服务发现监视（etcd::Watcher 前缀推送，无需轮询） |

**注册键约定**：`/{ServiceName}/{Method}` → `ip:port`，与原 ZK 方案一致。

## 2.3 服务端 RpcProvider

`RpcProvider : public sylar::TcpServer`。核心是 `notifyService` + `run` + `handleClient`。

**`notifyService`**（`rpc_provider.cc:40`）——用 protobuf 反射枚举服务的所有方法：

```cpp
void RpcProvider::notifyService(Service* service) {
    const ServiceDescriptor* serviceDesc = service->GetDescriptor();
    std::string serviceName = serviceDesc->name();
    for(int i = 0; i < serviceDesc->method_count(); ++i) {
        const MethodDescriptor* methodDesc = serviceDesc->method(i);
        info.methodMap.insert({methodDesc->name(), methodDesc});
    }
    m_serviceMap.insert({serviceName, info});
}
```

**`run`**（`rpc_provider.cc:57`）——绑定端口后把每个方法注册到 etcd（绑租约 + KeepAlive）：

```cpp
m_registrar = std::make_shared<EtcdRegistrar>(m_etcdEndpoint);
for(auto& service : m_serviceMap)
    for(auto& method : service.second.methodMap) {
        std::string method_path = "/" + service.first + "/" + method.first;
        info.key = method_path; info.value = m_advertise; info.ttl = m_leaseTtl;
        m_registrar->registerService(info);
    }
```

### 2.3.1 **§6 改动**：handleClient 构造 controller 注入 traceId

这是本会话的重要改动之一。原来 `handleClient` 调 `service->CallMethod` 时传的 controller 是 **`nullptr`**，impl 端拿不到任何 trace 信息。

新版（`rpc_provider.cc:155-165`）改成构造一个真实的 `RpcController`，把 header 里的 traceId 注入进去：

```cpp
// §6: 构造 RpcController 实例, 把 traceId 注入, 供 impl 端读取用于日志关联。
// CallMethod 同步执行, done 内同步 sendResponse, 返回后立即 delete。
sylar::rpc::RpcController* ctrl = new sylar::rpc::RpcController();
if(!traceId.empty()) ctrl->SetTraceId(traceId);
service->CallMethod(methodDesc, ctrl, request, response, done);
delete request;
delete ctrl;
```

注意 traceId 在前面解析 header 时就提出来了（`rpc_provider.cc:106-115`）：

```cpp
std::string traceId;   // §6 提取 traceId 供注入 controller
if(rpcHeader.ParseFromString(rpcHeaderStr)) {
    serviceName = rpcHeader.service_name();
    methodName = rpcHeader.method_name();
    argsSize = rpcHeader.args_size();
    traceId = rpcHeader.trace_id();
}
```

日志也跟着 traceId 走：

```cpp
if(traceId.empty()) {
    SYLAR_LOG_DEBUG(g_rpclogger) << "recv rpc: service=" << ...;
} else {
    SYLAR_LOG_INFO(g_rpclogger) << "[" << traceId << "] rpc in: service=" << ...;
}
```

`handleClient` 末尾还有一个**关键注释**解释为什么是"一连接一请求"短连接：

```cpp
// 一连接一请求: 处理完即退出循环, 由 TcpServer 回收连接。
// (曾尝试 keep-alive 循环复用连接, 但高频推送下出现 stack smashing,
//  根因是连接复用时数据流错位风险; 回退为短连接最稳妥。)
break;
```

## 2.4 客户端 RpcChannel

`RpcChannel : public google::protobuf::RpcChannel`，实现 `CallMethod`。两种构造（`rpc_channel.h:20-22`）：

```cpp
RpcChannel(const std::string& etcdEndpoint = "http://127.0.0.1:2379");    // 短连接
RpcChannel(const std::string& etcdEndpoint, RpcChannelPool* pool);        // 连接池
```

### 2.4.1 **§6 改动**：CallMethod 发送侧注入 traceId

新版在序列化 RpcHeader 时，从 controller 读 traceId 写入（`rpc_channel.cc:55-64`）：

```cpp
sylar::rpc::RpcHeader rpcHeader;
rpcHeader.set_service_name(serviceName);
rpcHeader.set_method_name(methodName);
rpcHeader.set_args_size(argsSize);
// §6: 从 controller 透传 traceId(gate 端 SetTraceId 注入, 中间服务继续传递)。
// dynamic_cast 因为入参 controller 类型是 google 基类 RpcController。
if(controller) {
    auto* sctrl = dynamic_cast<sylar::rpc::RpcController*>(controller);
    if(sctrl && !sctrl->TraceId().empty()) rpcHeader.set_trace_id(sctrl->TraceId());
}
```

为什么必须 `dynamic_cast`？因为 protobuf `CallMethod` 签名里的 controller 是 google 基类 `google::protobuf::RpcController*`，而 sylar 加了自己的 `m_traceId` 字段。要拿到它必须向下转型。

### 2.4.2 三个边界保护（踩过的坑）

`CallMethod` 读响应时有三个坑（`rpc_channel.cc:140-169`），都是实战教训：

**边界保护 1——响应大小异常**（防 keep-alive 数据错位崩溃）：

```cpp
if(respSize > 64u * 1024 * 1024) {
    SYLAR_LOG_ERROR(g_rpclogger) << "rpc response size abnormal: " << respSize
        << ", connection data misaligned, discarding";
    controller->SetFailed("response size abnormal");
    if(guard) { guard->release(); sock->close(); delete guard; }
    return;
}
```

**BUG-5 修复**——proto3 全默认值消息序列化为 0 字节：

```cpp
// BUG-5: proto3 全默认值消息序列化为 0 字节(如 ResultResp{SUCCESS=0,msg=""}),
// readFixSize(buf,0) 返回 0 会误判失败。0 字节响应是合法空消息。
if(respSize > 0 && ss.readFixSize(&respStr[0], respSize) <= 0) { ... }
```

这个特别隐蔽：proto3 里全默认值的消息（比如 `ResultResp{result=SUCCESS=0, msg=""}`）序列化后是 **0 字节**。`readFixSize(buf, 0)` 返回 0 会被当成失败。

**读失败时淘汰连接**：Guard release 后丢弃，不回池。

## 2.5 RpcController：**§6 加 m_traceId**

`rpc_controller.h` 本会话新增了一个字段（`rpc_controller.h:29-35`）：

```cpp
// §6 traceId: caller(gate)经 SetTraceId 注入, RpcChannel 序列化时读取并写入 RpcHeader.trace_id;
//             callee(RpcProvider)解析 RpcHeader 后通过 SetTraceId 注入到本 controller,
//             impl 端通过 TraceId() 读取用于日志关联。空串=未启用。
void SetTraceId(const std::string& id) { m_traceId = id; }
const std::string& TraceId() const { return m_traceId; }

private:
    bool m_failed;
    std::string m_errText;
    std::string m_traceId;   // §6 调用链追踪 ID
```

`Reset()` 也要清掉：

```cpp
void RpcController::Reset() {
    m_failed = false;
    m_errText = "";
    m_traceId.clear();
}
```

## 2.6 Rpcheader.proto：**§6 加 trace_id 字段**

```protobuf
message RpcHeader {
    bytes service_name = 1;
    bytes method_name = 2;
    uint32 args_size = 3;
    // §6 调用链追踪 ID。caller 在 RpcChannel 序列化时从 controller.TraceId() 读取;
    // callee 在 RpcProvider::handleClient 解析后注入到新建 controller, 供 impl 端日志关联。
    // 空=未启用, 不影响 wire(向后兼容旧 caller/callee)。
    string trace_id = 4;
}
```

关键设计：**trace_id 是字段 4，向后兼容**。旧 caller 不写这个字段，proto3 默认空串；旧 callee 不读这个字段，也无所谓。所以本次升级**不需要全集群同时重编译**——可以逐服务灰度。

## 2.7 RpcChannelPool：连接池 + 发现缓存

`RpcChannelPool` 给高频调用路径用（`rpc_channel_pool.h:33`）。两个能力：

1. **per-host socket 池**（mutex+condvar，connect 在锁外执行以便让出协程）+ RAII `Guard` 自动归还；
2. **TTL 服务发现缓存**（miss/过期才查 etcd，消除每次 RPC 都建 etcd gRPC 连接）。

```cpp
class RpcChannelPool {
    sylar::Socket::ptr acquire(const std::string& ip, uint16_t port);   // 借
    void release(const std::string& ip, uint16_t port, sylar::Socket::ptr sock);  // 还
    bool getDiscovery(const std::string& method_path, std::string& ip, uint16_t& port);
    void putDiscovery(const std::string& method_path, const std::string& ip, uint16_t port);
    class Guard { /* RAII 归还 */ };
};
```

**使用策略的真实取舍**（`gate_main.cc:28`、`battle_main.cc:33`）：
- gate/login 用**短连接**——连接池曾因 sylar hook 下 fd 复用竞态回退（第 13 章）；
- lobby/battle 对 gate 高频推送用 `RpcChannelPool(etcd, 8)`，**但仅在推送闭包里用**，对外暴露的转发 channel 仍是短连接。

`rpc_channel.cc:117` 加了 64MB 边界保护防止 keep-alive 数据错位崩溃。

## 2.8 traceId 端到端怎么走

把上面几节串起来：

```
[1] 客户端发 MSG_SHOOT 到 gate
[2] gate onShoot 调 newCtrl() 构造 RpcController, SetTraceId("g1-a42-m32-7")
[3] gate 调 BattleService::Shoot(channel, ctrl, ...)
[4] RpcChannel.CallMethod 读 ctrl->TraceId() 写入 RpcHeader.trace_id
[5] 包发出: [4B header_size][RpcHeader{...trace_id="g1-a42-m32-7"}][args]
[6] battle RpcProvider.handleClient 解析 RpcHeader, 取 traceId
[7] new RpcController(); ctrl->SetTraceId(traceId);
[8] service->CallMethod(methodDesc, ctrl, ...), impl 端日志带 [g1-a42-m32-7]
```

如果 battle 内部还要再调 data（比如落库），同一个 traceId 会继续透传——因为 RpcChannel 在序列化时读的就是 controller 里的 traceId，impl 只要把收到的 controller 传给下游 RPC 即可。

## 2.9 小结

读完这章你应该理解：
1. RPC 的线路协议是 `[4B header_size][RpcHeader][args]`，RpcHeader 是元数据（service/method/args_size/**trace_id**）；
2. **traceId 全链路透传**靠三件事：proto 加字段 + controller 加方法 + provider/channel 注入；
3. 短连接是 stack smashing 教训的妥协，连接池只在 lobby/battle 推送闭包里用；
4. proto3 全默认值消息序列化为 0 字节，是个特别坑的边界。

---

# 第 3 章 五服务微服务架构

把第 1、2 章的工具组合起来，就得到了 5 个独立可执行的业务服务。本章先讲整体调用关系，后面几章再逐个细讲。

## 3.1 调用关系图

```
                       客户端 (Unity / C#)
                          │ TCP (frame: [len][msg_id][pb])
                          ▼
                ┌─────────────────────────┐
                │         gate            │   8100 (TCP) + 8101 (RPC)
                │  - 帧解析/msg_id 分发   │
                │  - session 管理         │
                │  - 实现 PushService     │ ← lobby/battle 反推
                └────────┬────────────────┘
                         │ RPC (短连接)
       ┌─────────────────┼──────────────────┐
       ▼                 ▼                  ▼
   login             lobby                battle
   8200/8201         8300                 8400
   - ValidateToken   - RoomList/Create    - EnterBattle
   - Register        - JoinRoom/Ready     - Shoot/Move/Pass
   - Login           - Chat/Friend        - 回合调度 + 物理
       │                 │                  │
       └─────────────────┼──────────────────┘
                         │ RPC (短连接)
                         ▼
                       data  8500
                       - MySQL (账号/档案/战绩/好友/聊天)
                       - Redis (token → accountId)
```

## 3.2 服务发现与反向推送

每个服务在 etcd 注册 `/ServiceName/Method` → `ip:port`（绑租约 + KeepAlive）。任何调用方都先查 etcd 拿到目标实例再 RPC。

**反向推送**是个特别的设计：battle/lobby **不持有客户端 Socket**，它们持有一个 `RoutingHandle`：

```cpp
struct RoutingHandle {
    uint64_t accountId = 0;
    uint64_t gatewayId = 0;
};
```

battle 要推消息给玩家时，调 gate 暴露的 `PushService.NotifyClient(accountId, msg_id, payload)` RPC，gate 收到后查本地 session 表，把消息塞进对应 session 的发送队列。这相当于**反向 RPC**：通常 RPC 是客户端调服务端，这里是服务端调客户端所在的网关。

`PushService` 有三个方法：

```protobuf
service PushService {
  rpc NotifyClient(NotifyReq) returns (ResultResp);       // 单推
  rpc NotifyClients(NotifyManyReq) returns (ResultResp);  // 批量推(房间广播)
  rpc NotifyAllOnline(NotifyReq) returns (ResultResp);    // 世界广播
}
```

`NotifyClients` 是后加的批量推送：原来 4 人房间广播要 4 次 RPC（4 个 fiber），现在 1 次 RPC 搞定，是降 RPC 频率的根本手段。

## 3.3 为什么这样拆

- **gate 独立**：客户端唯一入口，便于做限流/鉴权/协议转换。多 gate 横向扩展时客户端只需指向 LB；
- **login 独立**：HTTP 明文入口（注册/登录）和 RPC（token 校验）分离，安全边界清晰；
- **lobby 独立**：房间逻辑跟战斗逻辑差异大，分进程避免相互影响（房间 bug 不该崩战斗）；
- **battle 独立**：CPU 密集（物理仿真），便于单独扩容；
- **data 独立**：唯一持久层，便于做缓存/分库/读写分离。

## 3.4 启动模式（ServiceRunner）

所有服务的 `main` 都长一个样，靠 `service_base` 抽象公共逻辑（`service_base.h:96`）：

```cpp
ddt::ServiceRunner runner("gate");
runner.init(argc, argv);      // 解析 -c conf.yml, 加载 sylar 全局配置
sylar::IOManager iom(4, true, "gate");
iom.schedule([&](){ /* 起服务 */ });
runner.installSignal();        // SIGINT/SIGTERM 双段式
return 0;                      // iom 在 main 线程跑
```

**信号双段式**（`service_base.cc:183`）：首次 SIGINT 触发 `IOManager::stop()` 优雅退出，第二次 `_exit(0)` 强退——防止优雅退出卡死时无法终止。

```cpp
static void onSignal(int sig) {
    if(++g_force_exit >= 2) _exit(0);   // 第二次强制退出
    std::cerr << "[shutdown] graceful, press again to force exit\n";
    sylar::IOManager::GetThis()->stop();
}
```

## 3.5 配置体系：**§2 环境变量覆盖**

`ServiceConfig::load`（`service_base.cc:28`）从 YAML 读 db/redis/heartbeat/physics/game/combat/player 段。本会话在末尾加了一段**环境变量覆盖**（`service_base.cc:102-113`）：

```cpp
// §2: 环境变量覆盖(优先级 > yml)。生产用环境变量注入敏感字段(如 DDT_DB_PASS),
// 避免明文密码落 yml。仅在变量已设时覆盖, 未设则保留 yml/默认值。
// 注: 启动期主线程串行读取, libc getenv 线程安全(setenv 才非线程安全)。
if(const char* p = getenv("DDT_DB_HOST"))      db_host = p;
if(const char* p = getenv("DDT_DB_PORT"))      db_port = std::atoi(p);
if(const char* p = getenv("DDT_DB_USER"))      db_user = p;
if(const char* p = getenv("DDT_DB_PASS"))      db_pass = p;
if(const char* p = getenv("DDT_DB_NAME"))      db_name = p;
if(const char* p = getenv("DDT_DB_POOL_SIZE")) db_pool_size = std::atoi(p);
if(const char* p = getenv("DDT_REDIS_HOST"))      redis_host = p;
if(const char* p = getenv("DDT_REDIS_PORT"))      redis_port = std::atoi(p);
if(const char* p = getenv("DDT_REDIS_POOL_SIZE")) redis_pool_size = std::atoi(p);
```

学到的点：
1. **优先级**：环境变量 > yml > 默认值。这跟 12-factor app 一致（配置优先级最高的是环境变量）；
2. **线程安全**：`getenv` 是线程安全的（多线程同时只读），不安全的是 `setenv`（会 realloc 全局表）；
3. **只在已设时覆盖**：未设环境变量保留 yml/默认值，避免空串误清配置；
4. **配置层级**：`advertiseAddr()` 把 `0.0.0.0` 替换为 `127.0.0.1`——注册到 etcd 的地址不能是通配地址。

---

# 第 4 章 协议设计（proto + frame + msg_id）

通信协议分两层：**外层帧**（4B 长度 + 2B msg_id + payload）+ **内层 payload**（protobuf 字节）。两个为什么分开讲清楚。

## 4.1 帧格式（frame.h）

客户端 ↔ gate 的 TCP 帧格式（大端序）：

```
[ 4B length ][ 2B msg_id ][ protobuf payload ]
length = 2 + payload.size()    (不含自身 4 字节)
```

`Frame::encode(msg_id, payload)` 拼包，`Frame::decode(...)` 拆包。**手动实现大端读写**，不用 `htonl`（跨平台、去依赖）：

```cpp
void writeBE32(char* p, uint32_t v) {
    p[0] = (v >> 24) & 0xFF; p[1] = (v >> 16) & 0xFF;
    p[2] = (v >> 8) & 0xFF;  p[3] = v & 0xFF;
}
```

gate 解帧时（`gate_server.cc:251-262`）还有一个边界保护：

```cpp
if(length < 2 || length > 16 * 1024 * 1024) {
    SYLAR_LOG_WARN(g_logger) << "gate: bad frame len " << length;
    break;
}
```

防止恶意/异常包触发巨值内存分配。

## 4.2 MsgId 枚举（msg_id.h）

```cpp
enum MsgId : uint16_t {
    MSG_LOGIN = 1, MSG_LOGIN_RESP = 2, MSG_REGISTER = 3, ...
    MSG_SHOOT = 32, MSG_SHOOT_RESULT_NOTIFY = 33,
    MSG_MOVE = 34, MSG_MOVE_NOTIFY = 35, ...
    MSG_ERROR = 90, MSG_HEARTBEAT = 91, MSG_HEARTBEAT_RESP = 92,
};
```

**为什么不直接用 proto 生成的枚举？** 注释解释：帧层不想依赖 proto 头。gate 只需要数字 msg_id 做路由，不需要 proto 定义。所以手写一份，和 `gate.proto` 的注释表手动同步。

## 4.3 内层 proto（3 个文件）

`src/proto/` 下 3 个 `.proto`（`syntax="proto3"`, `package ddt`）：

- **common.proto**：共享枚举（Result / TeamSide / Gender / ChannelType）+ 共享消息（PlayerState / PhysicsParamsMsg / RoomInfo）
- **gate.proto**：客户端 ↔ 网关的所有消息（战斗消息、房间消息、聊天消息），帧格式和登录流程都在文件头注释
- **rpc.proto**：服务间 RPC（5 个 Service），`option cc_generic_services = true`

### 4.3.1 **§13 改动**：rpc.proto 加 PlayerStat

战绩从"两玩家写死字段"扩展为"任意人数"。proto 端加了 `PlayerStat` 子消息（`rpc.proto:87-99`）：

```protobuf
// §13 多人战绩(向后兼容扩展)。
//   旧字段 player_ids(1)/winner_ids(2)/duration(3) 保留, 兼容旧 caller。
//   新字段 players(4)/winning_team(5) 优先使用, 支持任意人数 + 每位玩家明细。
//   data 端 SaveGameRecord: players 非空走新路径(主子表), 否则回退旧路径(前 2 玩家)。
message PlayerStat {
  uint64   account_id   = 1;
  TeamSide team         = 2;
  bool     is_winner    = 3;
  int32    damage_dealt = 4;   // 本局累计造成伤害(BattleRoom::onShoot AOE 内累加)
}
message GameRecordReq {
  repeated uint64 player_ids = 1;     // 兼容旧版
  repeated uint64 winner_ids = 2;     // 兼容旧版
  int32 duration = 3;
  repeated PlayerStat players = 4;    // §13 新增: 多人明细(优先)
  TeamSide winning_team = 5;          // §13 新增: 胜方队伍(0=RED 1=BLUE)
}
```

学到的：
- proto3 加字段是**非破坏性**的（旧 caller 不填这个字段就是默认值），所以可以平滑扩展；
- `damage_dealt` 这种业务统计字段（不是协议交换字段）直接放 proto，省得另外建一份缓存。

### 4.3.2 关键战斗消息（gate.proto）

```protobuf
message RoomReadyNotify {   // 战斗开局下发
  uint32 room_id; repeated PlayerState players; float wind; uint64 first_turn_id;
  PhysicsParamsMsg physics_params; float max_move_per_turn;
  string map_name; bytes terrain_bitmap;   // 2D 体素位图（替代旧 height_map）
  int32 terrain_w, terrain_h;
}
message ShootResultNotify {
  uint64 shooter_id; int32 angle; double force; float wind;
  float hit_x, hit_y; bool hit_player; uint64 hit_account_id;
  int32 damage; repeated PlayerState updated_players;
  enum DamageType { NORMAL=0; CRITICAL=1; BLOCK=2; }
  DamageType damage_type; float start_x, start_y; int32 direction; bool is_fly; int32 weapon_id;
}
```

注意 v1.0 **不用 `oneof payload` 聚合**（v0.4 用过），改成 **msg_id 外挂路由**——每个 msg_id 唯一映射一种消息类型，帧层不依赖 proto 头。

## 4.4 五个 Service 一览

`rpc.proto` 声明 5 个 Service（4 个游戏逻辑 + 1 个推送）：

| Service | 方法数 | 关键方法 |
|---------|--------|---------|
| `LoginService` | 3 | ValidateToken / Register / Login |
| `DataService` | 16 | CreateAccount / GetAccount* / SaveToken / LoadToken / SaveGameRecord / ... |
| `LobbyService` | 11 | RoomList / CreateRoom / JoinRoom / Ready / SwitchTeam / Chat / ... |
| `BattleService` | 6 | EnterBattle / Shoot / Move / Pass / AimBegin / LeaveBattle |
| `PushService`（gate 实现，反向） | 3 | NotifyClient / NotifyClients（批量）/ NotifyAllOnline |

---

# 第 5 章 网关 Gate（集中分发 + 注册式 + 安全删除）

> 文件：`src/server/gate/`，端口 8100（TCP）+ 8101（RPC）

Gate 是客户端唯一的 TCP 入口，也是整个系统**并发最复杂**的服务。本章重点：本会话新增的**注册式分发**和 **newCtrl traceId 注入**。

## 5.1 职责

- 接受 TCP 连接，解析帧 `[4B len][2B msgid][pb]`
- 首包 LOGIN 校验 token（调 LoginService），绑定 accountId
- 按 msg_id 路由到 login/lobby/battle
- 实现 `PushService`（battle/lobby 反推消息回客户端）
- 心跳超时清理

## 5.2 每会话发送队列 + drain 协程（最微妙的设计）

这是整个 gate 最值得学的并发设计。先看问题：

`handleClient` 协程和多个 PushService RPC 协程会**并发往同一个 sock 发数据**。如果某个 send 遇到 EAGAIN，sylar hook 的 `do_io` 会 `addEvent(fd, WRITE)` 然后挂起；此时另一个协程也往同一 fd send，又 `addEvent(WRITE)`——触发第 1 章说的 sylar 断言：

```cpp
// iomanager.cc:119
SYLAR_ASSERT(!(fd_ctx->events & event))   // → abort() → gate 崩溃
```

注释直接写在 ClientSession 里（`gate_server.h:36-44`）：

```cpp
// 发送队列: 保证同一 fd 同一时刻只有一个协程在 send。
// 根因: handleClient 协程与 PushService RPC 协程(多个)并发向同一 sock 发数据时,
// 若某次 send 遇 EAGAIN, hook 的 do_io 会 addEvent(fd, WRITE) 挂起当前协程;
// 第二个协程对同 fd 再 send 也会 addEvent(WRITE), 触发 sylar 的
// SYLAR_ASSERT(!(fd_ctx->events & event))(iomanager.cc:119) → abort() → gate 进程崩溃。
// 入队后由按需启动的 drainAndSend 协程串行消费, 从根本上消除该竞态。
sylar::Spinlock sendMutex;            // 仅保护 sendQueue/sendBusy
std::deque<std::string> sendQueue;    // 已组帧的完整包
bool sendBusy = false;                // 是否有发送协程正在消费队列(去重启动)
```

**解法**：每个会话一个发送队列 + 一个按需启动的 drain 协程，把所有发送**串行化**到单条协程（`gate_server.cc:161-204`）：

```cpp
void sendToSession(ClientSession::ptr s, uint16_t msgId, const std::string& payload) {
    std::string pkt = Frame::encode(msgId, payload);
    bool needStart = false;
    {
        sylar::Spinlock::Lock lk(s->sendMutex);
        s->sendQueue.push_back(std::move(pkt));
        if(!s->sendBusy) {
            s->sendBusy = true;          // 标记已有协程, 后续入队不再启动新协程
            needStart = true;
        }
    }
    if(needStart) {
        sylar::IOManager::GetThis()->schedule([s]() {   // 按需调度
            GateServer::drainAndSend(s);
        });
    }
}

void drainAndSend(ClientSession::ptr s) {
    while(true) {
        std::string pkt;
        {
            sylar::Spinlock::Lock lk(s->sendMutex);
            if(s->sendQueue.empty()) {
                s->sendBusy = false;    // 发空, 允许下次重启
                return;
            }
            pkt = std::move(s->sendQueue.front());
            s->sendQueue.pop_front();
        }
        // send 在锁外(hook 的 do_io 可能 yield, 锁内不可持有太久)
        int64_t rt = s->sock->send(pkt.data(), pkt.size());
        if(rt <= 0) return;
    }
}
```

三个要点：
- `sendBusy` 去重协程启动——**保证同一时刻最多一个协程在 do_io(WRITE)**；
- drain 协程**非常驻**，队列空就退出，下次 sendToSession 再启动；
- send 在锁外，锁内只做纯内存操作。

这是 sylar 协程模型并发 send 的标准范式。

## 5.3 **§15 改动**：注册式分发取代 switch-case

原 gate 的 `handleClient` 是 21 个 switch-case 分支（每个 msg_id 一段），又长又难维护。本会话改成 **`unordered_map<uint16_t, Handler>` 注册式**（`gate_server.h:125-134`）：

```cpp
// §15 注册式分发: 取代原 handleClient 的 21 个 switch-case。
// 已登录(需 accountId != 0)的 handler 注册到 m_handlers;
// 预登录(无需鉴权, 如 LOGIN/REGISTER/HEARTBEAT)的 handler 直接在 handleClient 用 static map 查。
// 新增 msg_id 仅加一行注册。
typedef std::function<void(ClientSession::ptr, const std::string&)> Handler;
void registerHandlers();
std::unordered_map<uint16_t, Handler> m_handlers;
std::unordered_set<uint16_t> m_preAuthMsgs = {
    MSG_LOGIN, MSG_REGISTER, MSG_HEARTBEAT, MSG_LOGOUT, MSG_SET_GENDER
};
```

`registerHandlers` 在构造函数里调一次（`gate_server.cc:46-63`）：

```cpp
void GateServer::registerHandlers() {
    m_handlers[MSG_ROOM_LIST]     = [this](ClientSession::ptr s, const std::string& b){ onRoomList(s, b); };
    m_handlers[MSG_CREATE_ROOM]   = [this](ClientSession::ptr s, const std::string& b){ onCreateRoom(s, b); };
    m_handlers[MSG_JOIN_ROOM]     = [this](ClientSession::ptr s, const std::string& b){ onJoinRoom(s, b); };
    // ... 22 个 handler
    m_handlers[MSG_SHOOT]         = [this](ClientSession::ptr s, const std::string& b){ onShoot(s, b); };
    m_handlers[MSG_MOVE]          = [this](ClientSession::ptr s, const std::string& b){ onMove(s, b); };
    m_handlers[MSG_PASS]          = [this](ClientSession::ptr s, const std::string& b){ onPass(s, b); };
    m_handlers[MSG_AIM_BEGIN]     = [this](ClientSession::ptr s, const std::string& b){ onAimBegin(s, b); };
}
```

`handleClient` 的分发逻辑变得极短（`gate_server.cc:266-293`）：

```cpp
// §15 注册式分发: 预登录白名单 + 已登录 handler 表。
if(m_preAuthMsgs.count(msgId)) {
    static const std::unordered_map<uint16_t, Handler> kPreAuth = {
        {MSG_LOGIN,      [this](ClientSession::ptr s, const std::string& b){ onLogin(s, b); }},
        {MSG_REGISTER,   [this](ClientSession::ptr s, const std::string& b){ onRegister(s, b); }},
        // ...
    };
    auto it = kPreAuth.find(msgId);
    if(it != kPreAuth.end()) it->second(sess, payload);
    else sendError(sess, 401, "not logged in");
} else {
    if(sess->accountId == 0) {
        sendError(sess, 401, "not logged in");
    } else {
        auto it = m_handlers.find(msgId);
        if(it != m_handlers.end()) it->second(sess, payload);
        else SYLAR_LOG_WARN(g_logger) << "gate: unknown msg_id=" << msgId;
    }
}
```

学到的：
1. **预登录白名单** + **已登录 handler 表**两层；预登录的（LOGIN/REGISTER/HEARTBEAT/LOGOUT/SET_GENDER）不需要鉴权，其他都要求 `accountId != 0`；
2. **static const 表避免每次构造**：预登录表用 `static const` 局部变量，只构造一次；
3. **新增 msg_id 仅加一行注册**：扩展成本从"加一段 case + 改分发逻辑"降为"加一行 map 插入"。

## 5.4 **§6 改动**：newCtrl 统一注入 traceId

22 个 `onXxx` handler 都要先构造一个 RPC controller 调下游服务。本会话抽出统一入口 `newCtrl`（`gate_server.cc:32-42`）：

```cpp
// §6 构造带 traceId 的 controller。22 个 onXxx handler 统一用此入口。
std::shared_ptr<sylar::rpc::RpcController> GateServer::newCtrl(
        ClientSession::ptr s, uint16_t msgId) {
    auto ctrl = std::make_shared<sylar::rpc::RpcController>();
    uint64_t seq = m_rpcSeq.fetch_add(1);
    std::string tid = "g" + std::to_string(m_gatewayId)
                    + "-a" + std::to_string(s ? s->accountId : 0)
                    + "-m" + std::to_string(msgId)
                    + "-" + std::to_string(seq);
    ctrl->SetTraceId(tid);
    return ctrl;
}
```

**traceId 格式**：`g<gateId>-a<accountId>-m<msgId>-<seq>`。比如 `g1-a42-m32-7` 表示「gate1 实例 / 账号 42 / MSG_SHOOT(32) / 第 7 个 RPC」。

学到的：
1. **统一入口消除样板代码**：22 个 handler 都不用重复写 `auto ctrl = make_shared<RpcController>(); ...`；
2. **traceId 格式可读**：人工看日志也能立刻定位"哪条调用链"；
3. **m_rpcSeq 原子递增**保证同一 gate 内 seq 唯一，加上 gateId/accountId/msgId 三段前缀，全局基本唯一。

handler 里调用变得极简：

```cpp
void GateServer::onShoot(ClientSession::ptr s, const std::string& body) {
    ShootReq req;
    if(!req.ParseFromString(body)) { sendError(s, 400, "bad shoot req"); return; }
    auto ctrl = newCtrl(s, MSG_SHOOT);
    auto ch = battleChannel();
    ddt::BattleService::Stub stub(ch.get());
    ShootRpcReq rreq;
    // ... 填 req
    ResultResp resp;
    stub.Shoot(ctrl.get(), &rreq, &resp, nullptr);
    // ...
}
```

## 5.5 登录流程（双路径）

`onLogin` 支持两种登录（`gate_server.cc:300-`）：

```cpp
if(!req.token().empty()) {
    // token 登录(重连): 直接 ValidateToken
} else if(!req.name().empty()) {
    // 账密登录(首次): 先 Login(name,pwd) 拿 token, 再 ValidateToken
}
stub.ValidateToken(ValidateTokenReq{token}, &vresp);
// 成功后:
kickExistingSession(accountId);          // 顶号: 先踢旧连接
m_accountToSession[accountId] = s;       // 再装新 session
// 查性别(决定客户端是否弹角色选择 UI)
dstub.GetAccountById(IdReq{accountId});
```

## 5.6 顶号 kickExistingSession

同账号新登录时踢旧连接：**先摘索引，再踢**（`gate_server.cc:133-154`）：

```cpp
void GateServer::kickExistingSession(uint64_t accountId) {
    ClientSession::ptr old;
    {
        MutexType::WriteLock lk(m_sessionMutex);
        auto it = m_accountToSession.find(accountId);
        if(it != m_accountToSession.end()) {
            old = it->second;
            m_accountToSession.erase(it);   // 先摘索引
        }
    }
    if(old) {
        KickNotify n; n.set_code(409); n.set_msg("账号在别处登录");
        sendToSession(old, MSG_KICK_NOTIFY, payload);
        if(old->sock) old->sock->close();
    }
}
```

## 5.7 异步断线清理 delSession（防 OOM）

`delSession` 把 LeaveRoom + LeaveBattle RPC **调度到独立协程**执行，不阻塞 handleClient（`gate_server.cc:90-126`）：

```cpp
// 注释: 曾同步执行, 大规模断线时卡住的 disconnect 协程占满 1MB 协程栈导致 OOM
sylar::IOManager::GetThis()->schedule([self, sess]() {
    uint64_t accountId = sess->accountId;
    // 通知 lobby 把该玩家踢出房间(否则房间永不销毁)
    try {
        auto ch = self->lobbyChannel();
        ddt::LobbyService::Stub stub(ch.get());
        auto ctrl = self->newCtrl(sess, MSG_LEAVE_ROOM);
        // ...
    } catch(...) { ... }
    // 如果在战斗中, 也通知 battle LeaveBattle
    try {
        auto bch = self->battleChannel();
        // ...
    } catch(...) { ... }
});
```

### 5.7.1 安全删除模式（防顶号误删）

从 `m_accountToSession` 删时，**只在"映射值 == 当前 session"时才删**（`gate_server.cc:82-89`）：

```cpp
// 安全删除: 只在 map 里存的就是当前 session 时才删
// (避免顶号场景下旧连接断开误删新连接的索引)
auto it = m_accountToSession.find(s->accountId);
if(it != m_accountToSession.end() && it->second == s) {
    m_accountToSession.erase(it);
}
```

为什么必须这样？考虑这个时序：
1. A 用账号 42 登录 → 装 `m_accountToSession[42] = sessA`；
2. B 用账号 42 登录 → `kickExistingSession` 把 42 的索引换成 sessB；
3. sessA 收到 KICK_NOTIFY，handleClient 退出，触发 `delSession`；
4. 如果不带"值相等检查"，sessA 的清理会把 sessB 的索引误删！

这是经典的**用 CAS 思想做安全删除**。

## 5.8 分片心跳检测

```cpp
static constexpr int SHARDS = 4;
// 每个 tick 只扫 1/4 的会话(按 idx % SHARDS)
// 4 × 10s = 40s < timeout 45s, 不会漏检
// 读锁持有时间降到 1/4
```

原来每个 tick 扫全部 `m_sockToSession`，大在线量时读锁持有久，阻塞 delSession 的写锁。分片后摊还。

**生命周期陷阱**：`tick` 计数器必须按值捕获（`shared_ptr<int>`）进定时器闭包，按引用捕获会悬空。

## 5.9 PushService 实现

gate 在 port 起 TCP，在 port+1 起 RpcProvider 注册为 `PushService`（`gate_main.cc:42-52`）：

```cpp
auto provider = std::make_shared<sylar::rpc::RpcProvider>();
provider->setEtcd(cfg.etcd_endpoint, cfg.etcd_ttl);
provider->setListen((uint16_t)(cfg.port + 1));   // +1 端口
provider->notifyService(gate.get());              // gate 实现 PushService
provider->run();
```

- `NotifyClient`：按 accountId 查 session → sendToSession
- `NotifyClients`：批量（房间广播）
- `NotifyAllOnline`：快照 m_accountToSession 后锁外遍历（世界频道）

---

# 第 6 章 大厅 Lobby（房间状态机 + tryStart 范式）

> 文件：`src/server/lobby/`，端口 8300

管房间/匹配/好友/聊天，**不持客户端连接**，靠注入的 push 闭包反推到 gate。

## 6.1 房间模型

```cpp
struct LobbyRoom {
    uint32_t roomId = 0;
    std::string name;
    std::string mode = "custom";   // "custom" / "match" / "pve"
    bool started = false;
    struct Seat {
        uint64_t accountId = 0;
        std::string name;
        TeamSide team = TEAM_RED;
        bool ready = false;
        uint64_t gatewayId = 0;
        Gender gender = GENDER_NONE;
        int weaponId = 1;
    };
    static constexpr int kMaxSeats = 8;   // 8 席(红蓝各 4)
    Seat seats[kMaxSeats];
    int seatCount = 0;
    std::string mapName = "rainbow";
};
```

`started` 是状态机的核心：未开始 → 大厅列表可见，可加入；已开始 → 从大厅列表消失，转交 battle。

## 6.2 tryStart（lobby→battle 交接，标准范式）

这是「**锁内快照 + 锁外 RPC**」的教科书示例（`lobby_service.cc:348`）。**绝不能持锁跨 RPC**（RPC 调用链深会跨多个 yield 点，持锁就死锁）：

```cpp
void LobbyServiceImpl::tryStart(uint32_t roomId) {
    struct Snap { uint64_t accountId; std::string name; TeamSide team;
                  uint64_t gatewayId; Gender gender; int weaponId; };
    std::vector<Snap> players;
    bool shouldStart = false;
    {
        sylar::RWMutex::WriteLock lk(m_mutex);
        auto it = m_rooms.find(roomId);
        if(it == m_rooms.end()) return;
        auto& room = it->second;
        // 多人战斗: 至少 2 人 + 红蓝各至少 1 人 + 全员准备, 则全量送战斗
        if(room->seatCount < 2 || room->started) return;
        int redReady = 0, blueReady = 0;
        bool allReady = true;
        for(int i = 0; i < room->seatCount; ++i) {
            if(!room->seats[i].ready) { allReady = false; break; }
            if(room->seats[i].team == TEAM_RED) ++redReady; else ++blueReady;
        }
        if(!allReady || redReady < 1 || blueReady < 1) return;
        room->started = true;                  // 标记, 房间从大厅列表消失
        for(int i = 0; i < room->seatCount; ++i) {
            players.push_back({room->seats[i].accountId, ...});   // 锁内拷贝
        }
        shouldStart = true;
    }
    if(!shouldStart) return;

    // 锁外: 构造 EnterBattleReq 调 battle 服
    auto ch = battleChannel();
    ddt::BattleService::Stub stub(ch.get());
    EnterBattleReq req;
    for(const auto& p : players) { /* 填 req */ }
    EnterBattleResp resp;
    stub.EnterBattle(&ctrl, &req, &resp, nullptr);
    if(ctrl.Failed() || resp.result() != SUCCESS) {
        // 回滚: 让房间重新可加入
        sylar::RWMutex::WriteLock lk(m_mutex);
        auto it = m_rooms.find(roomId);
        if(it != m_rooms.end()) it->second->started = false;
        return;
    }
}
```

学到的范式：
1. **锁内只做内存操作**（检查、标记、拷贝快照），不跨 yield；
2. **锁外做 RPC**（etcd + TCP + protobuf 调用链深，必然 yield）；
3. **失败回滚**：RPC 失败要重新加锁把 `started = false`，否则房间永久卡死。

这个范式在 battle 的 `saveGameRecordLocked` 也会再见到（第 10 章）。

## 6.3 异步推送派发

lobby_main 注入 push 闭包时，把 RPC 调用**重调度到独立协程**：

```cpp
setPushFn([&](const RoutingHandle& h, uint16_t msgId, const std::string& payload){
    iom->schedule([=]{
        NotifyReq req; req.set_account_id(h.accountId); req.set_msg_id(msgId); req.set_payload(payload);
        gstub.NotifyClient(&req, &resp);   // etcd+connect+send+recv+protobuf 深度大
    });
});
```

注释解释：RPC 调用链太深，同步执行会撑爆 1MB 协程栈。

## 6.4 批量广播

聊天/房间通知用 `setPushAllFn`（`NotifyAllOnline`）。房间广播用 `BroadcastPushFn`——N 人房从 N 个 RPC 压成 1 个。

## 6.5 聊天多频道

`Chat` 按 channel 分发：WORLD → pushAll；ROOM/TEAM → 找房间找队伍逐个 push。`PrivateChat` 推给目标**和**自己（回显）。

---

# 第 7 章 战斗 Battle（房间锁 + 回合状态机）

> 文件：`src/server/battle/`（`battle_main` / `battle_service` / `battle_room`），端口 8400

这是逻辑最重、注释最密集的服务。

## 7.1 共享 TimeWheel

```cpp
auto tw = std::make_shared<sylar::TimeWheel>();
tw->init(100, 10);   // 100ms 步长(回合超时是秒级), 最长 10 分钟
tw->start(&iom);
```

所有房间的回合定时器都挂在这个轮上（O(1) 调度，优于最小堆）。TimeWheel 的设计详见 sylar-learning-notes.md，这里只说用法。

## 7.2 为什么弃用 Actor 模型（重要注释）

`battle_room.h:36-48` 有一大段注释解释架构演进。原 Actor/邮箱模型把所有操作堆在一个协程栈上，叠加 RPC 推送链导致**爆栈**：

```cpp
// ============================================================
// BattleRoom
// 去掉了 Actor/mailbox 模型(原 Actor 把所有操作压在单个协程栈上,
// 加上 RPC 推送调用链导致栈溢出 stack smashing)。
// 现在用一把 Mutex 保护房间状态, RPC 入口(battle_service)持锁后直接调
// onXxx 方法。每个操作跑在独立的 RPC 协程上(独立栈), 彻底消除栈叠加。
//
// 串行化保证: 同一房间的 RPC 入口争同一把 m_roomMutex, 临界区内操作
// (算伤害/改坐标/组消息)是纯内存操作, 极快。回合制游戏天然串行,
// 持锁期间的其他操作等待是正确行为。
//
// 推送(broadcast)在锁内执行——如果推送 RPC 因网络 yield, 会阻塞同一
// 房间的其他操作, 但不阻塞其他房间(各房间锁独立)。且不栈溢出(独立栈)。
// ============================================================
```

学到的：
1. **Actor 模型 + 协程不是天然搭配**——Actor 把所有消息堆在一个邮箱里串行处理，如果该 Actor 跑在协程上，调用栈会叠加（A 调 B 调 C 都在同一个栈帧里）；
2. **改用 Mutex** 后，每个 RPC 入口是独立协程（独立栈），临界区只覆盖纯内存操作；
3. 回合制游戏天然串行，争锁等待是正确行为。

## 7.3 RPC 入口范式

`battle_service.cc` 每个 handler 都是同一个范式：

```cpp
void BattleServiceImpl::Shoot(::google::protobuf::RpcController*,
        const ShootRpcReq* req, ResultResp* resp, ::google::protobuf::Closure* done) {
    BattleRoom::ptr room;
    {
        sylar::RWMutex::ReadLock lk(m_mutex);              // 全局表锁
        room = roomOfLocked(req->account_id());            // 锁内找房
    }
    if(!room) { /* not in battle */ return; }
    {
        BattleRoom::MutexType::Lock rlk(room->m_roomMutex);  // 房间锁
        room->onShoot(req->account_id(), req->angle(), ...); // 直接调
    }
    resp->set_result(SUCCESS);
    if(done) done->Run();
}
```

两层锁：
- **全局表锁**（`m_mutex`）：保护 `m_rooms` / `m_accountToRoom` 映射，临界区极短；
- **房间锁**（`room->m_roomMutex`）：保护房间状态，临界区跑物理仿真。

## 7.4 Mutex 而非 Spinlock

```cpp
typedef sylar::Mutex MutexType;   // pthread_mutex, 不是 Spinlock
```

注释解释：`onShoot` 临界区要跑物理模拟（≈1500 步循环 + `removeCircle` 遍历数万格子），可能持锁数毫秒。**Spinlock 会烧 CPU 自旋，Mutex 走 futex 睡眠让出线程**。代价：pthread_mutex 不是协程感知的，**锁内绝不能 yield**（broadcast 已异步化，所以锁内不 yield）。

## 7.5 onShoot 物理流程（核心）

```cpp
void onShoot(accountId, angle, force, isFly, weaponId) {
    // 防作弊: angle/force 钳到 [min_angle,max_angle]/[min_force,max_force]
    m_shootLocked = true;

    if(isFly) { /* 纸飞机: 传送非伤害 */ }
    else {
        // 坡度 + 角度合成
        int physAngle = (direction<0) ? (180 - angle + slope) : (angle + slope);
        // 炮口原点(避免在坑里穿墙)
        originX = shooter->x; originY = shooter->y + 60.0;
        // 落点
        auto res = PhysicsEngine::computeHitPoint2D(originX, originY, physAngle, force, m_wind, pp, m_terrain, ...);

        // ★ 必须在挖坑前算伤害!
        // 注释: 用玩家当前位置(爆炸前的脚位)算距离, 否则挖坑后玩家 y 下降,
        // 距落点距离变大, 伤害会被错误地衰减到 0。
        for(每个存活玩家) {
            dmg = calculateDamage(res.hit_x, res.hit_y, p.x, p.y, base_damage, blast_radius);
            // 暴击/格挡: rand()%100 < 15 → CRITICAL(×1.5); <30 → BLOCK(÷2)
            if(dmg > 0) {
                // ... 算 finalDmg
                p.hp = std::max(0, p.hp - finalDmg);
                shooter->damageDealt += finalDmg;   // §13: 累计射手本局伤害
            }
        }

        // 挖坑
        m_terrain.removeCircle(res.hit_x, res.hit_y, blast_radius);
        // 玩家贴地(挖坑后重算脚高)
        for(存活玩家) { p.y = m_terrain.columnHeight(pix); if(p.y<=0){alive=false;hp=0;} }
    }
    broadcast(MSG_SHOOT_RESULT_NOTIFY, ...);
    checkGameOver();           // §13: 异步落库入口
    scheduleNextTurn();        // 延迟切下一回合
}
```

**「挖坑前算伤害」**这个注释是踩过坑的教训——顺序错了伤害会算错（详见第 13.4 节）。

## 7.6 广播的异步派发

`broadcast` 把推送**重调度到新协程**（`battle_room.cc:144-167`）：

```cpp
void BattleRoom::broadcast(uint16_t msgId, const std::string& payload) {
    // 异步推送: 把推送 RPC 投递到 IOManager 的独立协程执行。
    // 不在当前协程(onShoot/onMove 的调用栈)里同步执行 RPC——RPC 调用链
    // (etcd 查询 + TCP connect + send + recv + protobuf)栈深度极大,
    // 压在 onShoot 的协程栈上会撑爆 1MB 栈 → stack smashing。
    if(m_bpush) {
        std::vector<uint64_t> ids;
        for(const auto& p : m_players) ids.push_back(p.accountId);
        auto fn = m_bpush;
        sylar::IOManager::GetThis()->schedule(
            [fn, ids, msgId, payload]() { fn(ids, msgId, payload); });
        return;
    }
    // 兜底: 无批量推送闭包时退回逐人单推(也异步化)
    if(!m_push) return;
    for(const auto& p : m_players) {
        RoutingHandle h(p.accountId, p.gatewayId);
        auto fn = m_push;
        sylar::IOManager::GetThis()->schedule(
            [fn, h, msgId, payload]() { fn(h, msgId, payload); });
    }
}
```

注释直白：在 onShoot 协程栈上直接推（etcd+TCP+protobuf 深度）会爆 1MB 栈；重调度后 onShoot 栈只有几十 KB。

## 7.7 回合循环 + 顺序保证

```cpp
scheduleNextTurn() {
    // 注册 SHOOT_RESULT_DELAY_MS=2300ms 后才 nextTurn
    // 注释: 原 ShootResult 和 TurnStart 走独立异步 RPC 无顺序保证;
    // 延迟 nextTurn 保证 TurnStart 严格在 ShootResult 之后, 消除"开火即切回合"的时序 bug。
    if(m_destroying || m_nextTurnPending) return;
    m_nextTurnPending = true;
    auto self = shared_from_this();
    m_tw->addTimer(SHOOT_RESULT_DELAY_MS, [self]() {
        BattleRoom::MutexType::Lock lk(self->m_roomMutex);
        if(self->m_destroying) return;
        self->m_nextTurnPending = false;
        if(self->m_started) self->nextTurn();
    }, false);
}

nextTurn() {
    m_currentTurnIdx = (idx+1) % size;   // 跳过淘汰玩家
    m_wind = generateWind();
    broadcast(MSG_TURN_START_NOTIFY, ...);
    startTurnTimer();
}
```

**首回合宽限**：`startTurnTimer` 里 `if(m_turnNumber==1) ms += FIRST_TURN_READY_MS(12000)`——客户端要 8-10s 播降落动画+相机入场，不加宽限首回合会超时。

## 7.8 markDestroying（防泄漏永动机）

`LeaveBattle` 拆房时，无账号残留则 `markDestroying()`：

```cpp
// battle_service.cc:184-186
BattleRoom::MutexType::Lock rlk(room->m_roomMutex);
room->markDestroying();
```

```cpp
void markDestroying() {
    m_destroying = true;
    cancelTurnTimer();
}
```

`nextTurn`/`scheduleNextTurn`/`startTurnTimer`/`onTurnTimeout` 全部在开头 `if(m_destroying) return` 短路。

注释解释（`battle_room.h:84-90`）：

```cpp
// 标记房间正在销毁: 置 m_destroying + 取消回合定时器(假设已持房间锁)。
// 由 battle_service 在 m_rooms.erase 前调用, 杜绝孤儿 timer 在 erase 后仍触发
// nextTurn→startTurnTimer 形成泄漏永动机。
```

## 7.9 掉落死亡

挖坑或移动后 `if(p.y <= 0)`（该列被打穿）→ `alive=false, hp=0`（掉出地图）。

---

# 第 8 章 权威物理（解析弹道 + 2D 体素地形）

> 文件：`src/common/ddt_physics.{h,cc}`、`src/common/terrain2d.{h,cc}`

这是整个项目最硬核的部分——弹道是**解析解（闭式解）**，不是数值积分。地形用 **2D 体素位图**，不是 1D 高度图。

## 8.1 服务端权威 + 客户端复放

游戏里炮弹飞 1-3 秒，期间服务端不能等客户端告诉它"打中了"——会作弊。所以**服务端权威**：服务端算落点、算伤害、改地形，客户端拿到结果做视觉回放。

但是！如果只发"结果"给客户端，客户端没法播放飞行轨迹（弹道从哪飞过去？）。所以协议下发的 `ShootResultNotify` 同时包含：
- **结果**：落点 `(hit_x, hit_y)`、伤害、命中玩家
- **输入**：起点 `(start_x, start_y)`、角度、力度、风向、物理参数

客户端拿到输入后**用同一份物理公式本地重算弹道**，画飞行轨迹。这样既权威又视觉好看。

关键：**服务端和客户端用同一条解析曲线**，复放完全一致。所以物理公式必须**确定性的闭式解**，不能是带随机扰动的数值积分。

## 8.2 PhysicsParams（Y 向上坐标系）

```cpp
struct PhysicsParams {
    double air_factor     = 0.89927083;
    double wind_factor    = 5.8709153;
    double gravity_factor = 172.06527992;   // g = 9.8 × gravity_factor ≈ 1686.24
    double force_factor   = 41.0;
};
```

`gravity_factor = 172.06` 不是真实重力，是**调出来的游戏手感参数**——让弹道飞行时间、最远射程"看起来对"。

## 8.3 弹道公式（解析空气阻力解）

每一步（`dt=0.01`，最长 15 秒，约 1500 步）：

```cpp
double af = params.air_factor;        // 0.89927083
double wf = params.wind_factor;       // 5.8709153
double g  = 9.8 * params.gravity_factor;
double ff = params.force_factor;

double rad = angle * PI / 180.0;
double vx0 = ff * force * std::cos(rad);   // 初速度
double vy0 = ff * force * std::sin(rad);

// 闭式解(微分方程 ẍ=-af·ẋ+wind·wf, ÿ=-af·ẏ-g 的积分):
double em = 1.0 - std::pow(E, -af * t);                                       // E^(-af·t) 项
double cx = start_x + vx0*em/af + wind*wf*(t/af - em/(af*af));
double cy = start_y + vy0*em/af - g*(t/af - em/(af*af));
```

数学含义：`em(t) = 1 − E^(−af·t)`。这是带空气阻力的抛体运动的**精确解**，不是 `vx += ax*dt` 的数值积分。

为什么 Y 方程没有风项？风只影响水平 X。

## 8.4 两个版本

- `computeHitPoint2D`：**轻量版**，只算落点（不存轨迹）。服务端用——避免把 1500 点轨迹压到 1MB 协程栈导致栈溢出。
- `computeTrajectory2D`：完整轨迹，给客户端复放。

这个区分是踩过的坑：注释明确写了「服务端用轻量版避免栈溢出」。

## 8.5 碰撞判定

```cpp
int ix = (int)cx, iy = (int)cy;
if(terrain.isSolid(ix, iy)) { /* 命中地形 */ }
if(cx < 0 || cx > worldWidth) { hit_offscreen = true; }
```

精确到格子的体素碰撞。

## 8.6 AOE 伤害（线性衰减）

```cpp
double dist = std::sqrt(dx*dx + dy*dy);
if(dist > blast_radius) return 0;
double ratio = 1.0 - (dist / blast_radius);   // 中心满伤, 边缘 0
return (int)(base_damage * ratio);
```

`base_damage=25`，`blast_radius=50`。

## 8.7 坡度角（中央差分，±3 格）

```cpp
float dy = terrain.columnHeight(ix + 3) - terrain.columnHeight(ix - 3);
return atan2f(dy, 6.0f) * 180.0f / PI;   // 返回度
```

## 8.8 风力

```cpp
return (std::rand() % 201 - 100) / 10.0;   // 范围 [-10, +10]
```

## 8.9 二维体素地形 terrain2d

这是 v1.0 相对 v0.4 的**核心改进**。

### 8.9.1 为什么弃用 1D 高度图

v0.4 用 1D 高度图（每列一个高度值）。问题：爆炸挖坑后「上方平台」无处安放。比如玩家站在悬空平台上，下面被炸个洞，1D 模型只能「整列降低」，把平台也毁了——导致穿地形、误判掉落。

### 8.9.2 2D 位图设计

**1 bit = 1 格（1×1 世界单位）**，`1=实体`，`0=空`：

```cpp
std::string m_bits;   // bitset, 行优先: bit[x + y*m_w]
inline int byteIndex(int x, int y) const { return (x + y * m_w) >> 3; }
inline int bitOffset(int x, int y) const { return (x + y * m_w) & 7; }
```

**内存**：`w*h/8` 字节。标准 3000×420 房 ≈ **158 KB/房**。

### 8.9.3 generate（余弦起伏地形）

```cpp
for(int x = 0; x < m_w; ++x) {
    double nx = (double)(x - m_w/2) / m_w;
    float surfaceH = baseH - std::cos(nx * m_w * 0.003) * (baseH * 0.45f);
    surfaceH = std::max(baseH*0.5f, std::min(baseH*1.6f, surfaceH));   // clamp
    int top = std::min((int)std::ceil(surfaceH), m_h);
    for(int y = 0; y < top; ++y) setSolid(x, y, true);   // 从底填到地表
}
```

### 8.9.4 removeCircle（保留平台的关键）

```cpp
for(int x = minX; x <= maxX; ++x)
    for(int y = minY; y <= maxY; ++y) {
        float dx = x - cx, dy = y - cy;
        if(dx*dx + dy*dy <= r2) setSolid(x, y, false);   // 只清圆内
    }
```

**只清圆内格子，圆外（包括正上方的悬空平台）原样保留**。这是 2D 相对 1D 的根本优势。

### 8.9.5 columnHeight（从顶向下找首个实体格）

```cpp
for(int y = m_h - 1; y >= 0; --y)
    if(isSolid(x, y)) return (float)(y + 1);   // 格的顶部 = y+1
return 0;   // 全空
```

返回 `y+1`（实体格的顶边），等价于旧 `heightMap[x]`。

### 8.9.6 序列化

`bitmap()`/`setBitmap()` 直接暴露原始 `std::string m_bits`，用于 proto `bytes terrain_bitmap` 传输。客户端（`TerrainRenderer.cs`）重建为 `Texture2D`。

## 8.10 小结

权威物理的关键：**确定性的闭式解 + 确定性的体素碰撞**。服务端算什么客户端就复放什么，结果必然一致。地形必须是 2D 体素才能正确表达"挖坑+悬空平台"的二维结构。

---

# 第 9 章 数据层（MySQL ORM + Redis 池化 + 事务）

> 文件：`sylar/orm/` + `src/server/data/`

## 9.1 ORM 模块（sylar 自带）

`sylar/orm/` 是 sylar 自己的 MySQL ORM。设计目标是**自洽、零侵入**：只复用 sylar 的 `Logger`/`Mutex`/`Time2Str`，其余工具（字符串格式化、类型转换、SQL 转义）都在 `orm/util.*` 里自己实现。

分层架构：

```
   ┌──────────── ORM 层 ────────────┐
 Model<T>  →  Query(链式 where/join/…)  →  生成 SQL
   │              │                          │
   └─ Value(取值/赋值)                       ▼
 Database ──► Connection ──► MySQL C API
                ▲
        ConnectionPool(size / get / put / ping 心跳 / auto_reconnect)
```

- **上层** `Model`/`Query`/`Value`：面向业务，链式 DSL + 对象映射
- **中层** `Database`：适配入口，把连接（单条或池）交给 Model
- **底层** `Connection`/`Result`/`PreparedStmt`/`Transaction`：直接封 MySQL C API

### 9.1.1 实体建模：CRTP

`Model<T>` 用**奇异递归模板模式（CRTP）**，派生类只需声明 `table()` 和 `primary_key()`：

```cpp
class User : public sylar::Model<User> {
public:
    User(sylar::Database& db) : Model(db) {}
    std::string table()       const { return "t_user"; }
    std::string primary_key() const { return "id"; }
};
```

CRTP 的好处是链式方法能返回**派生类引用**（`static_cast<T&>(*this)`），于是 `User(db).where(...).one()` 能自然连写。

### 9.1.2 save 的智能判别

`save()` 自动判断 INSERT 还是 UPDATE：**有主键就 UPDATE，没主键就 INSERT**，且 INSERT 后回填自增主键。

### 9.1.3 链式查询 DSL

```cpp
auto all = User(db)
    .select("u.*").alias("u")
    .join("t_fans", "f", "u.id=f.uid")
    .where("f.fid", "=", 4)
    .group("u.age").having("money > 100")
    .order("age asc").limit(10).offset(0)
    .all();
```

支持 `where` 运算符：`=`、`!=`/`<>`、`<`、`<=`、`>`、`>=`、`in`、`not in`、`like`、`not like`、`is`、`is not`、`between`、`not between`。

> 关键设计：每个终结操作（one/all/save/count/...）执行后会**清空查询子句**，避免跨操作污染。

### 9.1.4 连接池（生产可用）

`ConnectionPool` 用 `mutex + condition_variable` 协调借还：

```cpp
// 借出: 池空且未达上限时阻塞等待归还
// 容量上限由 m_created 计数保证
谓词: !m_conns.empty() || m_created < m_maxSize
```

三个健康机制：
1. **借出时健康检查**：空闲较久或曾报错则 `ping()`，失败则 `connect()` 重连，仍失败则丢弃重建——实现「自动修复失效连接」；
2. **后台心跳**：`ping(sec)` 启独立线程定时 ping 所有空闲连接，防服务端超时断开；
3. **池析构安全停止**。

### 9.1.5 事务：用 SAVEPOINT 实现嵌套

MySQL 同一会话**不支持真正的嵌套 BEGIN**。这个 ORM 用 SAVEPOINT 模拟嵌套语义：

```cpp
trx.begin();   // 外层 → BEGIN
{
    trx.begin();   // 内层 → SAVEPOINT sp_1
    // ...
    trx.commit();  // 内层 → RELEASE SAVEPOINT sp_1
}
trx.commit();  // 外层 → COMMIT
```

`begin()` 计数自增，外层走 `BEGIN/COMMIT/ROLLBACK`，内层走 `SAVEPOINT/RELEASE/ROLLBACK TO`。事务对象析构时若未显式结束会整体回滚。**事务不能跨连接（会话）。**

### 9.1.6 防 SQL 注入

1. **拼接路径**：`Value::toSql()` 对字符串做 MySQL 标准转义（`' \ " \0 \n \r \x1a`）；
2. **预处理路径**：`PreparedStmt` 用 `mysql_stmt_bind_*` 参数化，彻底防注入。

## 9.2 DataService：唯一持久层

`src/server/data/` 实现 rpc.proto 的 DataService。所有方法在 RPC 协程内同步执行（ORM 连接池天然阻塞协程）。

### 9.2.1 后端

```cpp
std::shared_ptr<sylar::ConnectionPool> m_pool;   // MySQL 连接池
// §10: Redis 连接池(替代原 void* m_redis + mutex)。
//      借出/归还由 RedisGuard RAII 完成, token 操作并行化。
std::shared_ptr<RedisPool> m_redisPool;
```

注释提到：原方案是 Redis 单连接 + 全局 mutex（token 操作串行），换成连接池后并行化。

### 9.2.2 防注入

```cpp
std::string esc(sylar::Connection* c, const std::string& s) {
    // mysql_real_escape_string, 用 mysql 真转义防注入
}
```

### 9.2.3 **§10 改动**：RedisPool + RedisGuard

`src/server/data/redis_pool.{h,cc}` 是本会话新增的文件。仿 sylar ConnectionPool 模式。

`RedisPool`（`redis_pool.h:23`）：

```cpp
class RedisPool {
public:
    bool create(const std::string& host, int port, int maxSize);
    redisContext* get();                 // 借出(阻塞, 池空且达上限则等)
    void put(redisContext* c, bool healthy);  // 归还(healthy=false 销毁)
    void close();
private:
    redisContext* doConnect();           // 锁外实际建连
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::list<redisContext*> m_idle;
    int m_maxSize = 4;
    int m_created = 0;                   // 空闲 + 借出 的总创建数
    bool m_closing = false;
};
```

注释（`redis_pool.h:14-22`）：

```cpp
// 替代原 data_service 的单连接 + 全局 mutex 方案, token 操作并行化。
// 模式: mutex + condition_variable, get 阻塞(池空且未达上限), connect 在锁外
//       (sylar hook 下 connect 可能 yield, 不能持锁), put 时 notify_one。
// 自愈: 借出方通过 RedisGuard::markUnhealthy() 标记损坏的连接, 归还时池销毁它,
//       下次 get 自动新建(对称 sylar ConnectionPool 的 checkAndFix)。
```

`RedisGuard` RAII 借还（`redis_pool.h:64`）：

```cpp
class RedisGuard {
public:
    explicit RedisGuard(RedisPool* pool);   // 构造时 get
    ~RedisGuard();                          // 析构时 put(根据 m_healthy)
    redisContext* get() const { return m_ctx; }
    operator bool() const { return m_ctx != nullptr; }
    void markUnhealthy() { m_healthy = false; }   // 命令失败时调
private:
    RedisPool* m_pool;
    redisContext* m_ctx;
    bool m_healthy;
};
```

业务侧用法（`data_service.cc:211-221`）：

```cpp
// §10: 经 RedisPool 借连接, token 操作并行化(原方案单连接+全局 mutex 串行)。
//      命令失败时标 markUnhealthy 让池销毁坏连接, 下次 get 自动重建。
void DataServiceImpl::SaveToken(...) {
    if(!m_redisPool) { /* redis unavailable */ return; }
    RedisGuard g(m_redisPool.get());
    if(!g) { /* pool empty */ return; }
    std::string k = "session:" + req->token();
    bool ok = redisSet(g.get(), k, std::to_string(req->account_id()), req->ttl_sec());
    if(!ok || g.get()->err) g.markUnhealthy();   // ★ 失败标坏
    resp->set_result(ok ? SUCCESS : FAIL);
}
```

`get` 内部（`redis_pool.cc:40-65`）：

```cpp
redisContext* RedisPool::get() {
    while(true) {
        std::unique_lock<std::mutex> lk(m_mutex);
        m_cv.wait(lk, [this]() {
            return m_closing || !m_idle.empty() || m_created < m_maxSize;
        });
        if(m_closing) return nullptr;
        if(!m_idle.empty()) {
            redisContext* c = m_idle.front();
            m_idle.pop_front();
            return c;
        }
        // 无空闲但未达上限: 预占一个创建配额, 锁外建连
        ++m_created;
        lk.unlock();
        redisContext* c = doConnect();   // ★ 锁外(connect 可能 yield)
        if(c) return c;
        // 建连失败: 释放配额, notify 让其他等待者重试
        lk.lock();
        --m_created;
        m_cv.notify_one();
    }
}
```

学到的：
1. **借还协调**：mutex + condition_variable，跟 ORM ConnectionPool 一个套路；
2. **锁外做耗时操作**：connect 在锁外（hook 下可能 yield，不能持锁）；
3. **自愈机制**：业务侧 `markUnhealthy()` 标坏，归还时池销毁，下次 get 自动重建；
4. **预占配额**：池空但未达上限时先 `++m_created` 占名额，再去锁外 connect，避免多协程同时 connect 超额。

### 9.2.4 几个诚实的设计捷径（注释里写的）

- **UpdateGender**：若 execute 失败（老库可能没这列），自动 `ALTER TABLE accounts ADD COLUMN gender TINYINT DEFAULT 0` 再重试——向后兼容；
- **GetFriendList**：三表 JOIN，`online=false`（在线状态由 login 服务查 Redis，这里不查）；
- **GetChatHistory**：`ORDER BY id DESC LIMIT n` 然后 `rbegin/rend` 反转成时间正序。

### 9.2.5 Token 存储

Redis 键 `session:<token>` → `accountId`，TTL 86400s（1 天）。

---

# 第 9.5 章 Redis 进阶（在线状态 Set + 世界聊天 Pub/Sub）

> 文件：`src/server/data/redis_pool.{h,cc}`、`src/server/data/data_service.cc`、`src/server/gate/gate_server.cc` / `gate_main.cc`、`src/server/lobby/lobby_service.cc`、`src/proto/rpc.proto`

第 9 章讲完 Redis 连接池后，本会话在它之上又落了两个真业务：**玩家在线状态**（用 Redis Set）和**世界聊天广播**（用 Pub/Sub）。这两个例子正好对照了 Redis 的两种典型用法——"集中存储 + 集中查询" 和 "发布订阅 + 各自分发"。本章把两套链路和设计取舍讲透。

## 9.5.1 背景：两个原功能缺陷

读完代码会发现，v1.0 的 Redis 用得其实很浅——只有 token 存取。两个本应"用 Redis 做"的功能长期缺失：

1. **好友列表的 `online` 字段恒 `false`**。`docs/architecture-analysis.md §8.3 建议 7` 明确写着：「现状：`data_service.cc:351` `online` 恒 false（无人查）。**建议**：gate 维护在线集合，新增 `IsOnline` RPC，`GetFriendList` 批量回填。」——这是项目自己标注的功能缺陷。
2. **世界聊天靠 lobby → gate.NotifyAllOnline RPC**：每次世界消息都要 lobby 主动调一次 RPC，gate 收到后遍历本地所有 session 推送。单 gate 时凑合用，多 gate 部署时**每个 gate 都要 lobby 逐个 RPC 调到**，根本没有"自然分发"。

本会话两块一起补：用 Set 解决在线状态、用 Pub/Sub 解决世界聊天分发。

## 9.5.2 在线状态：Redis Set `online:players`

### 链路

```
   ┌────────────────────────────────────────────────────────┐
   │ gate.onLogin (login ok 之后)                           │
   │    schedule 异步 ──► data.SetOnline                    │
   │                       SADD online:players <accountId>  │
   │                                                        │
   │ gate.delSession (异步断线清理闭包内)                    │
   │    schedule 异步 ──► data.SetOffline                   │
   │                       SREM online:players <accountId>  │
   └────────────────────────────────────────────────────────┘
                              │
                              ▼ (Redis 集中存储)
   ┌────────────────────────────────────────────────────────┐
   │ lobby.GetFriendList  (实际由 data 实现)                 │
   │    SMEMBERS online:players → std::set<uint64_t>        │
   │    填每个 FriendInfo.online = onlineIds.count(fid) > 0 │
   └────────────────────────────────────────────────────────┘
```

### 关键代码位置

- `data_service.cc:625-667`：`SetOnline` / `SetOffline` 实现。两者结构完全对称：借 RedisGuard → `SADD` 或 `SREM` → 失败 `markUnhealthy` → 回 `ResultResp`。
- `data_service.cc:523-548`：`GetFriendList` 内部多了一次 `SMEMBERS online:players`，把返回数组塞进 `std::set<uint64_t> onlineIds`，再对每个好友 `f->set_online(onlineIds.count(fid) > 0)`。
- `rpc.proto:69-71`：DataService 新增两个 RPC：
  ```protobuf
  // 在线状态(Redis Set + Pub/Sub)。gate 登录/断线时调, 其他服务查 GetFriendList 自动回填。
  rpc SetOnline(IdReq) returns (ResultResp);       // 上线: SADD online:players
  rpc SetOffline(IdReq) returns (ResultResp);      // 下线: SREM online:players
  ```
- `gate_server.cc::onLogin` 末尾（`gate_server.cc:397-413`）：登录成功后 schedule 一个独立协程异步调 `SetOnline`，**不阻塞 LOGIN_RESP**。
- `gate_server.cc::delSession` 异步闭包末尾（`gate_server.cc:126-137`）：与 `LeaveRoom` / `LeaveBattle` 一起调 `SetOffline`。

### gate 端：为什么"异步上报"

`gate_server.cc:397-413` 的代码骨架：

```cpp
// 上报在线状态到 Redis(经 data 服务 SADD online:players)。
// 异步: 不阻塞 LOGIN_RESP 响应; 失败仅 warn 不影响登录。
auto self = std::static_pointer_cast<GateServer>(shared_from_this());
uint64_t accId = s->accountId;
sylar::IOManager::GetThis()->schedule([self, accId]() {
    try {
        auto ch = self->dataChannel();
        ddt::DataService::Stub stub(ch.get());
        sylar::rpc::RpcController ctrl;
        ddt::IdReq req;
        req.set_account_id(accId);
        ddt::ResultResp resp;
        stub.SetOnline(&ctrl, &req, &resp, nullptr);
    } catch(const std::exception& e) {
        SYLAR_LOG_WARN(g_logger) << "gate: SetOnline fail account=" << accId
            << " err=" << e.what();
    }
});
```

注意这条协程**在 sendToSession(LOGIN_RESP) 之前 schedule、之后才执行**——LOGIN_RESP 不等它。最坏情况：Redis 挂了，本次 SetOnline 失败，玩家好友列表里他显示"离线"。这是可接受的代价：登录流程不能因为"在线状态"这种附加元数据被拖慢或失败。

`delSession`（`gate_server.cc:126-137`）同理：原本就有 schedule 把 `LeaveRoom` / `LeaveBattle` 两个 RPC 投出去（避免大规模断线撑爆 1MB 协程栈，见 5.7 节），现在再加一个 `SetOffline`。注释也说明了"索引已在上面锁内删完，这里只做 RPC 通知"。

### data 端：为什么用 Set 而不是 String

```cpp
// data_service.cc:642-643 (SetOnline)
redisReply* r = (redisReply*)redisCommand(g.get(), "SADD %s %llu",
    "online:players", (unsigned long long)req->account_id());
// data_service.cc:668-669 (SetOffline)
redisReply* r = (redisReply*)redisCommand(g.get(), "SREM %s %llu",
    "online:players", (unsigned long long)req->account_id());
```

选 Redis Set（`online:players` 是一个 key，所有在线账号 id 是它的元素）而不是"每个玩家一个 `online:<accountId>` 字符串"，理由：

1. **上下线是 O(1)**：`SADD` / `SREM` 单元素都是 O(1)，跟 `SET` / `DEL` 一个数量级；
2. **查全部在线只需一次网络往返**：`SMEMBERS online:players` 一条命令拿到全部在线 id。如果用 String 模型，`GetFriendList` 就要为每个好友单独 `EXISTS online:<fid>`——N 个好友 = N 次 RTT，跟"一次 SMEMBERS 拿全表"完全没法比；
3. **天然适配"集合"语义**：在线状态本质上就是个集合（∈/∉），Set 就是为此而生。

`GetFriendList` 的查询代码（`data_service.cc:526-538`）：

```cpp
std::set<uint64_t> onlineIds;
if(m_redisPool) {
    RedisGuard g(m_redisPool.get());
    if(g) {
        redisReply* r = (redisReply*)redisCommand(g.get(), "SMEMBERS %s", "online:players");
        if(r && r->type == REDIS_REPLY_ARRAY) {
            for(size_t i = 0; i < r->elements; ++i) {
                if(r->element[i]->type == REDIS_REPLY_STRING) {
                    onlineIds.insert((uint64_t)strtoll(r->element[i]->str, nullptr, 10));
                }
            }
        }
        if(r) freeReplyObject(r);
    }
}
// 然后:
f->set_online(onlineIds.count(fid) > 0);   // 查 Redis Set
```

注：拿回来塞进 `std::set<uint64_t>` 是为了 O(log N) 查询，比线性扫描 `std::vector` 快——好友列表通常很小（几十个），但 set 在这里更"语义正确"。

### 为什么 gate 上报、data 集中存

data 是项目里**唯一**持久层（第 9 章），Redis 也归 data 管——没有任何其他服务直接连 Redis。这样设计的取舍：

1. **多 gate 部署天然全局一致**：所有 gate 实例上报到同一个 data，data 写同一个 Redis Set。任何一个 gate 上的玩家上线，所有 gate 的本地 session 查同一个 Set 都能看到他在线——不用做跨 gate 的状态同步；
2. **避免多服务各自连 Redis**：如果每个服务都自己连 Redis，连接管理、配置、错误处理都要重复一遍。data 集中后，其他服务（gate/lobby）只通过 RPC 跟 data 打交道，Redis 细节封在 data 里；
3. **代价**：多一次 RPC 跳转（gate → data → Redis）。但在线状态本来就允许几秒延迟（最坏情况晚一两秒看到好友上线），所以可接受。

### 修复了什么

之前 `GetFriendList` 的 `online` 恒 `false`，文档 `architecture-analysis.md §8.3 建议 7` 明确标注这是功能缺陷。本会话把链路补全：gate 上报 → data 集中 → GetFriendList 查询回填。**`data_service.cc:351`（旧版）那行 `f->set_online(false);` 被替换成 `f->set_online(onlineIds.count(fid) > 0);`**，诚实标注的"未实现"现在变成"已实现"。

## 9.5.3 世界聊天：Redis Pub/Sub `chat:world`

### 链路（替代原 lobby → gate.NotifyAllOnline RPC）

```
[1] 玩家发 CHAT(WORLD) 到 gate
[2] gate 转发到 lobby.Chat
[3] lobby.Chat 检测 CHANNEL_WORLD → data.PublishWorldChat(SaveChatReq)
[4] data.PublishWorldChat:
       ├─ MySQL 持久化到 chat_history(与世界频道原逻辑一致)
       ├─ 构造 ChatNotify proto 序列化为 payload
       └─ redisPublish(*m_redisPool, "chat:world", payload)
                              │
                              ▼ (Redis PUBLISH)
[5] gate 启动时(gate_main.cc:39) 调 startWorldChatSubscriber()
[6] gate 起独立 std::thread → Subscriber.subscribe("chat:world", cb) → loop()
[7] 收到消息: cb 解析 ChatNotify → 快照本地 m_accountToSession
       → 逐个 sendToSession(MSG_CHAT_NOTIFY, payload)
```

每个 gate 实例**各自**订阅 `chat:world`，所以 Redis PUBLISH 一次，**所有 gate 都被自动分发到**。多 gate 部署天然支持，lobby 完全不用关心下游有几个 gate。

### 关键代码位置

- `lobby_service.cc:555-574`：`Chat` 方法的世界频道分支。`CHANNEL_WORLD` 走 `dataStub.PublishWorldChat(...)` 后 `return`，不走下面的 `m_push` 闭包。
- `data_service.cc:679-714`：`PublishWorldChat`。先 MySQL 持久化、再 PUBLISH。
- `gate_main.cc:39`：`gate->startWorldChatSubscriber();`——和 `startHeartbeatCheck` 同级，gate 启动时调用一次。
- `gate_server.cc:910-944`：`startWorldChatSubscriber` 启动一个独立 `std::thread`，循环 subscribe → loop → 重连。
- `redis_pool.h:71-92` + `redis_pool.cc:100-171`：`Subscriber` 类的完整实现。
- `redis_pool.cc:175-205`：`redisPublish` 两个重载。

### data 端：先持久化再 PUBLISH

```cpp
// data_service.cc:679-714
void DataServiceImpl::PublishWorldChat(...) {
    // 先持久化到 MySQL(与世界频道原逻辑一致)
    sylar::Database db(m_pool.get());
    if(db.valid()) {
        sylar::Connection* c = db.getConnection();
        std::string sql = "INSERT INTO chat_history(channel,sender_id,sender_name,message,target_id) VALUES("
            + std::to_string((int)CHANNEL_WORLD) + "," + std::to_string(req->sender_id()) + ",'"
            + esc(c, req->sender_name()) + "','" + esc(c, req->message()) + ","
            + "0)";
        db.execute(sql);
    }

    // 构造 ChatNotify 推给 gate(复用 gate.proto 的 ChatNotify 消息)
    ChatNotify notify;
    notify.set_channel(CHANNEL_WORLD);
    notify.set_sender_id(req->sender_id());
    notify.set_sender_name(req->sender_name());
    notify.set_message(req->message());
    notify.set_timestamp((uint64_t)time(nullptr));
    std::string payload;
    notify.SerializeToString(&payload);

    // PUBLISH chat:world <payload>
    bool ok = redisPublish(*m_redisPool, "chat:world", payload);
    resp->set_result(ok ? SUCCESS : FAIL);
}
```

几个细节：

1. **复用 `ChatNotify` proto**：跟 gate ↔ 客户端的聊天消息同一个类型，gate 收到后不用重新构造，直接当 MSG_CHAT_NOTIFY 的 payload 推给客户端；
2. **持久化在 PUBLISH 之前**：哪怕 Redis 挂了导致没人收到，至少历史能查到。`db.execute(sql)` 失败也不致命（聊天不是关键业务），继续走 PUBLISH；
3. **MySQL INSERT 不开事务**：聊天记录是 append-only 单行写，InnoDB 默认 autocommit 一行就一个事务，不需要显式 BEGIN/COMMIT。

### lobby 端：只改世界频道分支

`lobby_service.cc:555-574`：

```cpp
void LobbyServiceImpl::Chat(...) {
    // 世界频道: 走 data.PublishWorldChat(持久化 + Redis PUBLISH, gate 订阅后 NotifyAllOnline)
    if(req->channel() == CHANNEL_WORLD) {
        auto ch = dataChannel();
        ddt::DataService::Stub dataStub(ch.get());
        sylar::rpc::RpcController ctrl;
        SaveChatReq sreq;
        sreq.set_channel(req->channel());
        sreq.set_sender_id(req->account_id());
        sreq.set_sender_name(req->name());
        sreq.set_message(req->message());
        ResultResp sresp;
        dataStub.PublishWorldChat(&ctrl, &sreq, &sresp, nullptr);
        resp->set_result(ctrl.Failed() ? FAIL : sresp.result());
        if(done) { done->Run(); }
        return;
    }
    // 其他频道(ROOM/TEAM): 原逻辑——data.SaveChat 持久化 + m_push 闭包逐人推
    // ...
}
```

**ROOM / TEAM 频道保留原 `m_push` 闭包逐人推送逻辑**（`lobby_service.cc:577-623`），不走 Pub/Sub。这个取舍见 9.5.5 节。

### gate 端：独立线程订阅

这是本节最微妙的部分。`gate_server.cc:910-944`：

```cpp
void GateServer::startWorldChatSubscriber() {
    if(m_redisHost.empty()) return;
    m_subscriber = std::make_shared<Subscriber>(m_redisHost, m_redisPort);
    auto self = std::static_pointer_cast<GateServer>(shared_from_this());
    m_subThread = std::make_shared<std::thread>([self, this]() {
        // 订阅失败重连循环(连接断了自动重试)
        while(true) {
            if(!m_subscriber->subscribe("chat:world", [self](const std::string& payload) {
                    // 收到世界聊天消息: 解析 ChatNotify, 广播给本地所有在线 session
                    ChatNotify n;
                    if(!n.ParseFromString(payload)) return;
                    std::string msgPayload;
                    n.SerializeToString(&msgPayload);
                    sylar::RWMutex::ReadLock lk(self->m_sessionMutex);
                    auto snapshot = self->m_accountToSession;
                    lk.unlock();
                    for(auto& kv : snapshot) {
                        if(kv.second) self->sendToSession(kv.second, MSG_CHAT_NOTIFY, payload);
                    }
                })) {
                sleep(2);
                continue;
            }
            SYLAR_LOG_INFO(g_logger) << "gate: subscribed chat:world (redis "
                << m_redisHost << ":" << m_redisPort << ")";
            m_subscriber->loop();   // 阻塞, 直到连接断开
            SYLAR_LOG_WARN(g_logger) << "gate: chat:world subscription lost, reconnecting in 2s";
            sleep(2);
        }
    });
    m_subThread->detach();
}
```

几个值得记住的设计：

1. **独立 `std::thread` 而不是协程**（见 9.5.4 节解释）；
2. **`while(true) + sleep(2)` 重连**：subscribe 失败 / loop 退出 / 连接断开 → 等 2 秒重试；
3. **回调里快照 session 表再锁外推**：`auto snapshot = self->m_accountToSession;` 是 `std::unordered_map` 的拷贝（持 shared_ptr），拷完立刻 `lk.unlock()`，再遍历 `sendToSession`——**绝不持读锁跨 sendToSession**（sendToSession 内部要拿 session 自己的 sendMutex，长持外层读锁会阻塞 delSession 的写锁）。这是第 6.2 节"锁内快照、锁外操作"范式的又一次复用；
4. **`detach()`**：线程与 gate 同生命周期，gate 析构时进程也基本在退出，不 join 也没事（detach 后线程跑 std::thread::~thread 不调 join 也不会 std::terminate，因为已 detach）。

### 9.5.4 为什么订阅用独立线程而不是协程

这是最容易踩坑的取舍，必须讲清楚。hiredis 的 `redisGetReply` 是**阻塞同步**调用——它在底层就是 `read(fd)`，但**不是 syscall hook 的目标**。回顾第 1.4 节，sylar 的 hook 用 `dlsym` 覆盖了一组固定的 libc 函数（`read`/`write`/`recv`/`connect`/...），但 hiredis 内部用的可能不是这些被 hook 的入口（比如它可能直接走 `poll`+`recv` 的组合，或者用了自己的 fd 管理逻辑），所以即使 hook 开着，`redisGetReply` 阻塞时**协程并不会让出**——而是把整个 worker 线程卡死。

如果把 `loop()` 放在协程里跑，那它阻塞期间这个 worker 线程上排队的其他协程全都没机会跑——gate 的 IOManager 只有 4 个线程（`gate_main.cc:18`），堵死一个就是 25% 算力消失。

放独立 `std::thread` 最简单稳定：操作系统调度，跟 sylar 协程互不干扰。**代价**：跨线程访问 `m_accountToSession` 必须加锁（这里用了 `RWMutex::ReadLock`），但这是廉价的。

> 如果未来要协程化：得自己把 hiredis 包成"hook 感知的"——比如用 `redisAsyncCommand` + 自己注册到 sylar IOManager 的事件回调。这条路复杂得多，本项目目前不值得做。

### 9.5.4.1 实战教训：两个调试时才暴露的关键坑

「订阅用独立线程」这个决定听起来很自然，但落地时暴露了两个**之前完全没意识到的坑**——一个在 hiredis 的 timeout 语义、一个在 sylar 协程基础设施的 thread-local 假设。两个坑都靠 core dump + gdb 才定位出来，值得单独记录。第 13 章坑 9/坑 10 是它们的"四段式"精简版，这里讲设计动机。

**坑 A：redisSetTimeout 把"连接超时"当成"命令超时"，订阅 1.5 秒就断开**

`Subscriber::subscribe`（`redis_pool.cc:111-129`）原本写得"看起来很合理"：

```cpp
struct timeval tv = {1, 500000};   // 1.5 秒
ctx_ = redisConnectWithTimeout(host_.c_str(), port_, tv);
redisSetTimeout(ctx_, tv);   // ← 元凶
```

现象：gate 启动后订阅 `chat:world` 成功，日志显示 `subscribed chat:world`，但 **1.5 秒后就反复刷 `subscription lost, reconnecting in 2s`**，期间 PUBLISH 的消息全丢。

根因：hiredis 的 timeout 有**两种语义**——

- **连接超时**（connect 阶段，`connect(2)` 卡多久放弃）；
- **命令超时**（连接建立后所有 read/write 的阻塞上限）。

`redisConnectWithTimeout` 会把传入的 tv **同时**设为两者；`redisSetTimeout(ctx, tv)` 又显式覆盖一遍。这个 command_timeout 对**所有后续命令生效**，包括 `loop()` 里阻塞等消息的 `redisGetReply`。

于是 `redisGetReply` 最多阻塞 1.5 秒；没消息就超时返回 `rc != REDIS_OK` → `loop()` 的 while 条件 break → loop 退出 → 重连循环看到「subscription lost」。

修复（`redis_pool.cc:127-128`）：连接成功后用 `{0, 0}` 清除 command_timeout：

```cpp
// 用 {0,0} 清除 command_timeout(hiredis 语义: 0 = NULL = 无超时),
// 让 redisGetReply 永久阻塞等消息。
struct timeval zeroTv = {0, 0};
redisSetTimeout(ctx_, zeroTv);
```

hiredis 内部把 `{0, 0}` 视为 NULL（即不设超时），`redisGetReply` 永久阻塞等消息——连接真断了 hiredis 会返回错误触发重连。

**关键认知**：hiredis 的 timeout 是个语义陷阱。订阅场景需要「连接用超时、命令不超时」——这两个语义在 hiredis API 里是**同一个参数**，必须连接成功后手动清零。

**坑 B：订阅线程是裸 std::thread，`IOManager::GetThis()` 返回 nullptr → SIGSEGV**

修完坑 A 之后订阅稳定了，但**世界聊天一发消息 gate 就 Segmentation fault (core dumped)**。gdb 栈帧：

```
#0  ___pthread_mutex_lock (mutex=0x8)
#3  sylar::Scheduler::schedule (this=0x0)        ← IOManager 指针为空
#4  GateServer::sendToSession (gate_server.cc:194)
#5  startWorldChatSubscriber 的 lambda (订阅回调)
#10 Subscriber::loop
#11 std::thread::_M_run                          ← 裸线程, 非 IOManager worker
```

根因：sylar 的 `IOManager::GetThis()` 用 thread-local（`t_scheduler`）——**只有 IOManager 自己 spawn 的 worker 线程才会被设置这个值**。9.5.4 解释了为什么订阅必须用裸 `std::thread`（hiredis 阻塞、sylar hook 不感知），但裸线程的 `t_scheduler` **从未被赋值**，`GetThis()` 返回 `nullptr`。

订阅回调里调 `sendToSession`（`gate_server.cc:177-204`），内部要走 `sylar::IOManager::GetThis()->schedule(...)` 启动 drain 协程——解引用 nullptr → 直接 SIGSEGV。

修复（`gate_server.cc:919-944`）：在 gate 的 IOManager 上下文里（此时 `GetThis()` 有效）先把 iom 指针捕获进闭包，订阅线程用它调 schedule 而不是依赖 GetThis：

```cpp
auto iom = sylar::IOManager::GetThis();   // 在 gate IOManager 协程里调, GetThis() 有效
m_subThread = std::make_shared<std::thread>([self, iom]() {
    self->m_subscriber->subscribe("chat:world", [self, iom](const std::string& payload) {
        iom->schedule([self, payload]() {   // 投递到 gate IOManager 的协程里执行
            // 这里 sendToSession 跑在 IOManager worker 上, GetThis() 有效
            // ...快照 m_accountToSession, 逐个 sendToSession
        });
    });
});
```

**关键认知**：sylar 的整个协程基础设施（`IOManager::GetThis` / `Fiber::GetThis` / `Scheduler::GetThis`）都依赖 thread-local 存储，**只在 IOManager worker 线程里有效**。任何用裸 `std::thread` 启动的线程（比如本订阅线程）**都不能直接调用这些 API**——必须通过外部捕获的 iom 指针间接访问，或者用 `iom->schedule(...)` 把工作投递回 IOManager 在协程里执行。

这两个坑的共同点：都是「连接看起来成功了」但「业务一跑就崩」——本质都是**对底层库语义/运行时假设理解不彻底**。教训是：**裸线程接入了协程基础设施，必须显式桥接**。

### 9.5.5 为什么订阅连接不入连接池

回顾 `redis_pool.h:71-92` 的 `Subscriber` 类：

```cpp
class Subscriber {
public:
    using MsgCallback = std::function<void(const std::string&)>;
    Subscriber(const std::string& host, int port);
    ~Subscriber();
    bool subscribe(const std::string& channel, MsgCallback cb);  // 可多次调订阅多 channel
    bool loop();                                                  // 阻塞接收
    void stop();                                                  // 设 running_=false + UNSUBSCRIBE
private:
    std::string host_;
    int port_;
    redisContext* ctx_ = nullptr;     // ★ 独立 redisContext, 不入连接池
    bool running_ = false;
    std::mutex cbMutex_;
    std::unordered_map<std::string, MsgCallback> callbacks_;   // channel → callback
};
```

`Subscriber` 自己持有一个 `redisContext`，**完全不走 RedisPool**。原因：Redis 的 SUBSCRIBE 命令会让连接进入"订阅模式"，**该连接此后只能收消息（PUSH）+ 发 SUBSCRIBE/UNSUBSCRIBE/PSUBSCRIBE/PUNSUBSCRIBE/QUIT/RESET**，不能发 GET/SET/SADD 等普通命令。如果订阅连接入了池，借出去给别人发 GET，会直接报错。

所以池化的连接是"通用连接"，**绝对不能进入订阅模式**；订阅必须用专用连接。这是 Redis 客户端设计里一个常见坑。

`Subscriber` 的生命周期管理：

- 构造时只存地址，**不连接**（懒连接，首次 `subscribe` 时才 `redisConnectWithTimeout`，见 `redis_pool.cc:111-122`）；
- `subscribe` 可多次调用，订阅多 channel——`callbacks_` 是 `unordered_map<channel, cb>`，loop 里按 channel 分发；
- `loop` 阻塞调 `redisGetReply`，收到 `["message", channel, payload]` 格式时查 callbacks_ 调对应回调（`redis_pool.cc:135-159`）；
- `stop` 设 `running_=false` + 发 `UNSUBSCRIBE` 让阻塞的 `redisGetReply` 立即返回；
- 析构 `stop + redisFree`。

### `redisPublish` 工具函数的两个重载

```cpp
// redis_pool.h:95-96
bool redisPublish(RedisPool& pool, const std::string& channel, const std::string& msg);
bool redisPublish(const std::string& host, int port, const std::string& channel, const std::string& msg);
```

- **重载 1（带 pool）**：从池借连接发布，归还。适合"服务自己有池"的场景，比如 data 服务内部（`data_service.cc:711` 就是这么用的：`redisPublish(*m_redisPool, "chat:world", payload)`）。
- **重载 2（host/port）**：独立短连接发布。适合"服务没池"的场景——比如 gate 如果某天想直接发 PUBLISH 而不经 data（本项目里没有这种用法，但接口预留）。

实现见 `redis_pool.cc:175-205`，两个重载逻辑对称，区别只在连接来源。命令都是 `PUBLISH %s %b`（`%b` 是 hiredis 的二进制安全占位符，配 `msg.data(), msg.size()`）。

## 9.5.6 ROOM/TEAM 频道为什么没用 Pub/Sub（重要取舍）

诚实标注：**Pub/Sub 是 fire-and-forget，不保证投递**。Redis 的 PUBLISH 把消息推给"当前在线的订阅者"，没有订阅者就丢弃，不缓存、不重试、无 ACK。

这条特性决定了哪些场景能用：

| 频道 | 关键性 | 能丢吗 | 选型 |
|------|--------|--------|------|
| **WORLD 世界聊天** | 低，丢了就是少看到一条话 | 可以 | **Pub/Sub** ✓ |
| **ROOM 房间内** | 中-高，含 SHOOT_RESULT / TURN_START 这种**关键消息** | 不能 | 原 m_push 闭包逐人推 |
| **TEAM 队伍** | 中，含队伍作战消息 | 不能 | 原 m_push 闭包逐人推 |

如果 ROOM/TEAM 也用 Pub/Sub，订阅连接断开期间的战斗消息（比如回合切换、伤害结算）会永久丢失——玩家看到的现象就是"别人开火了但回合没切到我"，对局直接卡死。这种业务**必须有 ACK + 重试 + 状态恢复机制**（本项目目前用 m_push 走 RPC 推送，依赖 TCP 可靠传输 + 客户端 ACK 的隐含假设）。

聊天不一样——掉一句话用户感知很弱，重发也没意义（聊天有时效性，过了 10 秒再补发一条"hello"反而突兀）。所以世界聊天用 Pub/Sub 是**正确取舍**。

> 注：原 lobby 调 gate.NotifyAllOnline 走的是 RPC，理论上是"可靠"的（RPC 失败会返错）。但实际上 lobby 调完 RPC 立刻返回，gate 推送失败 lobby 也不知道——可靠性跟 Pub/Sub 差不多。换成 Pub/Sub 反而少了一次 RPC 跳转，更轻。

## 9.5.7 整体收益

把两块改动一起看：

1. **修复了项目自标注的功能缺陷**（architecture-analysis §8.3 建议 7 的"在线状态查询未实现"现在实现）；
2. **多 gate 部署从"勉强能用"变"天然支持"**：在线状态集中在 Redis Set、世界聊天靠 Pub/Sub 自然分发，不用在 lobby 里维护 gate 列表；
3. **降 RPC 频次**：原世界聊天每次 = 1 次 lobby→data RPC + N 次 lobby→gate.NotifyAllOnline RPC（N=gate 数）。现在 = 1 次 lobby→data RPC + 1 次 Redis PUBLISH（gate 各自订阅）。N 越大收益越明显；
4. **诚实承认局限**：Pub/Sub 不能丢消息的业务（战斗、回合）仍然走原 m_push 闭包——没有"为了统一而统一"。

学到的两条通用经验：

- **数据结构选型决定性能特征**：在线状态选 Set（O(1) SADD/SREM + 一次 SMEMBERS 拿全表）而不是 String（N 次 EXISTS），是写代码前要想清楚的；
- **传输语义决定能用场景**：Pub/Sub 是 fire-and-forget，所以只能用于"可丢"的业务（聊天、心跳、监控）；关键业务（战斗结算、转账）必须有可靠通道。

---

# 第 10 章 战绩落库（拆主子表 + 累计伤害 + 异步 schedule）

> **§13 改动**，涉及 schema + proto + data + battle 四个文件

本会话最完整的一个改动。从"只能记 2 玩家"扩展为"任意人数 + 每位玩家明细"，且端到端事务保证原子性，落库过程不阻塞战斗。

## 10.1 问题：旧表结构写死 2 玩家

旧 `game_records` 表是 `player1_id` / `player2_id` / `winner_id` 的扁平结构，**只能记 2 玩家**。多人战斗（4 人房、8 人房）写不下，且没法记每位玩家的明细（伤害、是否胜者）。

## 10.2 schema 拆主子表（schema.sql:37-60）

```sql
-- §13: game_records 拆主子表, 支持多人战绩(原 player1_id/player2_id 字段限制只能记 2 人)。
--   主表: 一局对战记录, 子表: 该局每位参战玩家的明细(账号/队伍/是否胜者/累计伤害)。
--   原表无 FK 无读取者(Write-only), DROP 重建无数据损失风险。
DROP TABLE IF EXISTS game_records;
CREATE TABLE game_records (
  id           BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  winning_team TINYINT,                -- 与 proto TeamSide 数值一致(0=RED 1=BLUE); NULL=无胜方
  duration     INT,
  created_at   DATETIME DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB;

CREATE TABLE game_record_players (
  id           BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  record_id    BIGINT UNSIGNED NOT NULL,
  account_id   BIGINT UNSIGNED NOT NULL,
  team         TINYINT NOT NULL,      -- 与 proto TeamSide 数值一致(0=RED 1=BLUE)
  is_winner    TINYINT(1) DEFAULT 0,
  damage_dealt INT DEFAULT 0,
  created_at   DATETIME DEFAULT CURRENT_TIMESTAMP,
  INDEX idx_record (record_id),
  INDEX idx_account (account_id),
  FOREIGN KEY (record_id) REFERENCES game_records(id) ON DELETE CASCADE,
  FOREIGN KEY (account_id) REFERENCES accounts(id)
) ENGINE=InnoDB;
```

学到的：
1. **一对多关系拆主子表**：主表记"一局"，子表记"每位参战玩家"，靠 `record_id` 关联；
2. **CASCADE DELETE**：删主表行自动删子表（少写一份清理代码）；
3. **注释诚实标注"原表 Write-only"**：所以 `DROP` 重建无数据损失风险——这是工程判断的诚实；
4. **INDEX 设计**：`idx_record`（按局查所有玩家，结算页用）、`idx_account`（按玩家查历史战绩，个人页用）。

## 10.3 proto 加 PlayerStat（rpc.proto）

第 4.3.1 节已贴，这里强调设计：
- **旧字段保留**（`player_ids`/`winner_ids`/`duration`），向后兼容；
- **新字段优先**（`players`/`winning_team`），走新路径；
- `PlayerStat.damage_dealt` 是业务统计字段，省得另外建缓存。

## 10.4 data 端：事务 + 批量插入（data_service.cc:254-322）

```cpp
// §13: 拆主子表(游戏记录 → 主表 + 玩家统计子表), 支持任意人数。
//   - 新路径: req->players 非空, 用主子表 + 事务保证原子性。
//   - 旧路径: req->players 空, 兼容旧 caller(取 player_ids 前 2 + winner_ids 第 1),
//             映射到新表结构(红蓝各 1, damage_dealt=0)。
//   - 事务: BEGIN → INSERT 主表 → 取 lastInsertId → 批量 INSERT 子表 → COMMIT。
//     失败任一步 rollback, RAII 也保证 scope 退出时回滚(Transaction 析构)。
void DataServiceImpl::SaveGameRecord(::google::protobuf::RpcController*,
        const GameRecordReq* req, ResultResp* resp, ::google::protobuf::Closure* done) {
    sylar::Database db(m_pool.get());
    if(!db.valid()) { /* ... */ return; }

    bool useNew = req->players_size() > 0;
    if(!useNew && req->player_ids_size() == 0) {
        resp->set_result(BAD_PARAM); resp->set_msg("empty record");
        return;
    }

    // 事务绑定到本 db 借出的连接, 须与 db 同 scope 保证连接不归还中途。
    sylar::Transaction trx(db.getConnection());
    if(!trx.begin()) { /* begin tx fail */ return; }

    // 1) 主表: game_records
    std::string mainSql = "INSERT INTO game_records(winning_team,duration) VALUES("
        + std::to_string((int)req->winning_team()) + "," + std::to_string(req->duration()) + ")";
    if(trx.execute(mainSql) != 0) {
        trx.rollback();
        resp->set_result(FAIL); resp->set_msg("insert main fail");
        return;
    }
    uint64_t rid = (uint64_t)trx.getLastInsertId();

    // 2) 子表: game_record_players(批量 VALUES 拼 SQL, 一次 INSERT)
    std::string sql = "INSERT INTO game_record_players(record_id,account_id,team,is_winner,damage_dealt) VALUES";
    if(useNew) {
        for(int i = 0; i < req->players_size(); ++i) {
            const auto& p = req->players(i);
            if(i > 0) sql += ",";
            sql += "(" + std::to_string(rid)
                 + "," + std::to_string(p.account_id())
                 + "," + std::to_string((int)p.team())
                 + "," + std::to_string(p.is_winner() ? 1 : 0)
                 + "," + std::to_string(p.damage_dealt()) + ")";
        }
    } else {
        // 旧路径兼容: player_ids(0/1) → 红蓝各 1, winner_ids 判胜负, damage=0
        // ...
    }
    if(trx.execute(sql) != 0) {
        trx.rollback();
        resp->set_result(FAIL); resp->set_msg("insert players fail");
        return;
    }

    if(!trx.commit()) {
        resp->set_result(FAIL); resp->set_msg("commit fail");
        return;
    }
    resp->set_result(SUCCESS);
}
```

学到的：
1. **事务包裹多步 SQL**：主表 + 子表多行必须原子（不能主表插入成功但子表失败留下孤儿）；
2. **批量 INSERT**：N 个玩家拼成一个 `VALUES (...),(...),(...)` 一次 SQL，比 N 次 INSERT 快几个数量级；
3. **lastInsertId 关联**：主表插完取自增 ID，作为子表的 `record_id`；
4. **失败任一步 rollback**：`Transaction` 析构 RAII 兜底（scope 退出未 commit 自动 rollback）；
5. **旧路径兼容**：proto 旧字段非空时仍能写入新表结构（红蓝各 1，damage=0），平滑迁移。

## 10.5 battle 端：累计 damageDealt + checkGameOver 异步落库

### 10.5.1 BattlePlayer 加 damageDealt 字段

`battle_room.h:33`：

```cpp
struct BattlePlayer {
    // ... 其他字段
    int   damageDealt = 0;   // §13: 本局累计造成伤害(checkGameOver 落库用, onShoot AOE 内累加)
};
```

### 10.5.2 addPlayer 初始化（battle_room.cc:43）

```cpp
p.damageDealt = 0;   // §13: 初始化累计伤害
```

### 10.5.3 onShoot AOE 内累加（battle_room.cc:290）

```cpp
if(dmg > 0) {
    // ... 算 finalDmg(暴击/格挡)
    p.hp = std::max(0, p.hp - finalDmg);
    shooter->damageDealt += finalDmg;   // §13: 累计射手本局伤害(结算落库用)
    // ...
}
```

注意是给 `shooter->damageDealt` 加，不是 `p`。`p` 是受击者，`shooter` 是射手。

### 10.5.4 startGame 记开局时间戳（battle_room.cc:57）

```cpp
m_startTimeMs = sylar::GetCurrentMS();   // §13: 记开局时间戳(checkGameOver 算 duration)
```

### 10.5.5 checkGameOver 调 saveGameRecordLocked（battle_room.cc:484-515）

```cpp
void BattleRoom::checkGameOver() {
    if(!m_started) return;
    int alive = countAlive();
    if(alive <= 1) {
        GameOverNotify n;
        // ... 找唯一存活者, 设 winningTeam
        broadcast(MSG_GAME_OVER_NOTIFY, payload);
        m_started = false;
        cancelTurnTimer();

        // §13: 战绩落库(锁内快照 + 锁外 schedule 异步 RPC, 不阻塞 onShoot 协程)。
        // foundWinner=false 时(全员阵亡) winningTeam 用默认, 落库仍记全部玩家伤害。
        (void)foundWinner;
        saveGameRecordLocked(winningTeam);
    }
}
```

### 10.5.6 saveGameRecordLocked（battle_room.cc:521-557）

```cpp
// §13 战绩落库: 锁内快照全部玩家到 proto + 算 duration, schedule 到独立协程 RPC。
// - 锁内: 仅做内存拷贝(快), 不持锁跨 RPC(RPC 调用链深可能 yield, 不可持 m_roomMutex)。
// - 锁外 schedule: 调 data.SaveGameRecord, 失败仅 warn(不影响对局流程)。
// 调用方(checkGameOver)已持 m_roomMutex, 本方法内不可再 yield。
void BattleRoom::saveGameRecordLocked(TeamSide winningTeam) {
    if(!m_data) return;   // 未注入 data channel(旧 battle_main), 跳过
    auto self = shared_from_this();
    uint64_t nowMs = sylar::GetCurrentMS();
    int duration = (int)((nowMs - m_startTimeMs) / 1000);   // 秒

    // 锁内快照(其实 checkGameOver 已持锁, 此处构造 req 不再上锁, 但需要在锁内访问 m_players)
    ddt::GameRecordReq req;
    req.set_winning_team(winningTeam);
    req.set_duration(duration);
    for(const auto& p : m_players) {
        auto* s = req.add_players();
        s->set_account_id(p.accountId);
        s->set_team(p.team);
        s->set_is_winner(p.alive && p.hp > 0 && p.team == winningTeam);
        s->set_damage_dealt(p.damageDealt);
    }

    // 锁外 schedule: RPC 调用链(etcd+TCP+protobuf)深, 必须在独立协程上执行避免栈溢出。
    sylar::IOManager::GetThis()->schedule([self, req]() {
        try {
            ddt::DataService::Stub stub(self->m_data.get());
            sylar::rpc::RpcController ctrl;
            ddt::ResultResp resp;
            stub.SaveGameRecord(&ctrl, &req, &resp, nullptr);
            if(ctrl.Failed()) {
                SYLAR_LOG_WARN(g_logger) << "battle: save game record fail room=" << self->m_roomId
                    << " err=" << ctrl.ErrorText();
            } else {
                SYLAR_LOG_INFO(g_logger) << "battle: game record saved room=" << self->m_roomId
                    << " players=" << req.players_size();
            }
        } catch(const std::exception& e) {
            SYLAR_LOG_WARN(g_logger) << "battle: save game record exception: " << e.what();
        }
    });
}
```

这是 lobby tryStart 范式的又一个范例：

学到的：
1. **锁内快照、锁外 RPC**——绝不在持房间锁时跨 RPC（会 yield 死锁）；
2. **schedule 到独立协程**避免栈溢出（同 broadcast）；
3. **失败仅 warn 不影响对局流程**——落库失败不该让玩家卡在结算页；
4. **shared_from_this + 按值捕获 req**：保证 room 在 RPC 期间不被析构、req 在 scope 内有效。

## 10.6 battle_main 注入 data channel

`battle_main.cc:83-84`：

```cpp
auto impl = std::make_shared<ddt::BattleServiceImpl>(cfg, push, tw, bpush,
    std::make_shared<sylar::rpc::RpcChannel>(cfg.etcd_endpoint));   // §13 data 服 channel(战绩落库)
```

注意是 `RpcChannel`（短连接，无池）。理由：战绩落库是低频（每局结束才 1 次），不需要连接池；data 服务本身在 RPC 协程内同步执行，调用链不算极深。

## 10.7 小结

战绩端到端落库涉及 schema + proto + data + battle 四个文件，是一个完整的纵向改动。学到的：
- 数据模型设计（一对多拆主子表）；
- 协议设计（proto 向后兼容）；
- 事务保证（BEGIN/INSERT 主/INSERT 子/COMMIT）；
- 异步派发（锁内快照+锁外 schedule）。

---

# 第 11 章 客户端架构（Unity + 主线程队列 + DebugLog）

> 注：客户端是 Unity (C#) 项目，**不在本仓库 src/ 树内**。本章基于协议和注释讲解架构思想，不引用具体行号。

## 11.1 主线程无阻塞

Unity 的生命周期（Update / LateUpdate）跑在主线程。如果网络收发也放主线程，TCP 反压会冻住画面。

所以客户端架构是：
- **网络收发在独立线程**（C# 的 `TcpClient.GetStream().Read()` 阻塞调用）；
- **发送队列化**（业务调 SendMessage 只是入队，由网络线程串行 send）；
- **接收用主线程队列**（网络线程收到消息后入队，主线程 Update 里 drain）。

这样主线程永远不阻塞，网络慢只影响延迟不影响帧率。

## 11.2 场景切换缓冲

Unity 切场景（Lobby → Battle）是个特殊窗口：旧场景的回调还没销毁、新场景的回调还没准备好。这个窗口里如果收到消息，按场景状态分发会出问题——比如战斗消息 `MSG_TURN_START_NOTIFY` 在场景切换中途到达，会被路由到还没初始化的 BattleManager，NullReferenceException。

解决：**收到消息时若正在切场景，缓冲到队列**，场景切换完成后回放。这就是"场景切换缓冲"。

## 11.3 自动重连

断线后指数退避自动重连：
1. 网络线程检测到 read 返回 0 或异常 → 标记断线；
2. 启动定时器（1s → 2s → 4s → 8s → 16s 上限）尝试重连；
3. 重连成功后用缓存的 token 自动登录（不需要重新输密码）；
4. 登录后请求房间/战斗状态恢复。

服务端配合：`ValidateToken` 接受 token 即可恢复，账号绑定到新 session（旧 session 已被顶号清理）。

## 11.4 **客户端 DebugLog（DDT_DBG 编译开关）**

客户端调试时最大的痛点：**问题发生了但不知道在哪一步**。在 Unity 里加日志很容易写满代码、Release 包又得手动删。

解决：**编译期 DDT_DBG 开关**。在 C# 里：

```csharp
#if DDT_DBG
#define DLog(msg) UnityEngine.Debug.Log($"[DDT] {msg}")
#else
#define DLog(msg) (void)0
#endif
```

或者用 `[Conditional("DDT_DBG")]` 特性，让编译器在 DDT_DBG 未定义时**直接不编译调用代码**（连参数计算都省了）。

```csharp
[System.Diagnostics.Conditional("DDT_DBG")]
public static void Log(string msg) {
    UnityEngine.Debug.Log($"[DDT] {msg}");
}
```

各模块在状态变化点加日志：
- 网络模块：连接 / 断线 / 重连尝试 / 收到 msg_id
- 战斗模块：回合开始 / 射击结果 / 受击
- 房间模块：进入 / 离开 / 准备状态变化
- 场景切换：触发 / 完成 / 缓冲消息回放

Debug 模式下打开 DDT_DBG 符号，状态机每一步都打日志；Release 模式关掉，性能零损失。

## 11.5 客户端 focus 误丢消息（已踩坑，详见 13.5）

Unity 在窗口失焦时（`OnApplicationPause(true)`）默认会**暂停主循环**。如果这时候收到战斗消息，会被缓冲，但缓冲满后可能丢失。或者切回前台后状态已过期（比如"该你出手"已超时）。

教训：要么在 OnApplicationPause 时通知服务端"我暂时不在线"（gate 不会立刻踢，但战斗会算超时），要么保持后台运行（Unity Pro 才支持的 `Application.runInBackground = true`）。

---

# 第 12 章 运维（fleet.sh + LD_PRELOAD + core dump）

> 文件：`scripts/fleet.sh`、`scripts/etcd_close_preload.cc`

## 12.1 fleet.sh：5 服务一键启停

`scripts/fleet.sh` 是 5 服务（login/gate/lobby/battle/data）一键启停/状态脚本。用法：

```bash
./scripts/fleet.sh start      # 启动全部
./scripts/fleet.sh stop       # 停止全部
./scripts/fleet.sh restart    # 重新编译 + 重启
./scripts/fleet.sh status     # 查进程 + 端口
./scripts/fleet.sh etcd       # 查 etcd 注册的键
./scripts/fleet.sh logs gate  # tail 看 gate 日志
```

### 12.1.1 **fleet.sh 修复**：PATTERN 按二进制名匹配

本会话修复的关键 bug（`fleet.sh:13-15`）：

```bash
# 进程匹配模式: 按 5 个服务的二进制名精确匹配, 不依赖路径前缀。
# 修复: 原 "$BIN/ddt_" 只匹配绝对路径, 杀不掉用相对路径(./ddt_xxx)启动的旧进程,
#       导致它们占着端口让新进程 bind 失败。改为 ddt_(login|gate|...) 匹配所有路径形式。
PATTERN="ddt_(login|gate|lobby|battle|data)[ ]"
```

原 bug：原 PATTERN 是 `"$BIN/ddt_"`（绝对路径前缀），匹配不到用相对路径（`./ddt_xxx`）启动的旧进程。结果重启时旧进程没被杀掉，占着端口让新进程 bind 失败。

修复：改成 `ddt_(login|gate|lobby|battle|data)[ ]`——只匹配二进制名（5 选 1），加 `[ ]`（空格）防止匹配到 `ddt_gatekeeper` 之类前缀相同的进程。pkill -f 全路径都扫到。

### 12.1.2 **kill_all_ddt 重试 2 次**

```bash
# 杀掉所有 5 服务进程(不限路径)。重试 2 次确保杀干净。
kill_all_ddt() {
  pkill -f "$PATTERN" 2>/dev/null || true
  sleep 1
  # 兜底: 仍残留的强杀
  if pgrep -f "$PATTERN" >/dev/null 2>&1; then
    pkill -9 -f "$PATTERN" 2>/dev/null || true
    sleep 1
  fi
}
```

为什么重试？有些进程在 RPC 调用中（卡在 etcd 等），SIGTERM 优雅退出可能需要几秒。第一次发 SIGTERM 等 1 秒，仍残留就 SIGKILL（-9）强杀。

### 12.1.3 start 命令带 LD_PRELOAD 和 core dump

```bash
start)
    kill_all_ddt
    # 开启 core dump(排查 stack smashing 崩溃用)
    ulimit -c unlimited
    for s in "${SERVS[@]}"; do
      cd "$BIN" && nohup env LD_PRELOAD="$PRE" bash -c "ulimit -c unlimited; exec $BIN/ddt_$s -c $CONF/$s.yml" >"$LOG/ddt_$s.log" 2>&1 &
      echo "started ddt_$s pid=$!"
    done
    echo "waiting 4s for bind + etcd register..."
    sleep 4
    ;;
```

学到的：
1. **ulimit -c unlimited** 开启 core dump，崩了能拿到 core 文件 gdb 排查；
2. **LD_PRELOAD** 注入 etcd close 垫片（第 12.2 节）；
3. **bash -c 内再 ulimit** 是因为 nohup 启的子进程不一定继承父 shell 的 ulimit；
4. **sleep 4** 等 bind + etcd 注册完成，避免后续 status 命令看到端口未就绪。

## 12.2 LD_PRELOAD 垫片：解决 gRPC × sylar hook close 崩溃

`scripts/etcd_close_preload.cc` 是个**生产安全的 LD_PRELOAD 垫片**——不改 sylar 源码解决一个特别隐蔽的崩溃。

### 12.2.1 问题（注释很详细）

```cpp
// 问题：libsylar.so 的 syscall hook 里 close_f 等真实函数地址由 sylar::hook_init()
//   在 _HookIniter 静态构造期用 dlsym 解析。但 libsylar.so DT_NEEDED libetcd-cpp-api.so，
//   glibc 按"依赖先初始化"使 libetcd-cpp-api 内 gRPC 的静态初始化器(main 前)先调 close(fd)，
//   此时 close_f 仍为 NULL → sylar hook.cc:376 `return close_f(fd)` 跳 0 → SIGSEGV。
//   这会崩掉任何 sylar(hook)+gRPC 程序，含全部 5 个 ddt_* 服务。
```

翻译成人话：
1. sylar hook 把 `close` 也 Hook 了，调用时通过函数指针 `close_f` 跳到原系统 close；
2. `close_f` 在 `hook_init()` 里用 `dlsym` 解析，而 `hook_init` 由静态对象 `_HookIniter` 在 main 之前调；
3. **但** libsylar.so 依赖 libetcd-cpp-api.so，glibc 按"先初始化依赖"会让 libetcd-cpp-api 先初始化；
4. libetcd-cpp-api 里的 gRPC 在自己的静态初始化期会调 close()；
5. **此时 sylar 的 hook_init 还没跑**，`close_f` 还是 NULL；
6. → `return close_f(fd)` 跳到 0 地址 → SIGSEGV → 整个进程崩。

### 12.2.2 垫片方案

最朴素的垫片是"全部走 syscall"：

```c
int close(int fd) { return syscall(SYS_close, fd); }
```

但这破坏了 sylar hook 的协程 fd 清理（FdMgr::del + cancelAll），对启用 hook 的真实服务不安全。

更好的方案（注释里写的）：**根据 sylar 是否就绪分流**。

```cpp
extern "C" int close(int fd) {
    typedef int (*close_fun)(int);

    // close_f 变量的地址(sylar 导出的 extern "C" BSS 全局)。重试解析直到拿到。
    static close_fun* close_f_ptr = nullptr;
    if (!close_f_ptr) {
        close_f_ptr = (close_fun*)dlsym(RTLD_DEFAULT, "close_f");
    }

    // close_f 当前值非空 ⟺ sylar 的 hook_init 已跑 ⟺ 可安全转给 sylar 的 close 钩子。
    // 每次 close() 重读 *close_f_ptr 的值——不能缓存判定, 否则首次发生在 gRPC
    // 静态初始化期读到 NULL 会永久走 syscall, 退化回不安全的简单垫片。
    if (close_f_ptr && *close_f_ptr) {
        static close_fun sylar_close = nullptr;
        if (!sylar_close) {
            sylar_close = (close_fun)dlsym(RTLD_NEXT, "close");  // 命中 libsylar 的 close
        }
        if (sylar_close) {
            return sylar_close(fd);   // 保留 FdMgr::del + cancelAll 协程 fd 清理
        }
    }

    // sylar 未就绪(main 前 / gRPC 静态初始化窗口): 直接 syscall, 绝不解引用 NULL。
    return (int)syscall(SYS_close, fd);
}
```

学到的：
1. **LD_PRELOAD 可以无侵入修复第三方库的 bug**——比改源码并维护 fork 干净得多；
2. **`extern "C"` BSS 全局**可以被 LD_PRELOAD 库通过 `dlsym(RTLD_DEFAULT, ...)` 拿到地址（sylar 导出的 `close_f`）；
3. **不能缓存判定**——必须每次重读 `*close_f_ptr`，因为 sylar 的 hook_init 在 main 之前跑，第一次读到 NULL 不代表永远是 NULL；
4. **`RTLD_NEXT`** 命中 libsylar 的 close（垫片在 libsylar 之前加载，"下一个"就是 libsylar）。

### 12.2.3 构建

```bash
g++ -shared -fPIC -o lib/libetcd_close_fix.so scripts/etcd_close_preload.cc -ldl
```

使用：

```bash
LD_PRELOAD=/abs/lib/libetcd_close_fix.so ./ddt_*
```

## 12.3 core dump 调试

`ulimit -c unlimited` 开启 core dump，进程崩溃时会写 core 文件到 `bin/core`（fleet.sh start 时已设置）。

调试 stack smashing 的标准流程：
1. `gdb ./ddt_login /path/to/core`；
2. `bt` 看堆栈，找 `__stack_chk_fail`（栈金丝雀失败）；
3. `frame N` 跳到崩溃点，看 `Fiber::swapOut` 之类；
4. 结论通常是协程栈不够。

`g_core` 大小 195MB（最近一次崩溃留下的），说明 stack smashing 触发了完整 core dump。这种规模的 core 文件别提交到 git（`.gitignore` 已忽略）。

---

# 第 12.5 章 代码风格统一（注释精简 + K&R 重格式化）

> 范围：全项目 `sylar/rpc/` + `src/` + `scripts/` + `tests/`，60+ 文件

本章是一次**纯工程实践**的记录——没有任何功能改动，但作为方法论值得沉淀。本会话对全项目做了一次统一的注释优化 + 代码重格式化，原则和踩过的坑都在这里。

## 12.5.1 为什么要做

读老代码时发现几个反复出现的可读性问题：

1. **文件顶部大段注释块**：很多文件开头是十几行的 `// ==== 模块介绍 ==== // 详细说明 ... // ====`，介绍模块干啥的、有哪些字段、设计原则。这些信息初版有用，但**3 个月后就开始过时**——代码改了注释没改，反过来误导读者；
2. **压缩写法**：典型的如 `data_service.cc` 里有 56 处 `if(done){done->Run();} return;`——单行塞下，节省了行数但破坏了 grep / 断点 / diff 的可读性；
3. **花括号风格不统一**：有的文件 K&R（开括号跟 if 同行），有的 Allman（开括号另起一行）；有的单行 `if(...) return;`，有的拆三行；
4. **缩进不统一**：有 4 空格、2 空格、tab 混用。

这些问题不影响编译，但影响**长期维护成本**。统一一次能让后续 diff 更干净、读代码更顺。

## 12.5.2 原则

执行时严格遵守以下几条，目的是"只改外观不改语义"：

1. **删除文件顶部大段注释块**：那种 `// ============` 包裹的"模块介绍"一律删。文件顶部的简短 1-3 行说明（指明文件做什么 + 关键设计）保留；
2. **保留函数级注释**：每个函数上方 1-3 行简短中文说明（讲"做什么 + 关键设计点"）一律保留——这是最有价值的注释；
3. **保留行内"为什么"注释**：解释设计意图的注释（比如 `// 必须在挖坑前算伤害`、`// 防顶号误删`）一律保留——这是经验沉淀；
4. **删除"复述代码"的注释**：比如 `i++; // i 加 1` 这种废话直接删；
5. **重格式化**：
   - 4 空格缩进（不用 tab）；
   - K&R 花括号（`if (...) {` 开括号同行，`}` 闭括号另起）；
   - 单行 `if(x) return;` 拆为多行：
     ```cpp
     if(x) {
         return;
     }
     ```
   - 单语句单行（不把多个表达式塞一行）；
   - 函数之间空 1 行；
   - 文件末尾保留 1 个换行符；
6. **代码逻辑绝对不动**：变量名、函数体、控制流、类型、include 完全保留——这次是**纯文本改动**，编译产物应该 byte-by-byte 等价（除了行号变化）。

## 12.5.3 典型例子：`data_service.cc` 的压缩写法

改动前（多处）：

```cpp
void DataServiceImpl::GetFriendList(::google::protobuf::RpcController*,
        const IdReq* req, FriendListRpcResp* resp, ::google::protobuf::Closure* done) {
    sylar::Database db(m_pool.get());
    if(!db.valid()) { resp->set_result(FAIL); if(done){done->Run();} return; }
    auto rows = db.query("...");
    // ...
    if(done) done->Run();
}
```

改动后：

```cpp
void DataServiceImpl::GetFriendList(::google::protobuf::RpcController*,
        const IdReq* req, FriendListRpcResp* resp, ::google::protobuf::Closure* done) {
    sylar::Database db(m_pool.get());
    if(!db.valid()) {
        resp->set_result(FAIL);
        if(done) {
            done->Run();
        }
        return;
    }
    auto rows = db.query("...");
    // ...
    if(done) {
        done->Run();
    }
}
```

`data_service.cc` 全文有 56 处这种 `if(done){done->Run();} return;` 压缩写法，全部拆开。

**收益**：

- **断点更准**：gdb 在 `if(done)` 上设断点，断下后能清晰看到是 done 分支还是 return 分支；
- **diff 更干净**：将来要给某个分支加日志，只改一行不会触发整行重写；
- **grep 更靠谱**：`grep "done->Run"` 能定位到所有调用点（不会因为塞在 `if(...){}` 里被错过）。

## 12.5.4 注释删除的判断标准

哪些删、哪些留，是有标准的。举例：

**删（复述代码 / 易过时）**：

```cpp
// ======== DataService 服务实现 ========
// 本文件实现 rpc.proto 的 DataService 接口, 包含以下方法:
// - CreateAccount: 创建账号
// - GetAccountById: 按 ID 查账号
// - SaveToken: 存 token
// ...(20 行罗列所有方法)
// ======================================
```

这种"模块介绍"删——rpc.proto 自己就是接口定义，比这份注释权威。删了之后看函数名就知道干啥。

**留（讲"为什么" / 经验）**：

```cpp
// §13 战绩落库: 锁内快照全部玩家到 proto + 算 duration, schedule 到独立协程 RPC。
// - 锁内: 仅做内存拷贝(快), 不持锁跨 RPC(RPC 调用链深可能 yield, 不可持 m_roomMutex)。
// - 锁外 schedule: 调 data.SaveGameRecord, 失败仅 warn(不影响对局流程)。
void BattleRoom::saveGameRecordLocked(TeamSide winningTeam) { ... }
```

这种"设计动机"留——它解释的是**为什么这么写**，3 年后回看仍然有用。

**留（陷阱标注）**：

```cpp
// 必须在挖坑/贴地表之前算! 用玩家当前位置(爆炸前的脚位)算距离,
// 否则挖坑后玩家 y 下降, 距落点距离变大, 伤害会被错误地衰减到 0。
```

这种"踩坑警告"留——它防的是新人改坏。

## 12.5.5 验证

改完 60+ 文件后，全项目 8 个目标（5 个服务可执行 `ddt_login` / `ddt_gate` / `ddt_lobby` / `ddt_battle` / `ddt_data` + 3 个冒烟测试）全部重新编译。要求：

- **零 error**：编译必须通过；
- **零 warning 新增**：如果改动引入了新 warning，说明可能动了语义，必须排查；
- **行为不变**：理论上重格式化不改语义，编译产物除了行号 debug info 外应该等价。

实际验证结果：8 个目标全部 `make -j` 通过，无 error。这印证了"纯文本改动"的边界守住了。

## 12.5.6 学到的方法论

1. **大段顶部注释块是债务不是资产**——初版的"模块介绍"3 个月后就开始误导人。文档应该集中在 `docs/`（像本笔记这样），代码里只留"为什么"和"陷阱"，不留"是什么"（"是什么"看代码就知道）；
2. **格式统一是一次性的工作**——做一次定基调，CI 加个 `clang-format -i` 检查守住就行，不要反复手动维护；
3. **纯文本改动要用编译验证**——"我只改了注释"是程序员最常骗自己的话，实际经常手抖改了别的。改完必须重新编译所有目标；
4. **K&R 风格的实战优势**：同行开括号节省垂直空间、grep 友好、diff 友好；项目选定一种风格后**全项目统一**比"哪种风格"本身更重要；
5. **`if(x) return;` 拆多行**不是教条主义——是为了断点 / diff / grep 的工程便利。在协程 + RPC 项目里，单步调试和日志 grep 是主要排障手段，可读性的边际收益很高。

> 这个章节本身的存在就是为了证明上面的方法论——本章就是"代码改完后写的经验沉淀"，本身就是"留在 docs/ 而不是源文件顶部"的实践。

---

# 第 13 章 踩坑总结

这一章把本会话调试过的几个典型坑以「**问题 → 误判 → 真相 → 教训**」格式记录，作为经验沉淀。

## 13.1 坑 1：login 服 stack smashing（栈溢出）

### 问题
压测时 login 服崩，core dump 显示 `__stack_chk_fail at Fiber::swapOut`。栈被栈金丝雀检测出溢出。

### 误判
一开始怀疑：
- 是不是 login 服里有死循环把栈填满了？
- 是不是 RPC channel 共享出错，多个协程串到同一栈了？
- 是不是 protobuf 序列化 buffer 写溢出了？

### 真相
`ValidateToken` 在主 RPC 协程上做 **2 次同步 data RPC**（`LoadToken` + `GetAccountById`），每次新建 `EtcdClient`（gRPC channel 是重对象，构造栈开销大）。1MB 栈在高并发登录时**栈使用接近极限**，叠加 gRPC 内部缓冲，触发 stack smashing。

`login.yml` 注释解释：

```yaml
# login 服用 2MB: ValidateToken 在主 RPC 协程上调 2 次同步 data RPC, 每次新建
# EtcdClient(gRPC channel 重对象)。1MB 在高并发登录时栈使用接近极限, 曾触发
# stack smashing(core dump, __stack_chk_fail at Fiber::swapOut)。2MB 留足余量。
# 其他服务保持 1MB(login 调用栈特别深, 单独加大)。
fiber.stack_size: 2097152
```

### 教训
1. **协程栈不是无限的**——1MB 看起来不小，但 RPC 调用链（etcd client + gRPC channel + protobuf）深度极大；
2. **`fiber.stack_size` 是按服务可调的**——不要追求"全局最小"，按调用深度单独调；
3. **必须在 IOManager 创建前加载配置**——否则栈大小不生效（第 1.5 节）；
4. **__stack_chk_fail 是好东西**——栈金丝雀检测出溢出，比"诡异 UAF"好排查得多。

## 13.2 坑 2：RpcChannelPool × hook 死锁/竞态

### 问题
gate 加了 RpcChannelPool 后高频压测崩，断言 abort。

### 误判
- 是不是池满了卡死？
- 是不是 lock 顺序错了死锁？

### 真相
sylar hook 模型下，**同一个 fd 被 hook 化 send/recv 时会 yield**。RpcChannelPool 复用连接（fd），如果协程 A 借出 fd 在 send 时 yield（等 EPOLLOUT），协程 B 也借到**同一个 fd**……

实际上 RpcChannelPool 的设计是**借出独占**（acquire 返回后到 release 前不会被别人拿到），但 hook 模型下同一个协程内多次 acquire/release 可能不按预期配对（比如异常路径忘了归还）。更隐蔽的是：sylar 的 hook 给 fd 注册 EPOLLOUT 时**不是协程感知的**，并发 addEvent 会触发 SYLAR_ASSERT。

注释明确标注（`gate_main.cc:28-30`）：

```cpp
// 注: 下游 RPC channel 用短连接(无连接池)——连接池在 sylar hook 模型下
//     存在 fd 复用竞态(addEvent assert), 已回退。
```

### 教训
1. **不是"连接池一定更好"**——hook 模型下 fd 复用有微妙竞态，要看场景；
2. **gate 路径回退短连接**——稳；
3. **lobby/battle 推送闭包继续用池**——独占借出语义保证无竞态；
4. **rpc_channel 加了 64MB 响应大小边界保护**（`rpc_channel.cc:151`）防 keep-alive 数据错位崩溃。

## 13.3 坑 3：fleet.sh PATTERN 匹配不到相对路径进程

### 问题
`./scripts/fleet.sh restart` 重启服务，新进程 bind 失败："Address already in use"。

### 误判
- 是不是端口回收慢（TIME_WAIT）？
- 是不是上一个 fleet.sh 还没退出？
- 是不是 OS 没释放 socket？

`netstat` 一查：旧 ddt_* 进程**还活着**！fleet.sh 的 kill_all_ddt 没杀掉它们。

### 真相
原 PATTERN 是 `"$BIN/ddt_"`——**绝对路径前缀**。但调试时手动启动用的是 `./ddt_xxx`（相对路径），`pkill -f "$BIN/ddt_"` 匹配不到这种进程名。

`fleet.sh:13-15` 注释解释：

```bash
# 修复: 原 "$BIN/ddt_" 只匹配绝对路径, 杀不掉用相对路径(./ddt_xxx)启动的旧进程,
#       导致它们占着端口让新进程 bind 失败。改为 ddt_(login|gate|...) 匹配所有路径形式。
PATTERN="ddt_(login|gate|lobby|battle|data)[ ]"
```

### 教训
1. **pkill -f 匹配的是完整命令行**，不要假设一定是绝对路径；
2. **`[ ]` 后缀防前缀误匹配**（避免杀掉 `ddt_gatekeeper` 之类前缀相同的进程）；
3. **kill_all_ddt 重试 2 次**：第一次 SIGTERM 等 1 秒，残留 SIGKILL；
4. **bind 失败先查进程**——`ss -ltnp` 看是哪个 pid 占着端口。

## 13.4 坑 4：挖坑后伤害衰减为 0

### 问题
玩家射中对方附近，按距离算应该有伤害，但实际伤害是 0。

### 误判
- 是不是 `calculateDamage` 公式错了？
- 是不是 `blast_radius` 配小了？
- 是不是命中坐标算偏了？

### 真相
`onShoot` 里顺序错了：先 `removeCircle` 挖坑，**再**算伤害。

挖坑后玩家脚高度跟着下降（贴地表）。这时算伤害用的是 `p.y`（挖坑后的新 y），距离落点远了，按 `ratio = 1 - dist/radius` 算，距离一远 ratio 变小甚至为 0。

注释（`battle_room.cc:277-278`）：

```cpp
// AOE 伤害: 必须在挖坑/贴地表之前算! 用玩家当前位置(爆炸前的脚位)算距离,
// 否则挖坑后玩家 y 下降, 距落点距离变大, 伤害会被错误地衰减到 0。
```

正确顺序：

```cpp
// 1) 用 p.y(爆炸前)算伤害
for(每个存活玩家) {
    dmg = calculateDamage(hit_x, hit_y, p.x, p.y, ...);
    // ...
}
// 2) 挖坑(removeCircle)
m_terrain.removeCircle(hit_x, hit_y, blast_radius);
// 3) 贴地表(更新 p.y)
for(存活玩家) { p.y = m_terrain.columnHeight(pix); }
```

### 教训
1. **顺序敏感的操作必须有注释**——「先算伤害再挖坑」这种规则不能靠记忆，要写下来；
2. **副作用耦合的状态修改要小心**：挖坑改地形 → 贴地表改玩家 y → 影响距离 → 影响伤害，是条长链；
3. **测试用例要覆盖"玩家站在落点附近"的场景**——光测"直接命中"发现不了这个 bug。

## 13.5 坑 5：客户端 focus 误丢消息（Unity）

### 问题
玩家切到别的窗口（alt-tab），切回来发现回合已被跳过/已超时。

### 误判
- 是不是服务端超时太短了？
- 是不是网络断了？
- 是不是消息发丢了？

### 真相
Unity 默认 `Application.runInBackground = false`——窗口失焦时主循环暂停。这时收到的网络消息被缓冲在队列里，主线程不 drain。

如果缓冲期间服务端的回合定时器触发（10 秒超时自动 pass），切回来时客户端状态已过期（收到的是"上回合结果"+"新回合开始"两条，但客户端还停在"上回合操作中"）。

### 教训
1. **客户端默认行为要审查**——`runInBackground` 默认 false，对实时游戏是个坑；
2. **服务端的回合超时是"硬"的**——客户端暂停不算理由（否则会卡住其他玩家）；
3. **重连后状态恢复要考虑"已超时"分支**——客户端不能假设自己还在当前回合；
4. **DebugLog 是排查这种 bug 的利器**——加了 DDT_DBG 后能立刻看到"消息到达但主循环暂停"的窗口。

## 13.6 坑 6：gRPC 静态初始化期 close 解引用 NULL

### 问题
任何 sylar(hook) + gRPC 程序启动就崩，core dump 显示在 `close` 里 SIGSEGV。

### 误判
- 是不是 close 的 fd 不合法？
- 是不是 hook 的 close 实现错了？
- 是不是 gRPC 内部 bug？

### 真相
详见第 12.2 节。sylar hook 把 `close` Hook 成 `return close_f(fd)`，但 `close_f` 在静态初始化期还没被 `dlsym` 解析（是 NULL）。gRPC 的静态初始化器在 main 之前调 close，触发解引用 NULL。

### 教训
1. **静态初始化期是 C++ 最危险的窗口**——多库协作时初始化顺序由 glibc 决定，不可预测；
2. **LD_PRELOAD 垫片是优雅的修复手段**——比 fork 第三方库干净；
3. **`extern "C"` 全局变量是跨库通信的简单方式**——比 dlopen/dlsym 函数符号更稳定；
4. **不能缓存判定**——垫片要每次重读 `close_f` 的值，因为它的状态会变（NULL → 非 NULL）。

## 13.7 坑 7：proto3 全默认值消息序列化为 0 字节

### 问题
RPC 调用偶发性失败，错误信息 "read response body failed"，但实际业务正常。

### 误判
- 是不是网络不稳定？
- 是不是连接池借到的连接坏了？

### 真相
proto3 里全默认值的消息（比如 `ResultResp{result=SUCCESS=0, msg=""}`）序列化后是 **0 字节**。RpcChannel 读响应时 `readFixSize(buf, 0)` 返回 0，被当成失败。

注释（`rpc_channel.cc:162-164`）：

```cpp
// BUG-5 修复: proto3 全默认值消息序列化为 0 字节(如 ResultResp{SUCCESS=0,msg=""}),
// readFixSize(buf,0) 返回 0 会误判失败。0 字节响应是合法空消息, 直接进反序列化。
if(respSize > 0 && ss.readFixSize(&respStr[0], respSize) <= 0) { ... }
```

### 教训
1. **proto3 的默认值是"不存在"**——`int32 x = 0` 序列化时不占字节（wire 兼容性优化）；
2. **读固定长度的 IO 要小心 0 字节边界**——`if(respSize > 0 && readFixSize(...) <= 0)`；
3. **失败信息要可靠区分"真失败"和"边界情况"**——业务正常但日志报 fail 会迷惑人。

## 13.8 坑 8：注释与大段说明块的维护成本

### 问题
读老代码时被多处文件顶部的大段 `// ==== 模块介绍 ====` 注释块误导——注释说"这个模块做 X"，但代码早就改成了"做 X + Y + Z"，注释停留在 3 个月前的状态。

### 误判
- 是不是写注释的人不认真？
- 是不是 code review 没把好关？
- 是不是加个 CI 检查注释覆盖率就能解决？

### 真相
**大段顶部注释块天然容易过时**。原因：

1. 改代码时改注释的成本 > 改代码本身（写代码 5 分钟，更新注释还得读一遍旧注释理解原作者意图）；
2. PR review 时 reviewer 看 diff 主要看代码，注释差异容易 skim 过去；
3. 注释和代码是两份"事实"，**只要存在两份事实就一定会漂移**——单一真相源（single source of truth）原则被破坏；
4. 文件顶部的"模块介绍"通常是宏观描述，跟具体函数实现距离远，改实现时根本不会想到回头改顶部。

最糟的情况：**过时注释比没注释更坑**——新人按注释理解去做事，结果跑出来不对，浪费一小时排查才发现"哦注释过时了"。这是负价值。

### 教训
本会话对全项目做的注释精简（详见第 12.5 章）就是从这个坑里学出来的：

1. **代码注释只留两类**：「为什么这么写」（设计动机）+ 「这里有个坑」（陷阱警告）。其余的（"这个模块做啥""这个方法是啥"）删掉，让代码自己说话；
2. **"是什么"放文档**：宏观模块介绍、架构图、设计原则，统一放 `docs/`（本笔记就是干这个的），不要塞源文件顶部——文档有独立的 review 周期，比埋在代码里更容易维护；
3. **复述代码的注释比没注释还糟**：`i++; // i 加 1` 这种废话既增加行数又增加阅读负担，删；
4. **诚实标注占位与捷径的注释要保留**：像"SHA1 占位 SHA256（sylar util 没实现）"、"UpdateGender 自动 ALTER TABLE 兼容老库"这种**陷阱型注释**极有价值——它防止后人误以为是"正常设计"。这些一律留。

区分原则总结成一句话：**「让代码自己说"是什么"，注释只说"为什么"和"小心什么"」**。

## 13.9 坑 9：Subscriber redisSetTimeout 导致订阅 1.5s 自动断开

### 问题

世界聊天改用 Redis Pub/Sub 后（第 9.5 章），gate 启动时日志显示 `subscribed chat:world` 一切正常，但 **1.5 秒后开始反复刷 `subscription lost, reconnecting in 2s`**。期间 PUBLISH 出去的消息全丢——客户端世界频道收不到任何消息。

### 误判

- 是不是 Redis 服务端有问题（OOM？max clients？）？
- 是不是订阅 channel 名拼错了，订阅的是别的频道？
- 是不是 gate 和 data 连的 Redis 不是同一个实例？
- 是不是网络抖动导致 TCP 连接被掐？

`redis-cli SUBSCRIBE chat:world` 在另一个终端手动测，连接稳如老狗——所以排除 Redis 服务端、channel 名、网络。问题在 gate 客户端代码。

### 真相

`redis_pool.cc::Subscriber::subscribe` 里这段代码"看起来很合理"：

```cpp
struct timeval tv = {1, 500000};   // 1.5 秒
ctx_ = redisConnectWithTimeout(host_.c_str(), port_, tv);
redisSetTimeout(ctx_, tv);   // ← 元凶
```

但 hiredis 的 timeout **有两种语义**：

1. **连接超时**（connect 阶段，`connect(2)` 卡多久放弃）；
2. **命令超时**（连接建立后所有 read/write 的阻塞上限）。

`redisConnectWithTimeout` 会把传入的 tv **同时**设为两者，`redisSetTimeout` 又显式覆盖一遍。这个 command_timeout **对所有后续命令生效**——包括 `loop()` 里阻塞等消息的 `redisGetReply`。

于是 `redisGetReply` 最多阻塞 1.5 秒；如果这 1.5 秒内没人 PUBLISH，hiredis 超时返回 `rc != REDIS_OK` → loop 的 while 条件 break → loop 退出 → 重连循环看到「subscription lost」。

修复（`redis_pool.cc:127-128`）——连接成功后用 `{0, 0}` 清除 command_timeout：

```cpp
struct timeval zeroTv = {0, 0};
redisSetTimeout(ctx_, zeroTv);   // hiredis 语义: {0,0} = NULL = 无超时
```

这样 `redisGetReply` 永久阻塞等消息，连接真断了 hiredis 会返回错误触发重连。

### 教训

1. **hiredis 的 timeout 是个语义陷阱**：`redisConnectWithTimeout` 把同一个 tv 同时设为连接超时和命令超时——订阅场景需要「连接用超时、命令不超时」，但 API 里**没分开**，必须连接成功后手动清零；
2. **「连接成功」不等于「订阅稳定」**：bug 不是发生在建立连接时，而是在订阅后的 1.5 秒静默期。这种"延迟爆雷"特别容易让人误判成网络抖动；
3. **「subscription lost」未必是网络断了**：可能是客户端主动让 loop 退出的（超时、错误返回）。看日志先想"loop 是因为什么 break 的"，而不是"连接为什么掉了"；
4. **设计动机注释要早写**：`redis_pool.cc:123-126` 那段「关键: redisConnectWithTimeout 会把 tv 同时设为 command_timeout...」的注释，就是这个 bug 修复时加的。下次有人想"优化"删掉这行 `redisSetTimeout(zeroTv)`，看注释就停手了。

## 13.10 坑 10：裸 std::thread 调 `IOManager::GetThis()` 返回 nullptr → SIGSEGV

### 问题

修完坑 9（订阅不再 1.5 秒断开）之后，订阅本身稳定了——但**世界聊天一发消息 gate 立刻 Segmentation fault (core dumped)**。崩溃点不在订阅线程，而在订阅线程收到消息后的回调里。

### 误判

- 是不是 `m_accountToSession` 被并发访问出问题了（数据竞争）？
- 是不是 `ChatNotify` 反序列化 payload 出错了（UAF？越界）？
- 是不是 sendToSession 内部 sendQueue 的锁用错了（死锁导致崩溃）？

### 真相

gdb 加载 core dump 后 `bt` 看到的栈帧：

```
#0  ___pthread_mutex_lock (mutex=0x8)
#3  sylar::Scheduler::schedule (this=0x0)        ← IOManager 指针为空
#4  GateServer::sendToSession (gate_server.cc:194)
#5  startWorldChatSubscriber 的 lambda (订阅回调)
#10 Subscriber::loop
#11 std::thread::_M_run                          ← 裸线程, 非 IOManager worker
```

关键证据：`#3 sylar::Scheduler::schedule (this=0x0)`——`this` 是空指针。`schedule` 第一行要拿 `Scheduler` 的 `m_mutex`，地址 `0x8` 正是 `m_mutex` 在对象内的偏移量。所以崩在 `pthread_mutex_lock(0x8)`。

根因链：sylar 的 `IOManager::GetThis()` 用 thread-local（`t_scheduler`）实现——**只有 IOManager 自己 spawn 的 worker 线程才会被 `SetThis()` 设置这个值**。9.5.4 节解释了为什么订阅必须用裸 `std::thread`（hiredis `redisGetReply` 阻塞、sylar hook 不感知），但裸线程的 `t_scheduler` **从未被赋值** → `GetThis()` 返回 `nullptr`。

调用链：订阅回调里调 `sendToSession`（`gate_server.cc:177-204`）→ 内部 `sylar::IOManager::GetThis()->schedule([s]() {...})` → 解引用 nullptr → SIGSEGV。

修复（`gate_server.cc:919-944`）——在 gate IOManager 的协程上下文里（此时 `GetThis()` 有效）先把 iom 指针捕获进闭包，订阅线程用它调 schedule，把工作投递回 IOManager 的协程执行：

```cpp
auto iom = sylar::IOManager::GetThis();   // 在 IOManager 协程里调, GetThis() 有效
m_subThread = std::make_shared<std::thread>([self, iom]() {
    self->m_subscriber->subscribe("chat:world", [self, iom](const std::string& payload) {
        iom->schedule([self, payload]() {   // 投递到 IOManager 协程, GetThis() 才有效
            // sendToSession 在协程里跑, 不再依赖裸线程的 GetThis()
            // ...快照 m_accountToSession, 逐个 sendToSession
        });
    });
});
```

注意是**两层 schedule**：订阅线程拿到消息后 `iom->schedule(...)` 把工作投递回 gate 的 IOManager；实际 sendToSession 在 IOManager 的 worker 协程里跑，此时 `GetThis()` 才返回有效指针。

### 教训

1. **sylar 协程基础设施全部依赖 thread-local**：`IOManager::GetThis` / `Fiber::GetThis` / `Scheduler::GetThis` 都只在 IOManager worker 线程里有效。**任何用裸 `std::thread` 启动的线程都不能直接调这些 API**——必须通过捕获的 iom 指针间接访问，或 `iom->schedule(...)` 把工作投递回 IOManager；
2. **「能编译过」不代表「能跑」**：`GetThis()` 返回 `IOManager*`，语法上能调 `schedule`，编译器不会警告你这是空指针——直到运行时才崩；
3. **gdb 看 `this=0x0` 是金标准**：栈帧里 `this` 是 0，加上调用方在裸 `std::thread` 里（`std::thread::_M_run` 帧在栈上），立刻能定位到「thread-local 没设置」这个根因。比靠 print 排查快十倍；
4. **裸线程接入协程必须显式桥接**：本项目里订阅线程、（未来可能有的）定时任务线程、第三方库回调线程，凡是不在 IOManager worker 上的，**统一通过 `iom->schedule(...)` 桥接**回协程世界，不要直接调用协程 API。

---

# 第 14 章 贯穿全局的设计原则

读完上面这些代码，有几条**反复出现的设计原则**，是这套架构的灵魂：

## A. 协程栈很浅（1MB），一切深操作都要「重调度」

sylar 协程默认 1MB 栈。但一次 RPC 调用链（etcd 查址 + TCP 连接 + 发送 + 接收 + protobuf 序列化）深度很大。如果在游戏逻辑协程上直接做 RPC，会爆栈。

**全局解法**：把推送类 RPC `IOManager::schedule(...)` 到独立协程。gate/lobby/battle 全这么做。注释里反复出现「撑爆 1MB 栈」就是这个意思。

login 服特别深（ValidateToken 调 2 次 data RPC + EtcdClient 重对象），所以单独把栈调到 2MB。

## B. 锁内绝不阻塞/yield

协程框架的铁律：**持锁时一旦 yield，别的协程拿不到锁就死锁**。所以：
- 时间轮：锁内只收集回调，锁外投递；
- gate 发送队列：锁内只 push，send 在锁外；
- battle：broadcast 异步，锁内不 RPC；
- RedisPool：connect 在锁外（hook 下可能 yield）。

## C. 锁内快照 + 锁外 RPC

跨服务 RPC 绝不持锁。两个范本：
- lobby `tryStart`（第 6.2 节）：锁内拷贝玩家快照、置 started、释放，锁外 EnterBattle RPC，失败回滚；
- battle `saveGameRecordLocked`（第 10.5.6 节）：锁内拷贝玩家到 proto，锁外 schedule 落库。

## D. 短连接 vs 连接池的真实取舍

不是「连接池一定更好」。项目踩过坑：
- sylar hook 下 fd 复用有竞态（addEvent ASSERT），**gate 路径回退短连接**；
- 高频推送路径（lobby/battle）才用 `RpcChannelPool(etcd, 8)`，且**仅在推送闭包里用**；
- 连接池加了 64MB 响应大小边界保护防数据错位崩溃；
- 借出独占语义 + RAII Guard 保证不重复借出。

## E. 顺序保证靠延迟，不靠「希望」

battle 的 `SHOOT_RESULT_DELAY_MS`（2300ms）和首回合 `FIRST_TURN_READY_MS`（12000ms）都是显式延迟，保证客户端动画播完才切回合。

注释直说（`battle_room.cc:443-446`）：

```cpp
// 原实现 ShootResult 和 TurnStart 走独立异步 RPC 推送, 到达顺序无保证; 延迟 nextTurn
// 使 TurnStart 必然在 ShootResult 之后到达, 从根本上消除"一发射就切回合"的时序问题。
```

「希望异步 RPC 按顺序到达」是个错觉——必须有显式机制（延迟、序号、ACK）保证。

## F. 诚实标注占位与捷径

代码注释非常诚实：
- SHA1 占位 SHA256（`login_service.cc:15`）——sylar util 没 SHA256，演示用 SHA1；
- UpdateGender 自动 `ALTER TABLE` 兼容老库（`data_service.cc:170-174`）；
- SaveGameRecord 兼容旧两玩家 caller（`data_service.cc:296-309`）；
- 一连接一请求是因为 keep-alive 爆栈回退（`rpc_provider.cc:166-168`）；
- redis 用单连接 mutex 是"后续可换池"（已实现）；
- SHA1 转十六进制因为不能直接存 proto string。

这些都不是「完美设计」，而是**真实工程取舍**，对学习者极有价值。

## G. 自洽与隔离

- ORM 自带 `util.*` 不污染 sylar；
- etcd 客户端用 PImpl 隔离 C++17，保持框架 C++11 干净；
- 帧层用 `msg_id.h` 不依赖 proto 头；
- LD_PRELOAD 垫片不改 sylar 源码修复 gRPC close 崩溃；
- RedisPool 复用 ORM ConnectionPool 的设计模式。

每个模块都尽量「自给自足 + 不污染别人」。

## H. 协议向后兼容

proto3 加字段是非破坏性的：
- RpcHeader 加 trace_id（第 4 字段，旧 caller/callee 不影响）；
- GameRecordReq 加 players/winning_team（新字段 4/5，旧 caller 走旧路径）；
- 旧字段保留兼容旧 caller。

可以**逐服务灰度升级**，不需要全集群同时重编译。

## I. traceId 端到端的价值

本会话加的 traceId 不只是"加个字段"，是**可观测性**的基础设施：
- gate 入口生成 `g<gateId>-a<accountId>-m<msgId>-<seq>`；
- RpcHeader 透传到下游；
- 每个服务在日志里打 `[traceId]`；
- 出问题 grep traceId 就能看完整调用链。

学到的：可观测性要**端到端**，不能只在某一层加。一个 traceId 串起 5 个服务的日志，比 5 段孤立日志强一百倍。

## J. 异步派发是默认选择

「同步执行」在协程模型里是反模式（撑爆栈、持锁跨 yield 死锁）。**异步派发**（schedule 到独立协程）是默认选择：
- gate delSession 的断线清理 RPC；
- lobby push 闭包；
- battle broadcast；
- battle saveGameRecordLocked 落库；
- gate kickExistingSession 后的 close。

凡是会跨 yield 的操作，都 schedule 出去。

---

## 学习路径建议

按依赖顺序读：

1. `service_base.cc` —— 理解服务怎么起、配置怎么加载、信号怎么处理；
2. `common/frame.cc` + `msg_id.h` —— 理解通信协议；
3. `sylar/scheduler/fiber.cc` + `iomanager.cc` + `hook.cc` —— 理解协程模型（这一章读不透，后面全是黑盒）；
4. `gate_server.cc` —— 理解最复杂的并发（sendQueue、注册式分发、安全删除、心跳分片）；
5. `rpc_provider.cc` + `rpc_channel.cc` —— 理解 RPC（含 traceId 透传）；
6. `lobby_service.cc::tryStart` —— 理解「锁内快照+锁外 RPC」范式；
7. `battle_room.cc::onShoot` —— 理解游戏逻辑 + 物理串联 + 异步派发；
8. `data_service.cc::SaveGameRecord` —— 理解事务 + 批量插入；
9. `redis_pool.cc` —— 理解连接池的通用模式；
10. `scripts/fleet.sh` + `etcd_close_preload.cc` —— 理解运维与排障。

时间轮和 ORM 是独立的工具模块，可以随时穿插。

## 后记

这份笔记是边写代码边学习的过程记录。每个章节的「为什么」都是踩坑后回过头来才想清楚的——如果一开始就这么理解，能少走很多弯路。

特别值得记住的几点（这是反复出现、反复让人后悔的）：
- **协程栈很浅**，深调用链重调度；
- **锁内不 yield**，跨 RPC 不持锁；
- **顺序敏感的操作必须有注释**（先算伤害再挖坑、先摘索引再踢）；
- **客户端默认行为要审查**（Unity runInBackground）；
- **静态初始化期是最危险的窗口**（gRPC × sylar hook close）；
- **可观测性要端到端**（traceId）。

如果这份笔记能让下一个学协程微服务的人少踩一两个坑，它就值了。
