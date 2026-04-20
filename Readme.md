# 高性能服务器

## 1. 配置

### 1.1 库安装

~~~bash
## 放在include目录下面
# yaml-cpp
sudo apt update
sudo apt install -y libyaml-cpp-dev
https://github.com/jbeder/yaml-cpp

# boost
https://www.boost.org/releases/latest/
~~~

### 1.2 rl 编译与 HTTP 测试

~~~bash
# 1. 把http11_common.h http11_parser.h  httpclient_parser.h  httpclient_parser.rl  http11_parser.rl 放在sylar/http目录下

# 2. 安装ragel
sudo apt update
sudo apt install ragel
ragel -v

# 3. 编译
cd sylar/http
ragel -G2 -C http11_parser.rl -o http11_parser.rl.cc
ragel -G2 -C httpclient_parser.rl -o httpclient_parser.rl.cc

# HTTP请求测试
sudo apt install telnet
telnet www.baidu.com 80
GET / HTTP/1.0  # 两次回车
~~~

### 1.3 TcpServer / EchoServer 测试

~~~bash
# 1.tcp_server测试
./test_tcp_server
sudo apt install net-tools
netstat -ap | grep test_  # 查看
telnet 127.0.0.1 8080     # 另开会话连接 端口号在test_tcp_server.cc中修改

kill -9 <pid>
sudo rm -f /tmp/unix_addr # 解决unix套接字文件死锁

# 2. echo_server测试
./echo_server -t          # 文本
telnet 127.0.0.1 8020      # 另开会话连接 端口号在echo_server.cc中修改
./echo_server -b          # 二进制

# 3. 网页测试
ip addr  
# 找到inet [ip]/xx metric 100 brd xxx scope global dynamic eth0 # 我这里ip是192.168.139.145
# 打开网页输入 192.168.139.145:8020  或者是 192.168.139.145:8020/yinghuo?id=vvv#frag
~~~

### 1.4 HTTP_Server 测试

~~~bash
# 1.http_server测试
./test_http_server
ip addr  
找到 inet [ip]/xx metric 100 brd xxx scope global dynamic eth0 
# 我的IP：192.168.139.145
# 端口：8020（在 test_http_server.cc 里修改）
打开网页输入 192.168.139.145:8020              # ❌ 默认 404 Not Found
打开网页输入 192.168.139.145:8020/yinghuo      # ❌ 404（你没写这个路径）
打开网页输入 192.168.139.145:8020/yinghuo/xx   # ✅ 精准匹配（你代码里写了这个）
打开网页输入 192.168.139.145:8020/yinghuo/abc  # ✅ 通配符匹配（/* 匹配所有下级路径）
打开网页输入 192.168.139.145:8020/yinghuo/123  # ✅ 通配符匹配 
~~~

### 1.5 uri.rl.cc 编译

~~~bash
# 1. 把uri.h uri.rl 放在 sylar 目录下

# 2. 安装ragel
sudo apt update
sudo apt install ragel
ragel -v

# 3. 编译
cd sylar
ragel -G2 -C uri.rl -o uri.rl.cc
~~~

### 1.6 项目构建与测试

~~~bash
cd sylar/build
cmake ..
make -j4
cd ../bin
./test_xxx
~~~

### 1.7 压力测试
~~~bash
sudo apt install apache2-utils  # 压力测试工具
ab -V
cd bin
./test_http_server
ab -n 1000000 -c 200  "http://192.168.139.145:8020/" # 压测自己的

sudo apt install nginx
ab -n 1000000 -c 200  "http://192.168.139.145:80/" # 压测nginx
~~~

短连接：自己的 vs nginx 线程数：1 (修改my_http_server.cc中为false)
![alt text](./assets/stress_test_mine_1.png)
![alt text](./assets/stress_test_nginx_1.png)


长连接：自己的 vs nginx 线程数：1 (修改my_http_server.cc中为true)
![alt text](./assets/stress_test_mine_2.png)
![alt text](./assets/stress_test_nginx_2.png )

### 1.8 git操作

补充笔记

~~~bash
## 给两个空目录创建占位文件
touch include/boost/.gitkeep
touch include/yaml-cpp/.gitkeep

## 普通提交
git add .
git commit -m "xxx"
git push -f origin main

## 从上次conmmit取消并重新上传文件
git reset --mixed HEAD^
git rm -r --cached .
git add .
git commit -m "xxx"
git push -f origin main
~~~

---

## 2. 核心模块测试详解

### 2.1. 基础模块测试

*   **`test.cc` (日志系统基础测试)**
    *   **功能**：测试日志系统的核心功能，包括日志级别、输出到控制台、输出到文件（含按级别过滤）、以及格式化输出（`%d%T%p%T%m%n`）。
    *   **看点**：演示了流式输出宏 `SYLAR_LOG_INFO` 和类似 `printf` 的格式化宏 `SYLAR_LOG_FMT_ERROR` 的用法；展示了如何通过 `LoggerMgr` 单例获取命名的日志器。

![test](./assets/test.png)

*   **`test_config.cc` (配置系统与 YAML 热更新测试)**
    *   **功能**：全面测试基于 YAML 的强类型配置系统。
    *   **看点**：
        1.  演示了基本类型（`int`, `float`）、STL 容器（`vector`, `list`, `set`, `map`, `unordered_map`）作为全局配置项的静态注册。
        2.  展示了如何通过偏特化 `LexicalCast` 支持自定义复杂类（如 `Person` 对象及 `std::map<std::string, Person>`）的序列化与反序列化。
        3.  核心演示了**配置项变更回调函数 (`addListener`)**：当调用 `Config::LoadFromYaml` 加载文件覆盖默认值时，触发旧值与新值的对比及回调打印，这是实现服务热更新的关键。

![test_config](./assets/test_config.png)

* **`test_util.cc` (底层工具类与断言测试)**

  *   **功能**：测试系统级工具函数，主要关注调用栈追踪机制。
  *   **看点**：演示了宏 `SYLAR_ASSERT` 和 `SYLAR_ASSERT2`。当条件不满足时，程序不仅会 `assert` 终止，还会通过 `BacktraceToString` 打印出详细的多级 C++ 函数调用栈，极大方便了 Bug 定位。

  ![test_util](./assets/test_util.png)

### 2.2. 并发与协程模块测试

*   **`test_thread.cc` (多线程与锁机制测试)**
    *   **功能**：测试底层原生的多线程封装（`Thread` 类）以及互斥锁（`Mutex`）。
    *   **看点**：演示了多个线程同时对一个全局变量 `count` 进行累加（并发 100,000 次），通过 RAII 风格的 `sylar::Mutex::Lock` 保护临界区，最终输出的结果精准无误。同时测试了基于配置系统热加载动态调整日志器的多线程安全性。

![test_thread](./assets/test_thread.png)

*   **`test_fiber.cc` (纯协程上下文切换测试)**
    *   **功能**：验证最底层的非对称协程（Fiber）状态保存与恢复能力。
    *   **看点**：在单线程环境下，主执行流通过 `fiber->swapIn()` 切入子协程，子协程打印一段日志后通过 `Fiber::YieldToHold()` 主动让出 CPU 返回主执行流，往复穿插执行。直观展示了用户态上下文切换的过程。

![test_fiber](./assets/test_fiber.png)

*   **`test_scheduler.cc` (M:N 协程调度器测试)**  (最新版本不再支持测试)
    *   **功能**：验证 `Scheduler` 在多核物理线程上的任务分发与调度能力。
    *   **看点**：初始化一个拥有 3 个工作线程的调度器，并将一个会递归自我调度的协程任务投入队列。日志中将清晰地展示：**同一个协程在被挂起后重新唤醒时，可能会在不同的底层物理线程 ID 上被执行**，完美验证了 M:N 的跨线程动态调度模型。

### 2.3. Reactor 与 IO 异步模块测试

*   **`test_iomanager.cc` (Reactor 异步 IO 劫持测试)**
    *   **功能**：展示如何在协程框架下发起完全非阻塞的 TCP 客户端连接。
    *   **看点**：
        1.  将 Socket 设置为 `O_NONBLOCK`。
        2.  调用原生的 `connect`，此时立即返回 `EINPROGRESS`。
        3.  通过 `IOManager::addEvent` 向底层 `epoll` 注册可写事件 (`WRITE`)。此时在真实业务中，协程应 `Yield` 挂起。
        4.  当 TCP 三次握手成功，底层 `epoll_wait` 唤醒触发注册的回调函数。展示了异步事件驱动的编程范式。

![test_iomanager](./assets/test_iomanager.png)

*   **`test_timer.cc` (红黑树高精度定时器测试)**
    *   **功能**：验证基于 `std::set` 实现的高效定时器机制。
    *   **看点**：演示了如何在 `IOManager` 中添加一个间隔 1000ms 的循环定时器。并在回调中演示了如何通过捕获的智能指针 `s_timer` 执行 `reset` (改变触发周期) 或 `cancel` (彻底取消定时任务)。**代码中特别注释了闭包按引用捕获局部智能指针可能导致的严重宕机陷阱**。

![test_timer](./assets/test_timer.png)

*   **`test_hook.cc` (系统调用劫持极限测试)**
    *   **功能**：这是协程框架最核心的黑魔法测试，验证同步代码的异步非阻塞化。
    *   **看点**：
        1.  **睡眠劫持 (`test_sleep`)**：在调度器中同时丢入两个协程，分别 `sleep(2)` 和 `sleep(3)`。由于底层 Hook 将阻塞的 sleep 转换为了定时器 Yield，主线程不会卡死，总耗时为 3 秒而非 5 秒。
        2.  **网络劫持 (`test_sock`)**：直接使用标准的、看似阻塞的 C 语言 API (`socket`, `connect`, `send`, `recv`) 请求公网 HTTP 服务器。由于是在 `IOManager` 的上下文中执行，底层全自动转换为了异步 `epoll` 挂起与恢复，顺利拉取到网页源码，完美证明了 Hook 的绝佳稳定性。

![test_hook](./assets/test_hook.png)

### 2.4. 网络组件与应用层模块测试

*   **`test_address.cc` (网络地址解析测试)**
    *   **功能**：验证面向对象的 `Address` 模块的强悍解析能力。
    *   **看点**：演示了 `Address::Lookup` 自动发起 DNS 查询解析 `"www.baidu.com:ftp"`；演示了 `GetInterfaceAddresses` 遍历抓取本机的全部物理网卡名称及其 IPv4/IPv6 地址与子网掩码。

![test_address](./assets/test_address.png)

*   **`test_socket.cc` (Socket 面向对象封装测试)**
    *   **功能**：将之前的所有底层能力串联，发起一次完整的 HTTP 请求。
    *   **看点**：展示了标准的 Sylar 客户端编程范式：`LookupAnyIPAddress` -> `Socket::CreateTCP` -> `connect` (触发 Hook 非阻塞挂起) -> `send` -> `recv` (接收响应)。全程 0 阻塞，代码如丝般顺滑。

![test_socket](./assets/test_socket.png)

*   **`test_bytearray.cc` (内存池与序列化极限压测)**
    *   **功能**：验证 `ByteArray` 链表式内存池在极限内存碎片下的跨块读写正确性。
    *   **看点**：刻意将链表单个节点容量设置为极其变态的 `1` 字节。使用宏生成巨量随机正负数，混合使用 Varint / ZigZag 变长压缩与定长序列化，连续跨越成百上千个内存块进行读写校验。并测试了直接将其持久化到物理 `.dat` 文件中及无损恢复，验证了 `iovec` 零拷贝读取的绝对鲁棒性。

![test_bytearray](./assets/test_bytearray.png)

*   **`test_uri.cc` (URI 状态机解析测试)**
    *   **功能**：验证基于 Ragel 构建的 URL 解析引擎。
    *   **看点**：解析极其复杂的字符串 `"http://admin@www.yinghuo.website/test/中文/uri?id=100&name=sylar&vv=中文#frg中文"`。验证了框架对默认端口推导、以及对非常规中文字符直接提取的兼容能力。

![test_uri](./assets/test_uri.png)

*   **`test_http_parser.cc` (HTTP 报文极速解析测试)**
    *   **功能**：验证基于 Ragel 的 HTTP Request / Response 解析器。
    *   **看点**：传入一段硬编码的 HTTP 请求和响应报文（包含乱七八糟的 Headers）。调用 `parser.execute()` 一口吞下。观察日志输出的解析状态、正确的 Header 字典树提取结果，以及利用 `memmove` 将 Header 原地截断抹除、仅保留 Body 的完美操作。

![test_http_parser](./assets/test_http_parser.png)

### 2.5. 服务端架构测试

*   **`test_tcp_server.cc` (TCP 异步骨架测试)**
    *   **功能**：验证双 Reactor 模型的多路复用网络服务器启动逻辑。
    *   **看点**：在 `0.0.0.0:8080` 端口上发起并发绑定监听，如果端口被占用则自动 Sleep 并尝试重试绑定。演示了如何将繁琐的 Accept 和并发处理隐藏在 `server->start()` 的黑盒之后。

![test_tcp_server](./assets/test_tcp_server.png)

*   **`sylar/examples/echo_server.cc` (高性能回显服务器实战)**
    *   **功能**：基于 `TcpServer` 构建的真实业务应用。
    *   **看点**：重写了 `handleClient` 方法。展示了如何利用 `ByteArray::getWriteBuffers` 配合 `client->recv` 实现**零拷贝的网络流接收**，并将收到的客户端数据以文本或十六进制形式直接打印。可以通过 `telnet` 或 `nc` 命令与之进行高频并发交互测试。

![echo_server](./assets/echo_server.png)

*   **`test_http_server.cc` (HTTP 动态路由服务器实战)**
    *   **功能**：验证基于 Servlet 模型构建的高并发 Web 服务器。
    *   **看点**：
        1. 实例化 `HttpServer` 监听 `8020` 端口。
        2. 演示了轻量级的 Lambda 路由注册机制：
           * **精准路由**：`sd->addServlet("/yinghuo/xx", ...)`
           * **通配符模糊路由**：`sd->addGlobServlet("/yinghuo/*", ...)`
        3. 启动后，可用浏览器或 `curl` 高频请求这些路径，验证 `ServletDispatch` 的匹配优先级与请求分发正确性。

![http://192.168.139.145:8020/yinghuo](./assets/test_http_server_1.png)
![http://192.168.139.145:8020/yinghuo/xx](./assets/test_http_server_2.png)

*   **`test_http_connection.cc` (HTTP 连接池并发互调测试)**
    *   **功能**：验证作为微服务调用方的 `HttpConnectionPool`。
    *   **看点**：
        1. 演示了使用极简静态工具类 `HttpConnection::DoGet` 发起带超时控制的请求。
        2. 实例化一个连接池（最大连接10，保活30秒，每连接复用5次）。
        3. 启动后台循环定时任务，每秒从池中取连接发起请求。通过观察系统底层 `connect` 调用的频次，完美验证了长连接的保活复用机制以及超期淘汰算法的可靠性。

![test_http_connection_1](./assets/test_http_connection_1.png)

![test_http_connection_2](./assets/test_http_connection_2.png)

-----

## Version 1 : 日志系统 和 配置系统 (Log & Configuration)

本项目是一个基于 C++11 开发的高性能服务器框架，目前 Version 1 已完成**日志系统 (Log System)** 和**配置系统 (Configuration System)** 的开发，并实现了两者的深度整合。

### 核心架构类图

以下是当前系统核心模块的 UML 类图，展示了日志系统与配置系统的整体架构和类的依赖关系：

```mermaid
classDiagram
    %% =================全局工具函数 (命名空间封装)=================
    class sylar_util {
        <<namespace>>
        +GetThreadId(): pid_t $
        +GetFiberId(): uint32_t $
    }

    %% =================单例模式模块=================
    class Singleton~T,X,N~ {
        +GetInstance(): T* $
    }
    
    class SingletonPtr~T,X,N~ {
        +GetInstance(): std::shared_ptr~T~ $
    }

    %% =================日志系统模块 (Log System)=================
    class LogLevel {
        +enum Level
        +toString(level: LogLevel::Level): const char* $
        +fromString(str: const std::string&): LogLevel::Level $
    }

    class LogEvent {
        -m_file: const char*
        -m_line: int32_t
        -m_elapse: uint32_t
        -m_threadId: uint32_t
        -m_fiberId: uint32_t
        -m_time: uint64_t
        -m_ss: std::stringstream
        -m_logger: std::shared_ptr~Logger~
        -m_level: LogLevel::Level
        +LogEvent(logger: shared_ptr~Logger~, level: LogLevel::Level, file: const char*, line: int32_t, elapse: uint32_t, thread_id: uint32_t, fiber_id: uint32_t, time: uint64_t)
        +getFile(): const char* const
        +getLine(): int32_t const
        +getElapse(): uint32_t const
        +getThreadId(): uint32_t const
        +getFiberId(): uint32_t const
        +getTime(): uint64_t const
        +getContent(): std::string const
        +getLogger(): std::shared_ptr~Logger~ const
        +getLevel(): LogLevel::Level const
        +getSS(): std::stringstream&
        +format(fmt: const char*, ...): void
        +format(fmt: const char*, al: va_list): void
    }

    class LogEventWrap {
        -m_event: LogEvent::ptr
        +LogEventWrap(e: LogEvent::ptr)
        +~LogEventWrap()
        +getSS(): std::stringstream&
        +getEvent(): LogEvent::ptr
    }

    class FormatItem {
        <<interface>>
        +~FormatItem() virtual
        +format(os: std::ostream&, logger: std::shared_ptr~Logger~, level: LogLevel::Level, event: LogEvent::ptr): void *
    }

    class LogFormatter {
        -m_pattern: std::string
        -m_items: std::vector~FormatItem::ptr~
        -m_error: bool
        +LogFormatter(pattern: const std::string&)
        +init(): void
        +isError(): bool const
        +getPattern(): const std::string const
        +format(logger: std::shared_ptr~Logger~, level: LogLevel::Level, event: LogEvent::ptr): std::string
    }

    class LogAppender {
        <<interface>>
        #m_level: LogLevel::Level
        #m_formatter: LogFormatter::ptr
        #m_hasFormatter: bool
        +~LogAppender() virtual
        +log(logger: std::shared_ptr~Logger~, level: LogLevel::Level, event: LogEvent::ptr): void *
        +toYamlString(): std::string *
        +setFormatter(formatter: LogFormatter::ptr): void
        +getFormatter(): LogFormatter::ptr const
        +getLevel(): LogLevel::Level const
        +setLevel(level: LogLevel::Level): void
    }

    class StdoutLogAppender {
        +log(logger: Logger::ptr, level: LogLevel::Level, event: LogEvent::ptr): void
        +toYamlString(): std::string
    }

    class FileLogAppender {
        -m_filename: std::string
        -m_filestream: std::ofstream
        +FileLogAppender(filename: const std::string&)
        +log(logger: Logger::ptr, level: LogLevel::Level, event: LogEvent::ptr): void
        +toYamlString(): std::string
        +reopen(): bool
    }

    class Logger {
        -m_name: std::string
        -m_level: LogLevel::Level
        -m_appenders: std::list~LogAppender::ptr~
        -m_formatter: LogFormatter::ptr
        -m_root: Logger::ptr
        +Logger(name: const std::string&)
        +log(level: LogLevel::Level, event: LogEvent::ptr): void
        +debug(event: LogEvent::ptr): void
        +info(event: LogEvent::ptr): void
        +warn(event: LogEvent::ptr): void
        +error(event: LogEvent::ptr): void
        +fatal(event: LogEvent::ptr): void
        +addAppender(appender: LogAppender::ptr): void
        +delAppender(appender: LogAppender::ptr): void
        +clearAppenders(): void
        +getLevel(): LogLevel::Level const
        +setLevel(level: LogLevel::Level): void
        +getName(): const std::string& const
        +setFormatter(val: LogFormatter::ptr): void
        +setFormatter(val: const std::string&): void
        +getFormatter(): LogFormatter::ptr const
        +toYamlString(): std::string
    }

    class LoggerManager {
        -m_loggers: std::map~std::string, Logger::ptr~
        -m_root: Logger::ptr
        +LoggerManager()
        +init(): void
        +getLogger(name: const std::string&): Logger::ptr
        +getRoot(): Logger::ptr const
        +toYamlString(): std::string
    }

    %% 日志系统关系连线
    LogEventWrap ..> LogEvent : RAII封装管理
    LogFormatter "1" *-- "n" FormatItem : 解析为多项组合
    Logger "1" *-- "n" LogAppender : 派发日志
    Logger "1" *-- "1" LogFormatter : 拥有格式
    LogAppender <|-- StdoutLogAppender : 继承实现
    LogAppender <|-- FileLogAppender : 继承实现
    LogAppender "1" o-- "1" LogFormatter : 可选独立格式
    LoggerManager "1" *-- "n" Logger : 集中管理
    LoggerManager ..|> Singleton~LoggerManager~ : 单例实现类(LoggerMgr)

    %% =================配置系统模块 (Config System)=================
    class ConfigVarBase {
        <<interface>>
        #m_name: std::string
        #m_description: std::string
        +ConfigVarBase(name: const std::string&, description: const std::string&)
        +~ConfigVarBase() virtual
        +getName(): const std::string& const
        +getDescription(): const std::string& const
        +toString(): std::string *
        +fromString(val: const std::string&): bool *
        +getTypeName(): std::string *
    }

    class LexicalCast~F,T~ {
        <<template/functor>>
        +operator()(v: const F&): T
    }

    class ConfigVar~T~ {
        -m_val: T
        -m_cbs: std::map~uint64_t, on_change_cb~
        +ConfigVar(name: const std::string&, default_value: const T&, description: const std::string&)
        +toString(): std::string override
        +fromString(val: const std::string&): bool override
        +getValue(): const T& const
        +setValue(v: const T&): void
        +getTypeName(): std::string override
        +addListener(key: uint64_t, cb: on_change_cb): void
        +delListener(key: uint64_t): void
        +getListener(key: uint64_t): on_change_cb
        +clearListener(): void
    }

    class Config {
        -GetDatas(): ConfigVarMap& $ 
        +Lookup(name: const std::string&, default_value: const T&, description: const std::string&): ConfigVar~T~::ptr $
        +Lookup(name: const std::string&): ConfigVar~T~::ptr $
        +LoadFromYaml(root: const YAML::Node&): void $
        +LookupBase(name: const std::string&): ConfigVarBase::ptr $
    }

    %% 配置系统关系连线
    ConfigVarBase <|-- ConfigVar~T~ : 继承多态机制
    ConfigVar~T~ ..> LexicalCast~F,T~ : 依赖转换仿函数(支持特化)
    Config "1" *-- "n" ConfigVarBase : 通过静态局部字典管理
```

---

### 日志系统详细实现

日志系统支持多级别、多输出地、自定义格式化，并通过宏和流式输出提供极佳的开发者体验。

#### 核心组件设计

* **`LogLevel` (日志级别)**：定义了 `DEBUG`, `INFO`, `WARN`, `ERROR`, `FATAL` 五个级别，提供级别与字符串的相互转换功能。
* **`LogEvent` (日志事件)**：承载单次日志触发时的所有上下文信息，包括：文件名(`__FILE__`)、行号(`__LINE__`)、时间戳、线程ID、协程ID（预留）、所属日志器指针以及一个 `std::stringstream` 用于接收流式日志内容。
* **`LogEventWrap` (日志包装器 - RAII机制)**：
  * **实现细节**：通过宏（如 `SYLAR_LOG_INFO`）创建临时的 `LogEventWrap` 对象。其构造函数接收 `LogEvent`。
  * **巧妙之处**：宏展开后返回 `event->getSS()`，允许用户像使用 `std::cout` 一样使用 `<<` 拼接日志。当该行代码执行完毕，临时对象析构，在 `~LogEventWrap()` 中自动调用 `m_event->getLogger()->log()` 将这行拼接好的日志输出。
* **`LogFormatter` (日志格式器)**：
  * **实现细节**：负责将预设的字符串模板（如 `%d%T%p%T%m%n`）解析为具体的格式化项。
  * **解析模式 (Pattern)**：在 `init()` 函数中，通过状态机解析字符串，提取出普通字符和 `%` 开头的模式字符，并将其映射为内部类 `FormatItem` 的具体子类（例如解析 `%d` 生成 `DateTimeFormatItem`，解析 `%m` 生成 `MessageFormatItem`）。
* **`LogAppender` (日志输出地)**：
  * 抽象基类，自带独立的 `LogLevel` 和 `LogFormatter`。如果 Appender 未设置 Formatter，则默认使用父 `Logger` 的。
  * **`StdoutLogAppender`**：重写 `log()` 方法，将日志输出到 `std::cout`。
  * **`FileLogAppender`**：维护一个 `std::ofstream`，`reopen()` 负责打开文件，`log()` 方法将内容写入磁盘。
* **`Logger` (日志器)**：
  * 核心处理单元，拥有一个名称（默认为 "root"）。
  * **实现细节**：`log()` 方法首先检查传入事件的级别是否满足 `m_level`。满足则遍历内部所有的 `m_appenders`，依次调用它们的 `log()` 方法输出。如果没有配置 Appender，则会将日志转发给默认的 Root Logger。
* **`LoggerManager` (日志管理器)**：
  * **实现细节**：全局统一管理所有的 Logger。使用 `std::map<std::string, Logger::ptr>` 通过名称存储。如果通过 `getLogger("xxx")` 查找不存在，则自动创建并返回，确保随处可用。

---

### 配置系统详细实现

配置系统基于 `YAML` 构建，采用**约定优于配置**的理念，所有配置项在代码中强类型声明。

#### 核心组件设计

* **`ConfigVarBase` (配置项基类)**：
  * 采用**类型擦除**的设计模式，屏蔽了派生类的模板类型 `T`。
  * 包含配置项的名称（统一转小写，不区分大小写）和描述。提供纯虚函数 `toString()` 和 `fromString()`，供管理类在不知道具体类型的情况下进行统一的序列化/反序列化。
* **`LexicalCast` (词法转换模板类)**：
  * **实现细节**：基础类型通过 `boost::lexical_cast` 实现字符串与类型的互转。
  * **偏特化 (Partial Specialization)**：针对 STL 容器（`vector`, `list`, `set`, `unordered_set`, `map`, `unordered_map`）进行了大量模板偏特化。利用 `yaml-cpp` 将 YAML Node 与这些容器进行深度解析转换。
  * **自定义类型支持**：用户可以像代码中的 `Person` 类一样，实现全特化的 `LexicalCast`，即可让配置系统无缝支持业务层复杂对象的 YAML 注入。
* **`ConfigVar<T>` (模板配置项)**：
  * 继承自基类，真正存储配置项的值 `m_val`。
  * **事件回调机制**：维护了一个 `std::map<uint64_t, on_change_cb>` 监听器列表。在调用 `setValue(const T& v)` 时，如果发现新值与旧值不同，会遍历触发所有回调函数。这对于实现**配置热更新**（如端口号修改后自动重启监听）极其关键。
* **`Config` (全局配置管理)**：
  * **实现细节**：提供静态方法 `Lookup` 注册/获取配置。
  * **静态局部变量规避初始化顺序问题**：内部不使用普通的静态成员变量存储 map，而是使用 `GetDatas()` 函数返回局部静态变量的引用，完美避免了 C++ 跨编译单元的 Static Initialization Order Fiasco（全局变量初始化顺序灾难）问题。
  * **`LoadFromYaml`**：核心解析函数。先通过 `ListAllMember` 递归函数，将树形的 YAML 结构展平为带前缀的点号分割字符串（如将 YAML 中的 `system: { port: 80 }` 展平为键值对 `"system.port" -> 80`）。然后遍历注册的配置，通过基类接口 `fromString` 注入数据。

---

### 工具库与编译说明

#### 基础模块

* **`Singleton` (单例模式)**：提供 `Singleton` (返回指针) 和 `SingletonPtr` (返回智能指针) 模板类。通过额外的模板参数 `X` 和 `N`，允许为同一个类实例化出多个相互隔离的单例对象。
* **`util.cc` (系统级工具)**：封装了 `syscall(SYS_gettid)` 用于获取精准的真实线程 ID（相较于 `pthread_self` 更底层且便于日志排查）。

#### CMake 构建细节

在 `CMakeLists.txt` 中：

* 引入了自定义的 `cmake/utils.cmake` 中的 `force_redefine_file_macro_for_sources` 宏，强制重定义代码中的 `__FILE__` 宏，使得日志中输出的文件路径变为相对路径，避免了绝对路径泄露编译机信息且占用日志空间的缺点。
* 链接了第三方库 `-lyaml-cpp` 解析配置。

---

### 测试用例说明

* **`test.cc`**：测试了日志的流式输出宏、格式化宏（`FMT`）、多输出地（控制台+文件过滤）以及单例日志管理器的获取。
* **`test_config.cc`**：
  * 演示了如何静态注册基本类型、各类 STL 容器类型的全局配置项。
  * 演示了如何实现 `Person` 自定义类的 YAML 序列化特化。
  * 测试了配置项监听器 `addListener`，在调用 `LoadFromYaml` 覆盖配置时，触发了旧值到新值变更的日志打印。
  * 演示了日志系统与配置系统的初步结合（利用 YAML 解析结果生成内部状态日志）。

---

## Version 2: 线程 (Thread) 

随着框架向高并发演进，Version 2 引入了全面的线程管理和多维度锁机制。同时对之前的 **配置系统 (Config)** 和 **日志系统 (Log)** 进行了全面的线程安全升级。

### 线程模块核心类图

以下是线程模块独立的全量 UML 类图，严格包含了所有的方法签名、重载、静态方法、访问修饰符以及模板定义：

```mermaid
classDiagram
    %% =================信号量=================
    class Semaphore {
        -m_semaphore: sem_t
        +Semaphore(count: uint32_t)
        +~Semaphore()
        +wait(): void
        +notify(): void
        -Semaphore(const Semaphore&) : deleted
        -Semaphore(const Semaphore&&) : deleted
        -operator=(const Semaphore&) : deleted
    }

    %% =================RAII 锁模板=================
    class ScopedLockImpl~T~ {
        <<template>>
        -m_mutex: T&
        -m_locked: bool
        +ScopedLockImpl(mutex: T&)
        +~ScopedLockImpl()
        +lock(): void
        +unlock(): void
    }

    class ReadScopedLockImpl~T~ {
        <<template>>
        -m_mutex: T&
        -m_locked: bool
        +ReadScopedLockImpl(mutex: T&)
        +~ReadScopedLockImpl()
        +lock(): void
        +unlock(): void
    }

    class WriteScopedLockImpl~T~ {
        <<template>>
        -m_mutex: T&
        -m_locked: bool
        +WriteScopedLockImpl(mutex: T&)
        +~WriteScopedLockImpl()
        +lock(): void
        +unlock(): void
    }

    %% =================底层锁原语=================
    class Mutex {
        -m_mutex: pthread_mutex_t
        +Mutex()
        +~Mutex()
        +lock(): void
        +unlock(): void
    }

    class RWMutex {
        -m_lock: pthread_rwlock_t
        +RWMutex()
        +~RWMutex()
        +rdlock(): void
        +wrlock(): void
        +unlock(): void
    }

    class Spinlock {
        -m_mutex: pthread_spinlock_t
        +Spinlock()
        +~Spinlock()
        +lock(): void
        +unlock(): void
    }

    class CASLock {
        -m_mutex: volatile std::atomic_flag
        +CASLock()
        +~CASLock()
        +lock(): void
        +unlock(): void
    }

    class NullMutex {
        +NullMutex()
        +~NullMutex()
        +lock(): void
        +unlock(): void
    }

    class NullRWMutex {
        +NullRWMutex()
        +~NullRWMutex()
        +rdlock(): void
        +wrlock(): void
        +unlock(): void
    }

    %% =================线程封装=================
    class Thread {
        -m_id: pid_t
        -m_thread: pthread_t
        -m_cb: std::function~void()~
        -m_name: std::string
        -m_semaphore: Semaphore
        +Thread(cb: std::function~void()~, name: const std::string&)
        +~Thread()
        +getId(): pid_t const
        +getName(): const std::string& const
        +join(): void
        +GetThis(): Thread* $
        +GetName(): const std::string& $
        +SetName(name: const std::string&): void $
        -run(arg: void*): void* $
        -Thread(const Thread&) : deleted
        -Thread(const Thread&&) : deleted
        -operator=(const Thread&) : deleted
    }

    %% 关系连线
    ScopedLockImpl~T~ ..> Mutex : 依赖(typedef Lock)
    ScopedLockImpl~T~ ..> Spinlock : 依赖(typedef Lock)
    ScopedLockImpl~T~ ..> NullMutex : 依赖(typedef Lock)
    ReadScopedLockImpl~T~ ..> RWMutex : 依赖(typedef ReadLock)
    WriteScopedLockImpl~T~ ..> RWMutex : 依赖(typedef WriteLock)
    Thread *-- Semaphore : 组合(用于确保线程初始化同步)
```

### 线程模块详细实现细节

#### 1. 线程封装与 TLS (Thread Local Storage) 技术

* **核心类 `Thread`**：基于 POSIX pthread 库封装，弃用了 C++11 的 `std::thread`，原因是为了更底层地绑定系统内核级线程机制并设置名称。
* **TLS (线程局部存储)**：
  * 使用 `thread_local Thread* t_thread` 存储当前线程实例指针。
  * 使用 `thread_local std::string t_thread_name` 存储当前线程名称。
  * **作用**：让任何在这个线程上执行的代码，都可以通过静态方法 `Thread::GetName()` 和 `Thread::GetThis()` 以极低的代价获取当前线程上下文，这也是日志系统中 `%t` (线程ID) 快速打印的核心依赖。
* **同步启动机制 (`Semaphore`)**：
  * **痛点**：`pthread_create` 是异步的，创建线程后，如果主线程立刻去获取子线程的状态，子线程可能还没有执行到 `run` 函数的初始化逻辑，导致获取到错误信息。
  * **实现机制**：在 `Thread` 内部包含一个 `Semaphore`。`pthread_create` 之后主线程立刻调用 `m_semaphore.wait()` 阻塞。在新线程的 `run` 函数内部，完成 `pid` 抓取和名称设置后，调用 `m_semaphore.notify()` 唤醒主线程。这**绝对保证了**线程对象构造完成时，底层的内核线程已经完全初始化就绪。
* **系统级命名**：利用 `pthread_setname_np(pthread_self(), name)` 为底层线程设置名称（受 POSIX 限制最多 15 字符），使得在 Linux 下使用 `top -H` 或 `gdb` 时能直接看到有意义的线程名，极大方便调试。

#### 2. 多维度锁机制与 RAII 管理

本框架为了应对不同并发场景，提供了 6 种锁原语和 3 种 RAII 模板。

* **RAII 模板 (Resource Acquisition Is Initialization)**
  * 设计了 `ScopedLockImpl`, `ReadScopedLockImpl`, `WriteScopedLockImpl` 三个模板类。
  * **优点**：在构造函数中加锁，析构函数中解锁。即使业务代码中发生抛出异常、或者提前 `return`，也能利用 C++ 局部对象生命周期结束自动析构的特性保证锁一定被释放，**杜绝死锁**。
* **六种底层锁**：
  1. **`Mutex` (互斥锁)**：基于 `pthread_mutex_t`，最常规的锁，无论读写都阻塞，上下文切换开销较大。
  2. **`RWMutex` (读写锁)**：基于 `pthread_rwlock_t`。读锁共享，写锁独占。极其适合**读多写少**的场景。
  3. **`Spinlock` (自旋锁)**：基于 `pthread_spinlock_t`。在等待锁时不让出 CPU 时间片，而是死循环检测（忙等）。适合锁定范围极小、持有时间极短的场景。
  4. **`CASLock` (原子锁/乐观锁)**：基于 C++11 `std::atomic_flag` 的硬件级 CAS (Compare-And-Swap) 实现。比自旋锁更轻量，性能最高。
  5. **`NullMutex` & `NullRWMutex` (空锁)**：接口与普通锁一致但方法内为空。用于模板编程中，当使用者不需要线程安全时传入该类型，实现**零开销**。

### 跨模块集成：日志与配置的线程安全升级

在 Version 3 中，之前的模块被注入了并发安全能力：

#### 1. 配置系统 (Config) 的高并发改造

* **应用场景特点**：配置系统属于典型的**极度“读多写少”**场景（99.9%的业务在读取配置，极偶尔通过 YAML 热加载更新）。
* **改造细节**：
  * **注入锁类型**：为 `ConfigVar<T>` 注入了 `RWMutex` 读写锁。
  * **细粒度控制**：在 `ConfigVar::setValue()` 中，**对比新旧值以及触发回调函数的过程**被放入读锁作用域，只有真正执行 `m_val = v` 修改数据时，才切换为写锁作用域。
  * **全局配置字典锁**：全局 `s_datas` 映射表也加了 `RWMutex`。为防止跨编译单元锁的初始化顺序灾难，框架用 `GetMutex()` 返回局部静态锁 `static RWMutexType s_mutex`，保证安全。

#### 2. 日志系统 (Log) 的高性能改造

* **应用场景特点**：日志系统在服务运行期间会频繁写入，且多线程会争抢向同一个终端或文件输出字符。如果用 `Mutex` 会导致频繁的线程挂起和系统调用，严重拖慢业务。
* **改造细节**：
  * **注入锁类型**：为 `Logger`, `LogAppender`, `LoggerManager` 全部注入了 `Spinlock` (自旋锁)。
  * **锁范围优化**：写日志的过程通常只是字符串流转移或短时间的文件 I/O（有缓冲），非常迅速，用自旋锁替代互斥锁，避免了内核态陷入，极大提升了日志系统的吞吐率。
  * 解决了多线程并发写入日志时产生的“日志内容交错/乱码”问题。

---

## Version 3: 协程 (Fiber) 

本章节将深入剖析 `sylar` 框架的基石——协程模块。不同于简单的功能罗列，本章旨在揭示其**设计哲学、底层技术原理、关键实现细节以及针对复杂问题的解决方案**。

### 设计哲学：为何选择并如何实现协程

传统的网络编程模型（如多线程、异步回调）存在固有弊端：

*   **多线程模型**：线程是内核级资源，创建和上下文切换开销巨大。当并发量达到数万甚至更高时，系统资源消耗和调度开销将成为瓶颈。
*   **异步回调模型**：虽然性能高，但会导致“回调地狱 (Callback Hell)”，业务逻辑被拆分得支离破碎，代码可读性和可维护性极差。

`sylar` 框架的协程机制旨在结合两者的优点，规避其缺点，实现：

1.  **用户态调度**：协程是用户态的“轻量级线程”，创建和切换成本极低（仅涉及寄存器和栈指针的交换），可以轻松创建数百万个。
2.  **同步风格的异步编程**：允许开发者用看似同步的、顺序执行的代码，来编写本质上是异步的、非阻塞的逻辑。

本项目实现的是**非对称协程 (Asymmetric Coroutines)**，即协程的控制权只能在子协程和其调用者（通常是调度器主协程）之间转移，这使得调度关系清晰、易于管理。

---

### 底层基石：`ucontext.h` 与上下文切换原理

本协程库的核心是 POSIX 提供的 `ucontext.h` API，它允许程序在用户态保存和恢复完整的执行上下文。

*   **`ucontext_t` 结构体**：这是一个黑盒结构体，但其内部关键性地存储了：
    *   **CPU 寄存器**：包括指令指针 `rip`、栈顶指针 `rsp`、栈基址指针 `rbp` 以及所有通用寄存器。
    *   **`uc_stack`**：指向为该上下文分配的独立内存栈。
    *   **`uc_link`**：一个指向后继上下文的指针，当本上下文的入口函数执行完毕后，会自动切换到 `uc_link` 指向的上下文。
*   **核心函数三元组**：
    1.  **`getcontext(&ctx)`**：**“快照”**。将当前执行点的所有寄存器状态保存到 `ctx` 结构体中。
    2.  **`makecontext(&ctx, func, ...)`**：**“改装”**。修改一个已保存的 `ctx`，将其指令指针 `rip` 指向一个新的函数 `func`，并为其关联一个新分配的栈。**注意：它只能修改通过 `getcontext` 获取的上下文**。
    3.  **`swapcontext(&old_ctx, &new_ctx)`**：**“切换”**。这是原子操作，它将当前上下文保存到 `old_ctx`，然后立即从 `new_ctx` 中恢复上下文。这是协程 `Yield` 和 `Resume` 的直接实现。

---

### 全局与线程局部(TLS)状态：协程的“神经系统”

为了在不传递指针的情况下让代码感知当前所在的协程，框架巧妙地使用了全局和线程局部存储变量(TLS)：

*   **`static std::atomic<uint64_t> s_fiber_id / s_fiber_count`**：
    *   **原子性**：使用 `std::atomic` 是因为调度器可能在多个线程中创建协程，保证了 ID 分配和计数的线程安全。
*   **`static thread_local Fiber* t_fiber`**：
    *   **核心中的核心**。这是一个线程局部变量，它永远指向**当前线程上正在执行的那个协程**。`Fiber::GetThis()` 的高效实现完全依赖于此。
*   **`static thread_local Fiber::ptr t_threadFiber`**：
    *   **“锚点”**。它指向每个线程的**主协程**（即代表原始线程执行流的那个协程）。当一个子协程需要将控制权交还给“线程本身”而不是调度器时（例如 `back()` 操作），`t_threadFiber` 就是回归的目标。

---

### 内存管理：协程栈的创建与复用

*   **独立栈空间**：每个子协程都拥有独立的栈内存，通过 `MallocStackAllocator` (本质是 `malloc`) 分配。栈的大小由配置项 `fiber.stack_size` 决定，实现了灵活性。
*   **主协程的特殊性**：`Fiber::GetThis()` 在一个新线程上首次被调用时，会创建一个特殊的“主协程”。这个协程**不分配新栈**，而是直接“接管”当前线程的调用栈。这是协程系统启动的“第一推动力”。
*   **`reset(cb)` - 性能优化的关键**：
    *   **问题**：如果每次任务都 `new Fiber(...)`，在高并发下频繁 `malloc/free` 大块栈内存（通常是 1MB 或 2MB）会导致严重的性能抖动和内存碎片。
    *   **解决方案**：当一个协程执行完毕（状态变为 `TERM` 或 `EXCEPT`），调度器不会立即销毁它。而是通过 `reset(cb)` 方法，传入一个新的任务函数 `cb`，并重新调用 `makecontext` 来**复用这块已经分配好的栈内存**，实现了协程对象的池化，是高性能设计的体现。

---

### 方法剖析：解读关键函数的内部逻辑

#### `Fiber::GetThis()` - 智能的引导程序

此静态方法是用户与协程交互的主要入口之一，其内部逻辑极为精妙：

1.  检查 `t_fiber` (当前线程的当前协程)。
2.  若 `t_fiber` 非空，直接返回其 `shared_from_this()`。
3.  若 `t_fiber` 为空（**代表这是此线程第一次调用该函数**）：
    a.  创建一个新的协程实例 `main_fiber`，但调用的是**私有的无参构造函数 `Fiber()`**。
    b.  在此私有构造函数内，状态被置为 `EXEC`，并调用 `SetThis(this)` 将 `t_fiber` 指向自己。
    c.  `main_fiber` 不会分配新栈，它代表的就是当前线程的执行流。
    d.  将这个 `main_fiber` 赋值给 `t_threadFiber`，作为本线程的“锚点”。
    e.  断言 `t_fiber == main_fiber.get()` 确保初始化成功。
    f.  返回 `main_fiber`。

#### `swapIn()` vs `swapOut()` 与 `call()` vs `back()`

*   `swapIn()` / `swapOut()`：**为调度器服务**。`swapIn()` 是从调度器的主循环协程切换到业务协程；`swapOut()` 是从业务协程切回调度器的主循环协程。切换目标是固定的 `Scheduler::GetMainFiber()`。
*   `call()` / `back()`：**为非调度器场景服务**。例如在 `use_caller` 模式下，主线程的原始执行流需要和某个协程切换。`call()` 从主线程协程切入子协程，`back()` 从子协程切回主线程协程。切换目标是 `t_threadFiber`。

#### `MainFunc()` - 规避 `shared_ptr` 引用计数陷阱

这是整个协程模块**最精巧、最关键**的设计之一，完美解决了C++协程库中一个经典的内存泄漏问题。

* **问题场景**：

  ```cpp
  void Fiber::MainFunc() {
      Fiber::ptr cur = GetThis(); // cur的引用计数至少为1
      // ... 执行业务代码 cur->m_cb() ...
      cur->swapOut(); // <--- 问题点！
      // 此后的代码永远不会被执行
  }
  ```

  当 `cur->swapOut()` 执行后，CPU 的执行流已经跳转到了调度器协程，`MainFunc` 的栈帧被“冻结”。`cur` 这个栈上的 `shared_ptr` 对象永远没有机会被析构，导致它持有的引用计数永远无法释放，最终整个 `Fiber` 对象（包括其百万字节的栈）**永久泄漏**。

* **sylar 的解决方案**：

  ```cpp
  void Fiber::MainFunc() {
      // ... try-catch ...
      
      auto raw_ptr = cur.get();  // 1. 获取裸指针，不增加引用计数
      cur.reset();               // 2. 强制析构栈上的智能指针，引用计数-1
      raw_ptr->swapOut();        // 3. 使用裸指针进行上下文切换
  
      SYLAR_ASSERT2(false, "never reach here");
  }
  ```

  通过在切换前手动 `reset()` 智能指针，解除了当前栈帧对协程对象的引用，将生命周期管理完全交给了外部（如调度器的任务队列）。这是保障协程资源被正确回收的核心所在。

---

## Version 4: 协程调度 (Scheduler) 

随着框架向纯异步、高性能演进，Version 4 引入了**非对称协程 (Asymmetric Coroutine)** 机制，并实现了 **M:N 协程调度模型**（M 个协程在 N 个线程上动态调度），彻底改变了传统的基于回调的异步编程模式。

### 协程与调度模块核心类图

以下是严格包含所有方法签名、变量、访问修饰符和嵌套结构的 UML 类图：

```mermaid
classDiagram
    %% =================全局工具与断言宏=================
    class sylar_util {
        <<namespace>>
        +Backtrace(bt: std::vector~std::string~&, size: int, skip: int): void $
        +BacktraceToString(size: int, skip: int, prefix: const std::string&): std::string $
    }
    
    class sylar_macro {
        <<macro>>
        +SYLAR_ASSERT(x)
        +SYLAR_ASSERT2(x, w)
    }
    
    %% =================协程状态枚举=================
    class State {
        <<enumeration>>
        INIT
        HOLD
        EXEC
        TERM
        READY
        EXCEPT
    }

    %% =================协程模块 (Fiber)=================
    class Fiber {
        <<enable_shared_from_this>>
        -m_id: uint64_t
        -m_stacksize: uint32_t
        -m_state: State
        -m_ctx: ucontext_t
        -m_stack: void*
        -m_cb: std::function~void()~
        
        -Fiber()
        +Fiber(cb: std::function~void()~, stacksize: size_t, use_caller: bool)
        +~Fiber()
        +reset(cb: std::function~void()~): void
        +swapIn(): void
        +swapOut(): void
        +call(): void
        +back(): void
        +getId(): uint64_t const
        +getState(): State const
        
        +SetThis(f: Fiber*)$
        +GetThis(): Fiber::ptr$
        +YieldToReady(): void$
        +YieldToHold(): void$
        +TotalFibers(): uint64_t$
        +MainFunc(): void$
        +CallerMainFunc(): void$
        +GetFiberId(): uint64_t$
    }

    %% =================调度器模块 (Scheduler)=================
    class Scheduler {
        #m_threadIds: std::vector~int~
        #m_threadCount: size_t
        #m_activeThreadCount: std::atomic~size_t~
        #m_idleThreadCount: std::atomic~size_t~
        #m_stopping: bool
        #m_autoStop: bool
        #m_rootThread: int
        -m_mutex: MutexType
        -m_threads: std::vector~Thread::ptr~
        -m_fibers: std::list~FiberAndThread~
        -m_rootFiber: Fiber::ptr
        -m_name: std::string

        +Scheduler(threads: size_t, use_caller: bool, name: const std::string&)
        +~Scheduler() virtual
        +getName(): const std::string& const
        +GetThis(): Scheduler*$
        +GetMainFiber(): Fiber*$
        +start(): void
        +stop(): void
        +schedule~FiberOrCb~(fc: FiberOrCb, thread: int): void
        +schedule~InputIterator~(begin: InputIterator, end: InputIterator): void
        
        #tickle(): void virtual
        #run(): void
        #stopping(): bool virtual
        #idle(): void virtual
        #setThis(): void
        -scheduleNoLock~FiberOrCb~(fc: FiberOrCb, thread: int): bool
    }

    class FiberAndThread {
        <<struct>>
        +fiber: Fiber::ptr
        +cb: std::function~void()~
        +thread: int
        +FiberAndThread(f: const Fiber::ptr&, thr: int)
        +FiberAndThread(f: const std::function~void()~&, thr: int)
        +FiberAndThread(f: Fiber::ptr&&, thr: int)
        +FiberAndThread(f: std::function~void()~&&, thr: int)
        +FiberAndThread()
        +reset(): void
    }

    %% 关系连线
    Fiber *-- State : has a
    Scheduler "1" *-- "n" FiberAndThread : 维护任务队列
    FiberAndThread "1" o-- "1" Fiber : 包含协程指针
    Scheduler "1" *-- "n" Thread : 维护线程池
    Scheduler "1" *-- "1" Fiber : 拥有主协程(use_caller)

```

---

### 断言与工具链 (Macro & Util) 实现细节

为了保障协程框架在复杂上下文切换中的稳定性，引入了系统级调用栈追踪机制：

* **`Backtrace` 与 `BacktraceToString`**：
  * 底层调用 Linux/glibc 的 `<execinfo.h>` 库函数 `backtrace()` 抓取当前线程的调用栈指针数组。
  * 使用 `backtrace_symbols()` 将内存地址翻译为可读的函数名和偏移量字符串。
* **`SYLAR_ASSERT`**：
  * 通过宏定义实现严格的运行时检查。当条件不满足时，自动触发 `BacktraceToString` 获取堆栈信息，利用 Log 系统打印 `FATAL` 级错误后调用 `assert` 中止程序，极大提升了疑难 Bug 的排查效率。

---

### 协程模块 (Fiber) 详细实现细节

采用**非对称协程（Asymmetric Coroutines）**设计，子协程只能和创建它的父协程（或线程主协程）进行切换，职责清晰。

#### 核心组件设计

* **上下文切换 (`ucontext_t`)**：
  * 利用 `<ucontext.h>` 实现用户态的上下文保存与恢复。
  * `getcontext()` 保存当前 CPU 寄存器状态；`makecontext()` 绑定协程入口函数（如 `MainFunc`）和预先分配的独立内存栈；`swapcontext()` 实现原子级的“保存当前状态并加载新状态”。
* **双构造函数架构**：
  1. **私有无参构造 `Fiber()`**：仅用于将**当前线程的原始执行流**包装成第一个主协程。它不分配独立内存栈，直接接管当前执行流。
  2. **带参构造 `Fiber(cb, stacksize, use_caller)`**：用于创建真正的业务协程。通过 `MallocStackAllocator` 分配独立栈空间。
* **状态机设计**：
  * 维护了严格的状态流转：`INIT` -> `EXEC` -> `HOLD` / `READY` -> `TERM` / `EXCEPT`。
  * **协程重用 (`reset`)**：当协程处于 `TERM` 或 `INIT` 状态时，为了避免频繁的 `malloc/free` 栈内存带来的性能开销，允许直接传入新的回调函数 `cb`，重新调用 `makecontext` 复用已有的栈空间。
* **精妙的内存泄漏规避 (引用计数难题)**：
  * 在 `MainFunc` 协程入口函数中，业务执行完毕后协程需要切换回主协程。
  * **难点**：在 `MainFunc` 的栈中存在一个局部的 `Fiber::ptr cur = GetThis();`。如果在 `cur->swapOut()` 时直接切走，这个局部智能指针将永远无法析构（因为执行流再也不会回到这里），导致引用计数永远为 1，栈内存泄漏。
  * **破局方案**：提取裸指针 `auto raw_ptr = cur.get();`，显式调用 `cur.reset();` 将智能指针引用计数归零，最后通过 `raw_ptr->swapOut();` 安全切出。

---

### 调度器模块 (Scheduler) 详细实现细节

调度器实现了 **M:N 协程调度**，将 N 个协程任务均匀地分配给 M 个物理线程池执行。

#### 核心组件设计

* **`use_caller` (借用调用线程) 机制**：
  * 创建调度器时，如果 `use_caller` 为 `true`，调度器会将**创建调度器的那个主线程**也纳入线程池中（而不是纯粹只在后台新开线程）。
  * 为实现这点，调度器给主线程分配了一个特殊的 `m_rootFiber`。这个协程的入口是 `Scheduler::run`，但通过 `call()/back()` 机制与主线程的原始执行流进行切换，设计极其巧妙。
* **`FiberAndThread` (任务包装器)**：
  * 支持接收 `Fiber::ptr` 协程对象，或者原始的 `std::function<void()>` 回调。
  * 实现了**右值引用 (Rvalue Reference)** 的构造函数，支持 `std::move` 窃取资源，避免了将任务投入队列时产生不必要的拷贝开销。
  * 支持指定执行线程 `thread`（如果为 -1 则由任意线程抢占）。
* **核心调度循环 (`run()`)**：
  调度线程的核心就是一个 `while(true)` 死循环，内部逻辑如下：
  1. 加锁，从 `m_fibers` 队列中取出一个可执行的任务（匹配当前线程 ID 的，或未指定线程 ID 的）。
  2. 如果取到任务：
     * 若任务是协程对象：直接 `swapIn()` 切换过去执行。
     * 若任务是回调函数：为其分配一个协程对象，然后 `swapIn()`。
  3. 当协程 `Yield` 放弃 CPU 后（返回到 `run()`）：检查协程状态。如果是 `READY`，则重新放回任务队列尾部；如果是其他状态则保持 `HOLD` 挂起，等待其他地方唤醒。
  4. 如果队列中没有任务：线程切入 `idle()` 空闲协程，默认实现为不断地 `YieldToHold`（后续将在定时器和 IO 调度器中使用 `epoll_wait` 挂起，实现真正的睡眠休眠，降低 CPU 空转率）。
* **优雅退出机制 (`stop()`)**：
  * 置位 `m_autoStop` 标志，并向所有后台线程发送 `tickle()` 唤醒信号。
  * 如果启用了 `use_caller`，主线程会在这里陷入 `m_rootFiber->call()`，亲自动手帮忙处理剩下的任务，直到所有协程状态归位，最后逐一 `join()` 回收所有线程，实现无内存泄漏的优雅退出。

---

### 测试用例说明 (Tests)

* **`test_assert.cc`**：
  * 模拟异常条件，触发 `SYLAR_ASSERT2`。
  * 控制台将自动打印类似 `[FATAL] ASSERTION: 0 == 1 ... backtrace: ...` 的多级函数调用栈，验证了追踪系统的稳定性。
* **`test_fiber.cc`**：
  * 演示了最原始的单线程协程用法。
  * 主执行流通过 `swapIn()` 切入 `run_in_fiber`，子协程打印后通过 `YieldToHold()` 交还控制权，往复穿插执行。证明了协程在不涉及多线程条件下的状态保存能力。
* **`test_scheduler.cc`**：
  * 实例化 `sylar::Scheduler`（参数：3个工作线程，不使用调用者线程）。
  * 通过 `scheduler.schedule(&test_fiber)` 将回调任务投入调度池。
  * 在被调度的协程函数内部，利用静态变量 `s_count` 实现递归自我调度（倒数 5 次）。
  * 日志将清晰展示：同一个协程任务，在其挂起后被重新调度时，可能会在不同的底层 `thread_id` 上被执行，完美验证了 M:N 调度器的跨线程工作能力。

---

## Version 5: IO协程调度器与定时器 ( IOManager & Timer)

纯粹的协程调度器 (`Scheduler`) 只能解决 CPU 密集型任务的切换，但服务端开发面临的最大瓶颈是 **网络 IO 的阻塞**。
Version 5 通过引入 `epoll` 和 `pipe` 管道机制，将异步 IO、定时器与协程调度器完美融合，实现了一个真正的 **高性能 Reactor 协程模型**。当网络数据未就绪时，协程挂起并让出 CPU；当 `epoll` 检测到数据就绪时，自动唤醒对应的协程继续执行。

### IO与定时器模块核心类图

以下是严格包含所有结构体、枚举、方法签名、成员变量的详尽 UML 类图：

```mermaid
classDiagram
    %% =================工具函数补充=================
    class sylar_util {
        <<namespace>>
        +GetCurrentMS(): uint64_t $
        +GetCurrentUS(): uint64_t $
    }

    %% =================定时器模块 (Timer)=================
    class Comparator {
        <<struct>>
        +operator()(lhs: const Timer::ptr&, rhs: const Timer::ptr&): bool const
    }

    class Timer {
        <<enable_shared_from_this>>
        -m_recurring: bool
        -m_ms: uint64_t
        -m_next: uint64_t
        -m_cb: std::function~void()~
        -m_manager: TimerManager*
        
        -Timer(ms: uint64_t, cb: std::function~void()~, recurring: bool, manager: TimerManager*)
        -Timer(next: uint64_t)
        +cancel(): bool
        +refresh(): bool
        +reset(ms: uint64_t, from_now: bool): bool
    }

    class TimerManager {
        -m_mutex: RWMutexType
        -m_timers: std::set~Timer::ptr, Comparator~
        -m_tickled: bool
        -m_previouseTime: uint64_t
        
        +TimerManager()
        +~TimerManager() virtual
        +addTimer(ms: uint64_t, cb: std::function~void()~, recurring: bool): Timer::ptr
        +addConditionTimer(ms: uint64_t, cb: std::function~void()~, weak_cond: std::weak_ptr~void~, recurring: bool): Timer::ptr
        +getNextTimer(): uint64_t
        +listExpiredCb(cbs: std::vector~std::function<void()>~&): void
        +hasTimer(): bool
        
        #onTimerInsertedAtFront(): void *
        #addTimer(val: Timer::ptr, lock: RWMutexType::WriteLock&): void
        -detectClockRollover(now_ms: uint64_t): bool
    }

    %% =================IO调度器模块 (IOManager)=================
    class Event {
        <<enumeration>>
        NONE = 0x0
        READ = 0x1
        WRITE = 0x4
    }

    class EventContext {
        <<struct>>
        +scheduler: Scheduler*
        +fiber: Fiber::ptr
        +cb: std::function~void()~
    }

    class FdContext {
        <<struct>>
        +read: EventContext
        +write: EventContext
        +fd: int
        +events: Event
        +mutex: MutexType
        
        +getContext(event: Event): EventContext&
        +resetContext(ctx: EventContext&): void
        +triggerEvent(event: Event): void
    }

    class IOManager {
        -m_epfd: int
        -m_tickleFds: int[2]
        -m_mutex: RWMutexType
        -m_pendingEventCount: std::atomic~size_t~
        -m_fdContexts: std::vector~FdContext*~

        +IOManager(threads: size_t, use_caller: bool, name: const std::string&)
        +~IOManager()
        +addEvent(fd: int, event: Event, cb: std::function~void()~): int
        +delEvent(fd: int, event: Event): bool
        +cancelEvent(fd: int, event: Event): bool
        +cancelAll(fd: int): bool
        +GetThis(): IOManager* $
        +GetInstance(): IOManager* $
        +contextResize(size: size_t): void
        +stopping(timeout: uint64_t&): bool
        
        #tickle(): void override
        #stopping(): bool override
        #idle(): void override
        #onTimerInsertedAtFront(): void override
    }

    %% 关系连线
    TimerManager "1" *-- "n" Timer : 维护基于红黑树的定时器集合
    Timer ..> Comparator : 使用比较器进行排序
    IOManager --|> Scheduler : 继承核心调度能力
    IOManager --|> TimerManager : 继承定时器管理能力
    IOManager "1" *-- "n" FdContext : 维护文件描述符上下文
    FdContext "1" *-- "2" EventContext : 维护读/写独立的回调或协程
    FdContext ..> Event : 依赖枚举类型
```

---

### 定时器模块 (Timer) 实现深度剖析

定时器模块不依赖单独的线程循环，而是极其巧妙地融入到了 `IOManager` 的 `epoll_wait` 阻塞超时机制中。

#### 1. 数据结构的选择：红黑树 (`std::set`)

* **痛点**：高并发服务器中随时可能有成千上万个定时任务（如超时剔除、心跳检测），我们需要一种能**极其快速地找出最近一个要过期的定时器**的数据结构。
* **实现方案**：使用了 `std::set<Timer::ptr, Timer::Comparator>`。`std::set` 底层是红黑树，始终保持有序。
* **精妙的 `Comparator`**：比较器首先按照定时器的**绝对到期时间 (`m_next`)** 升序排列。最巧妙的是，如果两个定时器恰好在同一毫秒到期，比较器会回退到比较**内存地址 (`lhs.get() < rhs.get()`)**。这严格保证了 `set` 认为它们是两个不同的对象，避免了相同时间的定时器被非法覆盖剔除。

#### 2. 定时器核心机制

* **获取最近超时时间 (`getNextTimer`)**：O(1) 复杂度。由于红黑树有序，只需要取 `m_timers.begin()` 的时间戳减去当前时间，这就是 `epoll_wait` 下一次应该睡眠的时间。
* **获取已超时的任务 (`listExpiredCb`)**：
  * 当 `epoll_wait` 醒来时，调用此方法。
  * **设计亮点**：创建一个临时的“当前时间” dummy Timer 对象，利用 `std::set::lower_bound` 极其高效地进行二分查找，一刀将整棵红黑树切开：前面的全都是超时的，直接批量切割并提取 `cb` 回调函数塞入调度器，后面的继续保留。

#### 3. 解决服务器时间跳变 (Clock Rollover)

* 服务器运维时可能会遇到 NTP 时间同步或手动修改系统时间的情况。如果时间被强行往回调了一个月，基于绝对时间的定时器将全部失效卡死。
* **解决方案 (`detectClockRollover`)**：每次检查定时器时，对比上一次记录的时间。如果发现当前系统时间比上一次记录的时间**倒退了超过 1 小时**，则认为发生了时间重置。此时会无视到期时间，强行触发全部当前已有的定时器，防止死锁。

#### 4. 条件定时器 (`addConditionTimer`)

* 解决异步回调中对象生命周期的终极痛点：定时器触发时，对应的业务对象可能已经被销毁。
* **实现原理**：通过封装 `std::weak_ptr`。在真正执行 `cb` 之前，调用 `weak_cond.lock()` 尝试提升为强指针 `shared_ptr`。如果提升失败，说明对象已死，直接丢弃该任务；如果提升成功，说明对象依然存活，正常执行业务。

---

### IO协程调度器 (IOManager) 实现深度剖析

`IOManager` 是继承自 `Scheduler` 和 `TimerManager` 的集大成者，也是 `sylar` 框架进行网络编程的心脏。

#### 1. Reactor 架构的协程化改造

* **基础骨架**：初始化时创建了 `m_epfd`（epoll 句柄）和一对 `pipe` 管道（`m_tickleFds`）。
* **FdContext (句柄上下文)**：
  * 框架维护了一个一维数组 `m_fdContexts`，索引就是文件描述符 `fd`。这比使用 `map` 查询速度快几十倍，是典型的空间换时间。
  * 每个 `fd` 挂载两个 `EventContext`：分别对应读(`READ`)和写(`WRITE`)。里面保存着等待这个事件的 `Scheduler` 和被挂起的 `Fiber` 或 `Callback`。

#### 2. 最核心的方法：被重写的 `idle()`

* 父类 `Scheduler` 在没有任务时会不断让出 CPU，这会导致空转。`IOManager` 重写了 `idle()`，它是 `IOManager` 真正工作的核心循环：
* **运作流程**：
  1. 调用 `stopping(next_timeout)` 判断是否要停止，并顺便获取**离现在最近的一个定时器的超时时间**。
  2. 调用 `epoll_wait(m_epfd, events, 64, next_timeout)` 进入休眠。线程在这里挂起，不会消耗任何 CPU。
  3. **唤醒契机**有三个：① 定时器到期了；② 监听的网络 `fd` 来了数据；③ 有人往调度器里添加了新任务，调用了 `tickle()` 往管道 `m_tickleFds[1]` 里写了一个字节 `"T"`。
  4. 醒来后，先调用 `listExpiredCb` 将到期的定时器任务全部加入调度队列。
  5. 然后遍历 `epoll` 触发的事件列表：
     * 如果是管道读端可读，说明是 `tickle` 唤醒信号，采用 **ET (边缘触发) 模式** 利用 `while(read(...))` 将管道内积压的唤醒字节一次性全部抽干。
     * 如果是业务 `fd` 触发了可读或可写，则从 `FdContext` 中摘下对应被挂起的 `Fiber` 或 `Callback`。
     * 调用 `triggerEvent()` 强行将其塞入调度器的任务队列。
  6. **精妙的收尾**：`idle` 协程完成一轮事件分发后，立刻显式 `Yield` 切出。此时主调度 `run()` 函数会接管 CPU，开始飞速执行刚才被塞入队列的定时器任务和 IO 业务协程任务。

#### 3. 事件控制：`addEvent` vs `delEvent` vs `cancelEvent`

* **`addEvent`**：向 `epoll` 注册监听，并绑定当前正在执行的协程。随后用户代码就可以安心地去 `YieldToHold` 休眠了。
* **`delEvent`**：纯粹从 `epoll` 中删除监听。如果该事件关联的协程还在休眠，**它将永远不会被唤醒**（产生死协程）。
* **`cancelEvent`**：从 `epoll` 中删除监听，**同时强制触发一次回调/协程恢复**。这在实际业务中极其重要，例如在关闭 Socket 前，必须强制唤醒因等待读取而挂起的协程，让它执行收尾并正常退出，否则会造成严重的内存泄漏。

---

### 测试用例核心说明 (Tests)

#### `test_iomanager.cc`

* 揭示了在协程下发起非阻塞 TCP 连接的标准范式。
* **代码流转图**：
  1. 将 Socket 设置为非阻塞 (`O_NONBLOCK`)。
  2. 调用 `connect`，此时必定返回 `EINPROGRESS` (正在连接中)。
  3. 通过 `addEvent` 向 `IOManager` 注册 `WRITE` 可写事件监听。
  4. **重点注意**：在真实业务中，此时协程应调用 `YieldToHold()` 挂起自己。
  5. 当 TCP 三次握手成功（内核层面完成），该 socket 变为可写，`IOManager` 的 `idle` 会从 `epoll_wait` 醒来。
  6. `IOManager` 摘下挂起的任务投入队列，业务协程恢复执行，得知连接已建立。
  7. **避坑指南**：在执行 `close(sock)` 之前，务必调用 `cancelEvent` 注销尚未触发的事件，防止内核态句柄状态与用户态对象状态脱节。

#### `test_timer.cc`

* 演示了循环定时器的用法以及 **智能指针引发的深层悬空陷阱**。
* **避坑指南**：如果业务逻辑写在 lambda 表达式中，并且该 lambda 作为回调传给了脱离当前生命周期的后台定时器：
  * **绝不可按引用捕获** (`[&]`) 局部变量智能指针（如 `s_timer`）。因为触发时外层函数早已结束，局部指针已销毁，访问必报 `Segmentation fault`。
  * **正解**：将需要跨周期管理的智能指针提升为全局/类成员变量（如代码中提取为外部的 `s_timer`），确保其生命周期大于定时器的运行周期。

---

## Version 6: 系统调用劫持与句柄管理 (IO_Hook & FdManager)

### 版本差异与核心演进亮点

在上一个版本 (Version 5) 中，我们虽然实现了 `IOManager` 协程调度器，但业务代码依然面临一个**致命缺陷**：如果协程中调用了标准的 `read()`、`write()` 或 `sleep()` 等阻塞型系统 API，**整个底层物理线程将被操作系统挂起阻塞**。这导致该线程上的其他所有协程都无法被调度，协程的高并发优势荡然无存。

**Version 6 完美解决了这个问题，核心升级如下：**

1.  **动态链接库劫持 (IO Hook)**：利用 `dlsym` 劫持了 Glibc 的底层 Socket I/O 及睡眠相关的系统调用。让开发者**可以按照完全同步的思维写代码**，但在底层，框架会自动将阻塞调用转换为“非阻塞 I/O + `epoll` 监听 + 协程 `Yield` 挂起”。这是现代 C++ 协程库（如微信 libco、Sylar）的终极黑魔法。
2.  **句柄上下文管理 (FdManager)**：统一接管全站的文件描述符 (FD) 状态，记录其是否是非阻塞、是否是 Socket、以及单独的读写超时时间。
3.  **内存与拷贝控制规范 (`Noncopyable`)**：系统中的锁对象（如 `Mutex`、`Spinlock` 等）绝对不能被拷贝，否则会导致不可预期的死锁或崩溃。本版本剥离出了面向对象设计中经典的 `Noncopyable` 基类，大幅度净化了所有锁模块的基础代码。

---

### 模块全量核心类图 (UML)

以下是严格包含所有模块、枚举、方法签名、成员变量以及 Extern C 接口的详尽 UML 类图：

```mermaid
classDiagram
    %% =================基础规范模块=================
    class Noncopyable {
        <<class>>
        +Noncopyable() default
        +~Noncopyable() default
        -Noncopyable(const Noncopyable&) : deleted
        -operator=(const Noncopyable&) : deleted
    }

    %% 各类锁及信号量全部继承自 Noncopyable
    class Semaphore
    class Mutex
    class RWMutex
    class NullMutex
    class NullRWMutex
    class Spinlock
    class CASLock
    Noncopyable <|-- Semaphore
    Noncopyable <|-- Mutex
    Noncopyable <|-- RWMutex
    Noncopyable <|-- NullMutex
    Noncopyable <|-- NullRWMutex
    Noncopyable <|-- Spinlock
    Noncopyable <|-- CASLock

    %% =================句柄管理模块 (FdManager)=================
    class FdCtx {
        <<enable_shared_from_this>>
        -m_isInit: bool : 1
        -m_isSocket: bool : 1
        -m_sysNonblock: bool : 1
        -m_userNonblock: bool : 1
        -m_isClosed: bool : 1
        -m_fd: int
        -m_recvTimeout: uint64_t
        -m_sendTimeout: uint64_t

        +FdCtx(fd: int)
        +~FdCtx()
        +init(): bool
        +isInit(): bool const
        +isSocket(): bool const
        +isClose(): bool const
        +close(): bool
        +setUserNonblock(v: bool): void
        +getUserNonblock(): bool const
        +setSysNonblock(v: bool): void
        +getSysNonblock(): bool const
        +setTimeout(type: int, v: uint64_t): void
        +getTimeout(type: int): uint64_t
    }

    class FdManager {
        -m_mutex: RWMutexType
        -m_datas: std::vector~FdCtx::ptr~
        
        +FdManager()
        +get(fd: int, auto_create: bool): FdCtx::ptr
        +del(fd: int): void
    }

    %% =================系统调用劫持模块 (Hook)=================
    class sylar_hook {
        <<namespace>>
        +is_hook_enable(): bool $
        +set_hook_enable(flag: bool): void $
        +hook_init(): void $
    }

    class _HookIniter {
        <<struct>>
        +_HookIniter()
    }
    
    class timer_info {
        <<struct>>
        +cancelled: int = 0
    }

    class Extern_C_Syscalls {
        <<C API Intercept>>
        +sleep(seconds: unsigned int): unsigned int
        +usleep(usec: useconds_t): int
        +nanosleep(req: const struct timespec*, rem: struct timespec*): int
        +socket(domain: int, type: int, protocol: int): int
        +connect(sockfd: int, addr: const struct sockaddr*, addrlen: socklen_t): int
        +accept(s: int, addr: struct sockaddr*, addrlen: socklen_t*): int
        +read(fd: int, buf: void*, count: size_t): ssize_t
        +readv(fd: int, iov: const struct iovec*, iovcnt: int): ssize_t
        +recv(sockfd: int, buf: void*, len: size_t, flags: int): ssize_t
        +recvfrom(sockfd: int, buf: void*, len: size_t, flags: int, src_addr: struct sockaddr*, addrlen: socklen_t*): ssize_t
        +recvmsg(sockfd: int, msg: struct msghdr*, flags: int): ssize_t
        +write(fd: int, buf: const void*, count: size_t): ssize_t
        +writev(fd: int, iov: const struct iovec*, iovcnt: int): ssize_t
        +send(s: int, msg: const void*, len: size_t, flags: int): ssize_t
        +sendto(s: int, msg: const void*, len: size_t, flags: int, to: const struct sockaddr*, tolen: socklen_t): ssize_t
        +sendmsg(s: int, msg: const struct msghdr*, flags: int): ssize_t
        +close(fd: int): int
        +fcntl(fd: int, cmd: int, ...): int
        +ioctl(d: int, request: unsigned long int, ...): int
        +getsockopt(sockfd: int, level: int, optname: int, optval: void*, optlen: socklen_t*): int
        +setsockopt(sockfd: int, level: int, optname: int, optval: const void*, optlen: socklen_t): int
    }

    %% 关联
    FdManager "1" *-- "n" FdCtx : 管理全部句柄上下文
    FdManager ..|> Singleton~FdManager~ : 单例模式 (FdMgr)
    sylar_hook ..> _HookIniter : 通过静态构造执行Hook注入
```

---

### 核心实现细节与底层原理深度剖析

#### 1. `Noncopyable` 的引入与 C++ 资源管理哲学

* **设计初衷**：在多线程框架中，所有的同步原语（互斥锁、读写锁、自旋锁、信号量）都是独占性/状态敏感的系统资源。如果发生锁对象的拷贝或赋值，将导致严重的未定义行为（如解锁未加锁的内存、导致死锁等）。
* **实现细节**：新增 `Noncopyable` 基类，明确使用 C++11 的 `= delete` 语法禁用了**拷贝构造函数**和**赋值运算符**。随后，将 `Semaphore`、`Mutex` 等全部同步组件继承自该类。这不仅消除了每个类中重复编写 `= delete` 的冗余代码，更在编译期间杜绝了资源被意外拷贝的可能，提升了框架的工程级健壮性。

#### 2. `FdManager` 与 `FdCtx`：句柄状态的守护者

要实现 IO 协程化，框架必须知道每一个操作的 FD 是什么类型的：

* **`FdCtx` (文件描述符上下文)**：
  * **位域优化 (Bit-fields)**：巧妙使用了 `bool m_isInit: 1;` 位域语法，将 5 个布尔状态极致压缩到了同一个字节内，极大节省了高并发下的内存占用。
  * **非阻塞状态隔离**：分为 `m_sysNonblock`（系统层面是否非阻塞）和 `m_userNonblock`（用户层面是否希望非阻塞）。因为框架为了实现异步，会**强制将所有 Socket 在系统层面设置为 `O_NONBLOCK`**；但为了对用户保持透明，必须单独记录用户原本的设置（如果用户自己设置了非阻塞，框架将不再干预，直接原样返回 `EAGAIN` 让用户自己处理）。
* **`FdManager` (全局管理器)**：
  * 使用 `std::vector<FdCtx::ptr>` 以 FD 自身的值作为索引，实现了 $O(1)$ 的极限查询速度。利用读写锁保障多线程下的扩容安全。

#### 3. 动态劫持魔法：`_HookIniter` 与 `dlsym`

* **如何偷梁换柱？** 框架通过定义与标准 C 库同名的函数（例如 `extern "C" unsigned int sleep(unsigned int seconds)`）来覆盖系统调用。
* **找回真实函数**：利用宏 `HOOK_FUN(XX)` 结合 `dlsym(RTLD_NEXT, "sleep")` 动态查找符号表。`RTLD_NEXT` 告诉链接器：“不要找我当前重写的这个 `sleep`，去动态库加载链的下一个环节去找真实的 `glibc` 里的 `sleep`”。并将真实地址保存在 `sleep_f` 函数指针中。
* **时机控制**：定义了结构体 `_HookIniter` 并声明了全局静态变量 `static _HookIniter s_hook_initer;`。这确保了在 C++ 的 `main` 函数执行之前，动态链接劫持就已经初始化完毕。

#### 4. 终极奥义：`do_io` 异步转换模板函数

`do_io` 是本模块乃至整个框架最核心的黑魔法引擎。几乎所有的读写操作（`read`, `recv`, `send`, `write`）都被这个万能模板重写：

1. **拦截检查**：检查当前线程是否开启了 Hook（通过 `thread_local bool t_hook_enable` 判断）。如果没有开启，或者 FD 不是 Socket，或者用户自己设置了非阻塞，**直接调用真实系统函数后返回**。
2. **首次尝试**：调用真实的系统函数尝试读写。如果成功，或者被信号中断（`EINTR`，立刻重试），则直接返回结果。
3. **异步挂起 (`EAGAIN`)**：如果真实函数返回 `EAGAIN`，代表缓冲区空了（或满了），常规线程会在此死锁。**我们的魔法开始**：
   * 向 `IOManager` 的 `epoll` 树中注册该事件（例如等待 `EPOLLIN`）。
   * 获取指定的超时时间，若存在，则创建一个条件定时器。
   * 调用 `Fiber::YieldToHold()` **将当前协程挂起，让出 CPU 执行权**！
4. **被动唤醒与重试**：
   * 当未来某时刻网络数据到达，`IOManager` 的 `idle` 协程中的 `epoll_wait` 会苏醒。
   * 调度器会将本协程重新放入执行队列。
   * 本协程在 `Fiber::YieldToHold()` 之后恢复执行：首先检查是否因为超时被唤醒（通过判定 `timer_info->cancelled`）。
   * 若非超时，利用 `goto retry;` 重新发起真实系统调用。此时数据必然已经就绪，成功读取数据并返回给用户。
   * **结果**：对于上层业务开发者而言，这就是一行普通的 `recv` 代码，但底层已经完成了一次极其高效的 CPU 让渡与网络事件驱动切换。

#### 5. 特殊的 `connect_with_timeout` 实现

* `connect` 无法直接复用 `do_io`。因为非阻塞的 `connect` 会立即返回 `EINPROGRESS`（正在建立连接中），而不是 `EAGAIN`。
* **实现逻辑**：
  * 发起非阻塞 `connect` 后立刻返回。
  * 将 Socket 的 `WRITE` 可写事件注册到 `epoll`，并让出协程 `YieldToHold`。
  * 当 TCP 三次握手成功或失败时，Socket 会变得可写，触发 `epoll` 唤醒协程。
  * 协程苏醒后，使用 `getsockopt(fd, SOL_SOCKET, SO_ERROR, ...)` 读取内核底层的真实错误码。如果 `error == 0`，代表连接完美建立；否则连接失败。从而实现了带精准超时控制的 TCP 异步直连。

#### 6. 调度器全局联动 (`Scheduler::run`)

在 `scheduler.cc` 中，我们为 `Scheduler::run` 入口函数增加了 `set_hook_enable(true);`。

* **深刻意义**：这意味着，**只要是交由 `Scheduler` (及 `IOManager`) 调度执行的任何协程任务，都会自动享受 IO 劫持带来的非阻塞加成**。而独立于调度器之外的普通原生线程，则继续保持传统的阻塞 I/O 行为，互不干扰，极大提升了框架的侵入式兼容性。

---

### 单元测试核心分析 (test_hook.cc)

* **`test_sleep()`**：同时向调度器抛入两个协程任务，分别 `sleep(2)` 和 `sleep(3)`。由于系统 `sleep` 被劫持变为了底层定时器 + 协程 Yield，主线程不会被卡住。实际运行耗时为 3 秒（而非 2+3=5秒），完美验证了同步代码的异步并发执行。
* **`test_sock()`**：通过原始的 `socket` -> `connect` -> `send` -> `recv` 请求外部服务器（如百度或 Cloudflare）。代码看起来完全是**同步阻塞式**的经典网络编程，但底层已经全部被 `sylar` 转化为非阻塞状态机，顺利跑完了完整的 HTTP 请求流程，验证了网络 IO 劫持的绝佳稳定性。

---

## Version 7: 网络地址与套接字通信 (Address & Socket) 

### 版本差异与核心演进亮点

在前面的版本中，我们已经打通了底层协程调度与 IO 劫持，使底层具备了高性能并发能力。但对于上层业务开发而言，直接操作 Linux 原生的 `sockaddr` 结构体、处理大端小端字节序、调用繁琐的 `socket/bind/listen/connect` C 语言 API 依然非常痛苦且容易引发内存泄漏。

**Version 7 进行了全面的面向对象（OOP）抽象，核心升级如下：**

1. **CPU 分支预测优化 (`macro.h`)**：新增 `SYLAR_LIKELY` 和 `SYLAR_UNLIKELY` 宏。利用 GCC/LLVM 的 `__builtin_expect` 机制，在断言和网络异常判断中主动告诉 CPU 哪些是小概率事件，从而优化指令流水线，提升极限性能。
2. **字节序转换模块 (`endian.h`)**：引入统一的跨平台字节序转换模板，完美抹平 Host 字节序（通常是小端）与 Network 字节序（大端）的差异。
3. **强大的地址解析系统 (`Address`)**：将 `sockaddr_in`, `sockaddr_in6`, `sockaddr_un` 完美抽象。内置了强悍的 `getaddrinfo` 域名解析和 `getifaddrs` 本机网卡遍历功能。
4. **全功能 `Socket` 封装**：继承自 `Noncopyable` 和 `enable_shared_from_this`。通过 RAII 机制彻底杜绝了 FD 泄漏。完美融入了上一版本的 IO Hook 机制，提供了支持超时控制的异步 TCP/UDP 读写接口。

---

### 模块全量核心类图 (UML)

以下是严格包含所有模块、类、枚举、多态虚函数、重载方法及模板方法的详尽 UML 类图：

```mermaid
classDiagram
    %% =================宏与工具模块=================
    class sylar_macro {
        <<macro>>
        +SYLAR_LIKELY(x)
        +SYLAR_UNLIKELY(x)
        +SYLAR_ASSERT(x)
        +SYLAR_ASSERT2(x, w)
    }

    class sylar_endian {
        <<namespace>>
        +byteswap~T~(value: T): T $
        +byteswapOnLittleEndian~T~(t: T): T $
        +byteswapOnBigEndian~T~(t: T): T $
    }

    %% =================网络地址模块 (Address)=================
    class Address {
        <<abstract>>
        +~Address() virtual
        +Create(addr: const sockaddr*, addrlen: socklen_t): Address::ptr $
        +Lookup(result: std::vector~Address::ptr~&, host: const std::string&, family: int, type: int, protocol: int): bool $
        +LookupAny(host: const std::string&, family: int, type: int, protocol: int): Address::ptr $
        +LookupAnyIPAddress(host: const std::string&, family: int, type: int, protocol: int): IPAddress::ptr $
        +GetInterfaceAddresses(result: std::multimap~std::string, std::pair~Address::ptr, uint32_t~~&, family: int): bool $
        +GetInterfaceAddresses(result: std::vector~std::pair~Address::ptr, uint32_t~~&, iface: const std::string&, family: int): bool $
        +getFamily(): int const
        +getAddr(): const sockaddr* const *
        +getAddr(): sockaddr* *
        +getAddrLen(): socklen_t const *
        +insert(os: std::ostream&): std::ostream& const *
        +toString(): std::string
        +operator<(rhs: const Address&): bool const
        +operator==(rhs: const Address&): bool const
        +operator!=(rhs: const Address&): bool const
    }

    class IPAddress {
        <<abstract>>
        +Create(address: const char*, port: uint16_t): IPAddress::ptr $
        +broadcastAddress(prefix_len: uint32_t): IPAddress::ptr *
        +networdAddress(prefix_len: uint32_t): IPAddress::ptr *
        +subnetMask(prefix_len: uint32_t): IPAddress::ptr *
        +getPort(): uint32_t const *
        +setPort(v: uint16_t): void *
    }

    class IPv4Address {
        -m_addr: sockaddr_in
        +Create(address: const char*, port: uint16_t): IPv4Address::ptr $
        +IPv4Address(address: const sockaddr_in&)
        +IPv4Address(address: uint32_t, port: uint16_t)
        +getAddr(): const sockaddr* const override
        +getAddr(): sockaddr* override
        +getAddrLen(): socklen_t const override
        +insert(os: std::ostream&): std::ostream& const override
        +broadcastAddress(prefix_len: uint32_t): IPAddress::ptr override
        +networdAddress(prefix_len: uint32_t): IPAddress::ptr override
        +subnetMask(prefix_len: uint32_t): IPAddress::ptr override
        +getPort(): uint32_t const override
        +setPort(v: uint16_t): void override
    }

    class IPv6Address {
        -m_addr: sockaddr_in6
        +Create(address: const char*, port: uint16_t): IPv6Address::ptr $
        +IPv6Address()
        +IPv6Address(address: const sockaddr_in6&)
        +IPv6Address(address: const uint8_t[16], port: uint16_t)
        +getAddr(): const sockaddr* const override
        +getAddr(): sockaddr* override
        +getAddrLen(): socklen_t const override
        +insert(os: std::ostream&): std::ostream& const override
        +broadcastAddress(prefix_len: uint32_t): IPAddress::ptr override
        +networdAddress(prefix_len: uint32_t): IPAddress::ptr override
        +subnetMask(prefix_len: uint32_t): IPAddress::ptr override
        +getPort(): uint32_t const override
        +setPort(v: uint16_t): void override
    }

    class UnixAddress {
        -m_addr: sockaddr_un
        -m_length: socklen_t
        +UnixAddress()
        +UnixAddress(path: const std::string&)
        +getAddr(): const sockaddr* const override
        +getAddr(): sockaddr* override
        +getAddrLen(): socklen_t const override
        +setAddrLen(v: uint32_t): void
        +insert(os: std::ostream&): std::ostream& const override
    }

    class UnknownAddress {
        -m_addr: sockaddr
        +UnknownAddress(family: int)
        +UnknownAddress(addr: const sockaddr&)
        +getAddr(): const sockaddr* const override
        +getAddr(): sockaddr* override
        +getAddrLen(): socklen_t const override
        +insert(os: std::ostream&): std::ostream& const override
    }

    Address <|-- IPAddress
    Address <|-- UnixAddress
    Address <|-- UnknownAddress
    IPAddress <|-- IPv4Address
    IPAddress <|-- IPv6Address

    %% =================Socket 枚举提取=================
    class Type {
        <<enumeration>>
        TCP = SOCK_STREAM
        UDP = SOCK_DGRAM
    }

    class Family {
        <<enumeration>>
        IPv4 = AF_INET
        IPv6 = AF_INET6
        UNIX = AF_UNIX
    }

    %% =================Socket模块=================
    class Socket {
        <<Noncopyable, enable_shared_from_this>>
        
        -m_sock: int
        -m_family: int
        -m_type: int
        -m_protocol: int
        -m_isConnected: bool
        -m_localAddress: Address::ptr
        -m_remoteAddress: Address::ptr

        +CreateTCP(address: Address::ptr): Socket::ptr $
        +CreateUDP(address: Address::ptr): Socket::ptr $
        +CreateTCPSocket(): Socket::ptr $
        +CreateUDPSocket(): Socket::ptr $
        +CreateTCPSocket6(): Socket::ptr $
        +CreateUDPSocket6(): Socket::ptr $
        +CreateUnixTCPSocket(): Socket::ptr $
        +CreateUnixUDPSocket(): Socket::ptr $

        +Socket(family: int, type: int, protocol: int)
        +~Socket()
        +getSendTimeout(): int64_t
        +setSendTimeout(v: int64_t): void
        +getRecvTimeout(): int64_t
        +setRecvTimeout(v: int64_t): void
        +getOption(level: int, option: int, result: void*, len: size_t*): bool
        +getOption~T~(level: int, option: int, result: T&): bool
        +setOption(level: int, option: int, result: const void*, len: size_t): bool
        +setOption~T~(level: int, option: int, value: const T&): bool

        +accept(): Socket::ptr
        +bind(addr: const Address::ptr): bool
        +connect(addr: const Address::ptr, timeout_ms: uint64_t): bool
        +listen(backlog: int): bool
        +close(): bool

        +send(buffer: const void*, length: size_t, flags: int): int
        +send(buffers: const iovec*, length: size_t, flags: int): int
        +sendTo(buffer: const void*, length: size_t, to: const Address::ptr, flags: int): int
        +sendTo(buffers: const iovec*, length: size_t, to: const Address::ptr, flags: int): int
        +recv(buffer: void*, length: size_t, flags: int): int
        +recv(buffers: iovec*, length: size_t, flags: int): int
        +recvFrom(buffer: void*, length: size_t, from: Address::ptr, flags: int): int
        +recvFrom(buffers: iovec*, length: size_t, from: Address::ptr, flags: int): int

        +getRemoteAddress(): Address::ptr
        +getLocalAddress(): Address::ptr
        +getFamily(): int const
        +getType(): int const
        +getProtocol(): int const
        +isConnected(): bool const
        +isValid(): bool const
        +getError(): int
        +dump(os: std::ostream&): std::ostream& const
        +getSocket(): int const
        +cancelRead(): bool
        +cancelWrite(): bool
        +cancelAccept(): bool
        +cancelAll(): bool

        -initSock(): void
        -newSock(): void
        -init(sock: int): bool
    }

    Socket "1" o-- "2" Address : 缓存 Local/Remote Address
    Socket ..> Type : 依赖枚举
    Socket ..> Family : 依赖枚举
```

---

### 核心实现细节与底层原理深度剖析

#### 1. 分支预测黑魔法 (`SYLAR_LIKELY` / `SYLAR_UNLIKELY`)

* **原理**：现代 CPU 采用高度流水线化执行，遇到 `if-else` 会进行分支预测并提前将指令读入流水线。如果预测失败，CPU 必须清空整条流水线重新加载，性能惩罚极高（约消耗数十个时钟周期）。
* **实现**：引入 `__builtin_expect(!!(x), 0)` 包装断言 `SYLAR_ASSERT`。因为程序发生断言失败（宕机）的概率极小，告诉 CPU `!(x)` 大概率为 `false`，从而让 CPU 放心大胆地优先预取 `if` 外的正常逻辑指令。在 `Socket::newSock()` 等高频方法中也使用了该宏，榨干 CPU 极限性能。

#### 2. 大小端字节序转换 (`endian.h`)

* **网络编程痛点**：网络传输协议严格规定必须使用**大端序（Big-Endian）**。而我们常用的 x86 架构 CPU 几乎全是**小端序（Little-Endian）**。
* **实现细节**：利用 `<byteswap.h>` 中的 `bswap_16/32/64`。通过模板 `std::enable_if` 结合 `sizeof(T)` 实现了 `byteswap` 对所有整型的安全泛型重载。配合宏 `#if BYTE_ORDER == BIG_ENDIAN`，如果是本机是大端，`byteswapOnLittleEndian` 直接返回；若是本机是小端，则调用底层汇编指令级别的高效逆序互换。

#### 3. 地址抽象体系 (`Address` 模块)

该模块将错综复杂的底层的 `sockaddr` 系列结构体统一收口到了面向对象的继承体系中。

* **智能工厂 (`Address::Create`)**：通过读取传入的底层 C 结构体中的 `sa_family` 字段，配合 `switch` 语句，自动 `dynamic_cast` 实例化出对应的 `IPv4Address`, `IPv6Address` 或是 `UnixAddress` 智能指针对象。
* **超强域名解析 (`Address::Lookup`)**：
  * 基于 POSIX 强大的 `getaddrinfo` 函数实现。
  * **难点攻克**：不仅能解析 `www.baidu.com`，还能精准拆解带端口的复合字符串。通过扫描 `[` 和 `]`，完美兼容了解析 IPv6 特殊格式 `[2001:db8::1]:80`；通过扫描 `:` 处理了普通的 IPv4 `127.0.0.1:8080` 格式。将解析出的一组地址转换为 `Address::ptr` 列表返回。
* **本机网卡抓取 (`Address::GetInterfaceAddresses`)**：
  * 利用底层 `getifaddrs` 抓取本机所有的物理/虚拟网卡（如 `eth0`, `lo`）。
  * **位运算艺术 (`CountBytes`)**：如何根据底层的 netmask（如 `255.255.255.0`）推算出前缀长度（24）？利用了 Brian Kernighan 的经典位运算算法 `value &= value - 1`，该操作可以在 $O(N)$（N为二进制中1的个数）内极速统计出子网掩码中 1 的数量。
* **子网/广播地址计算**：
  * `networdAddress`（网络地址）：IP 与 掩码做 `&` 操作。
  * `broadcastAddress`（广播地址）：IP 与 反码做 `|` 操作。
* **IPv6 的字符串压缩打印 (`IPv6Address::insert`)**：
  * IPv6 地址极长，规范允许将连续的 `0` 压缩为 `::`（且只能压缩一次）。该方法内部维护了一个 `used_zeros` 状态机，遍历 8 个 16 位整数段，完美实现了遵循 RFC 规范的优雅打印。

#### 4. 极致封装的网络套接字 (`Socket` 模块)

`Socket` 将繁杂的句柄操作变为了极简的方法调用，且 **100% 融入了底层的 IO 协程调度框架**。

* **生命周期管理 (RAII)**：构造时初始化或绑定已有 fd，**析构函数 `~Socket()` 强制调用 `close()`**，彻底根治服务器开发最容易犯的 File Descriptor 泄漏问题。并且继承自 `Noncopyable`，防范误拷贝导致的多次 `close()`。
* **超时时间联动 (`getSendTimeout` / `setRecvTimeout`)**：
  * 这里不是直接修改底层 socket，而是拦截后与 Version 6 中的 `FdMgr` 句柄管理器联动，将超时状态保存在用户态上下文中。
* **连接与阻塞控制 (`connect`)**：
  * 当调用 `sock->connect(addr)` 时，内部会自动判断：如果指定了超时时间，将直接使用 `hook.cc` 中劫持包装好的 `connect_with_timeout`。这使得业务层用同步调用的写法，在底层却实现了 **协程 Yield 挂起等待 + Epoll 异步唤醒**，绝不阻塞当前线程。
* **Scatter/Gather IO（分散/聚集读写）支持**：
  * 除了提供普通的 `send/recv` 接收 `void* buffer`，还深度支持传入 `iovec*` 结构。这允许将内存中不连续的多块数据一次性抛给内核去发送（通过 `sendmsg`/`recvmsg`），减少系统调用次数，极大优化零拷贝网络传输性能。
* **懒加载地址缓存 (`getLocalAddress` / `getRemoteAddress`)**：
  * 当应用频繁调用获取对端 IP 时，没必要每次都下沉到内核去调 `getpeername`。
  * **优化细节**：只有在第一次调用时才会执行真实的系统调用，结果被智能工厂转化为 `Address::ptr` 后缓存在 `m_localAddress` 和 `m_remoteAddress` 成员变量中，后续调用 $O(1)$ 极速返回。
* **优雅的协程取消操作 (`cancelRead/Write`)**：
  * 当外部强行终止 Socket 时，调用此方法会直接打通到底层的 `IOManager::GetThis()->cancelEvent()`，强制触发 epoll 回调，让沉睡在当前 Socket 上的协程苏醒过来，妥善处理资源回收。

---

### 单元测试核心分析 (Tests)

#### `test_address.cc`

* **功能验证**：
  * 测试了基于 `Lookup` 的 DNS 域名解析，能够将 `"www.baidu.com:ftp"` 等字符串准确解析出真实的 IP 列表。
  * 测试了 `GetInterfaceAddresses`，成功遍历输出当前 Linux 系统上的所有网卡设备名及其关联的 IPv4 / IPv6 地址与子网前缀。
  * 测试了 `IPv4Address::Create` 的字符串反序列化创建。

#### `test_socket.cc`

* **功能验证 (The Ultimate Test)**：这是将前面的“协程调度”、“IO劫持”、“地址解析”、“Socket封装”融会贯通的终极测试。
* **代码流转图**：
  1. 创建 `IOManager` 启动协程 Reactor 模型。
  2. 调度业务协程 `test_socket`。
  3. `Address::LookupAnyIPAddress("www.baidu.com")` 自动发起 DNS 查询获取百度 IP。
  4. `Socket::CreateTCP` 创建面向对象的 TCP 句柄。
  5. `sock->connect()` 发起连接。此时由于底层开启了 Hook，当前协程会在 TCP 握手期间**非阻塞自动挂起**，绝不卡死线程。
  6. 握手成功协程苏醒，直接调用 `sock->send` 发送原生 HTTP GET 报文。
  7. `sock->recv` 等待响应（同样，若数据未到，协程自动挂起）。
  8. 收到响应，打印百度首页 HTML 源码，协程完美结束。

---

## Version 8: 序列化与字节流缓冲区 (ByteArray)

### 版本差异与核心演进亮点

在之前的版本中，我们通过 `Socket` 和 `IOManager` 实现了异步的网络收发。但是，直接发送和接收原始的 `char*` 缓冲存在极大的缺陷：一旦发生数据量突增，传统的连续内存（如 `std::vector<char>` 或 `std::string`）会频繁触发 `realloc`，导致巨量的内存拷贝，引起服务器性能骤降（CPU 抖动）。

**Version 8 彻底解决了内存与序列化问题，核心升级如下：**

1. **链表式内存池架构**：摒弃了连续内存。底层采用 `Node` 链表串联起一个个固定大小的内存块。按需扩容，**永远不会发生内存搬迁和拷贝**。
2. **Varint / ZigZag 极致压缩算法**：针对网络传输中常见的小整数，引入了 Google Protocol Buffers (Protobuf) 核心的 Varint 变长整型编码和 ZigZag 负数映射算法。可以将 64 位的整数极限压缩到 1~10 字节，大幅节省网络带宽。
3. **网络/主机字节序自动转换**：内置大小端转换机制（对接 Version 7 的 `endian.h`），确保序列化的数据在跨平台网络传输时完全一致。
4. **原生支持 `iovec` (Scatter/Gather I/O)**：提供 `getReadBuffers` 和 `getWriteBuffers`，直接提取链表底层碎片的真实物理地址。配合 `readv`/`writev` 系统调用，实现**零拷贝**级别的高效网络吞吐。

---

### 模块全量核心类图 (UML)

以下是严格包含所有内部结构、属性、数十个序列化重载方法的详尽 UML 类图：

```mermaid
classDiagram
    %% =================内部节点结构=================
    class Node {
        <<struct>>
        +ptr: char*
        +next: Node*
        +size: size_t
        +Node()
        +Node(s: size_t)
        +~Node()
    }

    %% =================ByteArray 核心类=================
    class ByteArray {
        -m_baseSize: size_t
        -m_position: size_t
        -m_capacity: size_t
        -m_size: size_t
        -m_endian: int8_t
        -m_root: Node*
        -m_cur: Node*

        +ByteArray(base_size: size_t)
        +~ByteArray()

        %% 固定长度写
        +writeFint8(value: int8_t): void
        +writeFuint8(value: uint8_t): void
        +writeFint16(value: int16_t): void
        +writeFuint16(value: uint16_t): void
        +writeFint32(value: int32_t): void
        +writeFuint32(value: uint32_t): void
        +writeFint64(value: int64_t): void
        +writeFuint64(value: uint64_t): void

        %% 可变长度写
        +writeInt32(value: int32_t): void
        +writeUint32(value: uint32_t): void
        +writeInt64(value: int64_t): void
        +writeUint64(value: uint64_t): void

        %% 浮点与字符串写
        +writeFloat(value: float): void
        +writeDouble(value: double): void
        +writeStringF16(value: const std::string&): void
        +writeStringF32(value: const std::string&): void
        +writeStringF64(value: const std::string&): void
        +writeStringVint(value: const std::string&): void
        +writeStringWithoutLength(value: const std::string&): void

        %% 固定长度读
        +readFint8(): int8_t
        +readFuint8(): uint8_t
        +readFint16(): int16_t
        +readFuint16(): uint16_t
        +readFint32(): int32_t
        +readFuint32(): uint32_t
        +readFint64(): int64_t
        +readFuint64(): uint64_t

        %% 可变长度读
        +readInt32(): int32_t
        +readUint32(): uint32_t
        +readInt64(): int64_t
        +readUint64(): uint64_t

        %% 浮点与字符串读
        +readFloat(): float
        +readDouble(): double
        +readStringF16(): std::string
        +readStringF32(): std::string
        +readStringF64(): std::string
        +readStringVint(): std::string

        %% 内部操作与游标控制
        +clear(): void
        +write(buf: const void*, size: size_t): void
        +read(buf: void*, size: size_t): void
        +read(buf: void*, size: size_t, position: size_t): void const
        +getPosition(): size_t const
        +setPosition(v: size_t): void
        +getSize(): size_t const
        +getBaseSize(): size_t const
        +getReadSize(): size_t const
        +isLittleEndian(): bool const
        +setIsLittleEndian(val: bool): void

        %% 文件 IO 与 字符串导出
        +writeToFile(name: const std::string&): bool const
        +readFromFile(name: const std::string&): bool
        +toString(): std::string const
        +toHexString(): std::string const

        %% Scatter/Gather IO 支持
        +getReadBuffers(buffers: std::vector~iovec~&, len: uint64_t): uint64_t const
        +getReadBuffers(buffers: std::vector~iovec~&, len: uint64_t, position: uint64_t): uint64_t const
        +getWriteBuffers(buffers: std::vector~iovec~&, len: uint64_t): uint64_t

        -addCapacity(size: size_t): void
        -getCapacity(): size_t const
    }

    ByteArray "1" *-- "n" Node : 底层采用单向链表维护内存碎片
```

---

### 核心算法与底层原理深度剖析

#### 1. 底层存储架构：固定大小的内存块链表

* **痛点**：由于网络数据流是源源不断的，传统的 `std::vector` 在容量不足时会申请一块原来 2 倍大的新内存，然后把老数据全部 `memcpy` 过去。这种**全量内存搬迁**是高并发服务器的性能杀手。
* **Sylar 解决方案 (`Node`)**：
  * 初始化时，分配一个容量为 `m_baseSize`（默认 4096 字节，刚好等于内存一页）的 `Node` 节点。
  * `m_root` 永远指向链表头部，`m_cur` 指向当前正在操作的节点。
  * **绝不搬迁**：当调用 `write()` 且当前节点容量不足时，`addCapacity()` 会计算需要的新节点数量，并在链表尾部挂载全新的 `Node`。新老数据物理上不连续，但在逻辑上通过偏移量 `m_position` 被完美抹平。

#### 2. 黑魔法一：Base128 Varint 变长整型压缩

* **现象**：业务中经常发送类似 ID=`15` 这样的整型。如果用 64 位整数发送，会产生 7 个字节的纯零（`00 00 00 00 00 00 00 0F`），白白浪费了 87% 的网络带宽。
* **Varint 实现原理 (`writeUint32` / `writeUint64`)**：
  * 将数字按 **7 位** 进行拆分。
  * 每个字节的 **最高位（MSB，第 8 位）** 作为标志位：`1` 表示“后面还有数据”，`0` 表示“这是最后一个字节”。
  * 代码实现中：`while(value >= 0x80) { tmp[i++] = (value & 0x7F) | 0x80; value >>= 7; }`。不断剥离后 7 位，给最高位补 1 压入缓冲区，直到数字本身小于 128。
  * **收益**：数字 `15` 仅需 1 个字节；极大地压缩了业务通讯的数据包体积。

#### 3. 黑魔法二：ZigZag 负数映射算法

* **现象**：Varint 虽然好，但是对**负数**极其致命。在计算机的补码表示中，负数（如 `-1`）的二进制全是 1（`FF FF FF FF FF FF FF FF`）。如果用 Varint 压缩负数，不仅无法压缩，反而会膨胀到 10 个字节！
* **ZigZag 算法原理 (`EncodeZigzag32` / `DecodeZigzag32`)**：
  * 将有符号整数映射为无符号整数，使得绝对值小的数字（无论正负）映射后依然是一个极小的值。
  * **映射表**：`0 -> 0`, `-1 -> 1`, `1 -> 2`, `-2 -> 3`, `2 -> 4`。
  * **Sylar 的巧妙编码**：
    * `if(v < 0) return (uint32_t)(-v) * 2 - 1; else return v * 2;`
    * **解码公式**：`return (v >> 1) ^ -(v & 1);`。利用了 C++ 的算术右移和负数补码特性，仅用一行极其优雅的位运算便完美还原了原始正负号。
  * **流程**：`writeInt32` 会先对数字进行 ZigZag 编码转成无符号数，然后再用 Varint 进行极致压缩。

#### 4. 浮点数的二进制剥离

* 浮点数 `float` 和 `double` 无法直接进行位运算（不能移位）。
* **Sylar 实现 (`writeFloat` / `writeDouble`)**：
  * 极其硬核地使用 `memcpy(&v, &value, sizeof(value));`，直接剥离内存底层的 IEEE 754 标准二进制序列，将其强行转化为无符号整数，然后调用底层的写函数。完美绕过了编译器的类型安全检查，实现了极速序列化。

#### 5. 跨越内存碎片的读写逻辑 (`write` / `read`)

由于内存被分割成了一个个 4096 字节的 `Node` 碎片，当我们想写入一段长达 10000 字节的数据时，必然会跨越多个节点。

* **算法实现**：
  * `npos = m_position % m_baseSize;` 计算出在当前小碎块内的偏移量。
  * `ncap = m_cur->size - npos;` 计算出当前小碎块还剩多少容量。
  * 利用 `while(size > 0)` 循环。如果剩余容量 `ncap` 够用，直接 `memcpy` 并更新指针；如果不够，先把当前碎块填满（`memcpy(..., ncap)`），然后触发 `m_cur = m_cur->next;`，切到下一个节点继续填，直到全部写完。

#### 6. 零拷贝核心：`iovec` 的神级集成

* 当 `Socket` 要发送 `ByteArray` 里的数据时，如果把链表里的碎片拼接成一块完整的内存再发送，就又产生了内存拷贝。
* **`getReadBuffers` 实现细节**：
  * 根据当前 `m_position`，遍历链表节点。
  * 对于每一个带有有效数据的 `Node`，将其内存的首地址 `cur->ptr + npos` 赋给系统结构体 `iovec.iov_base`，数据长度赋给 `iovec.iov_len`。
  * 最终返回一个 `vector<iovec>`。此时只需调用一次系统 API `writev`（聚集写），Linux 内核网卡驱动会自动去这多个物理不连续的内存地址里抓取数据发送。**全程 0 次用户态内存拷贝**！

#### 7. 游标控制 (`setPosition`) 边界坑点

* 当通过 `setPosition` 强行重置读写游标时，需要将 `m_cur` 指针挪到对应的链表节点。
* **代码高亮**：
  * 循环递减 `v -= m_cur->size; m_cur = m_cur->next;`。
  * **临界判断**：如果重置后 `v` 刚好等于 `m_cur->size`，说明游标恰好落在当前块的绝对末尾。如果不额外处理，下一次写入就会越界崩溃。代码中通过 `if(v == m_cur->size) m_cur = m_cur->next;` 完美处理了这一隐蔽的边界灾难。

---

### 单元测试核心分析 (Tests)

#### `test_bytearray.cc`

* **随机数据鲁棒性测试**：
  * 测试代码通过宏 `XX(type, len, write_fun, read_fun, base_len)` 生成大批量的 `rand()` 随机正负数、不同类型的整数。
  * 将 `m_baseSize` 刻意设置为极限的 `1` 字节。这意味着，就算写入一个 `uint64_t` 的 8 字节整数，也会**被迫跨越 8 个链表节点**，对 `write` 和 `read` 跨块处理逻辑进行了最严苛的极限压力测试。
  * 测试结果：写入后再 `setPosition(0)` 重新读取，并配合 `SYLAR_ASSERT`，保证了百万次跨内存碎片读写的零误差。
* **持久化能力测试**：
  * 调用 `writeToFile` 将极其零碎的链表数据连续地写入物理磁盘的 `.dat` 文件。
  * 创建一个崭新的 `ByteArray` 调用 `readFromFile` 加载数据，通过对比两者的 `toString()` 判断完全一致，验证了文件流导出/导入的绝对可靠性。

---

## Version 9: HTTP 协议封装 (HTTP)

### 版本差异与核心演进亮点

如果想要在我们的协程框架之上搭建高吞吐量的 Web 框架，就必须具备极速解析 HTTP 报文的能力。

**Version 9 的核心升级如下：**

1. **全面且强类型的 HTTP 协议封装**：通过宏自动生成了全量 HTTP Method 和 Status Code 的枚举与字符串映射，彻底消除了魔法数字 (Magic Number)。封装了 `HttpRequest` 和 `HttpResponse` 面向对象体系。
2. **基于 Ragel 状态机的解析引擎**：抛弃了低效的正则和字符串切分。引入了 Ragel 语法文件 (`.rl`)，通过编译生成 C/C++ 代码级别的高效状态机 (`.rl.cc`)，实现了逐字节级别的状态跳转解析，性能达到了 Nginx / Node.js 的同等级别。
3. **零拷贝设计与原地截断**：解析器在提取 URI、Header 等字段时，通过指针 `at` 和长度 `length` 进行标记，并在解析完成后通过 `memmove` 将未处理的 Body 数据原地前移，最大程度降低了内存申请和拷贝开销。
4. **无缝集成全局配置系统**：利用之前开发的 `Config` 模块，将请求/响应的最大缓冲区大小、最大 Body 长度全部实现了热更新配置管理。

---

### 模块全量核心类图 (UML)

以下是严格包含所有枚举、结构体、类模板、虚函数、回调函数及成员变量的详尽 UML 类图：

```mermaid
classDiagram
    %% ================= HTTP 宏与枚举 =================
    class HttpMethod {
        <<enumeration>>
        DELETE, GET, HEAD, POST, PUT, CONNECT...
        INVALID_METHOD
    }

    class HttpStatus {
        <<enumeration>>
        CONTINUE, OK, BAD_REQUEST, NOT_FOUND, INTERNAL_SERVER_ERROR...
    }

    class HttpUtil {
        <<namespace>>
        +StringToHttpMethod(m: const std::string&): HttpMethod $
        +CharsToHttpMethod(m: const char*): HttpMethod $
        +HttpMethodToString(m: const HttpMethod&): const char* $
        +HttpStatusToString(s: const HttpStatus&): const char* $
    }

    class CaseInsensitiveLess {
        <<struct>>
        +operator()(lhs: const std::string&, rhs: const std::string&): bool const
    }

    %% ================= 模板方法 =================
    class MapHelper {
        <<namespace>>
        +checkGetAs~MapType, T~(m: const MapType&, key: const std::string&, val: T&, def: const T&): bool $
        +getAs~MapType, T~(m: const MapType&, key: const std::string&, def: const T&): T $
    }

    %% ================= HTTP 实体对象 =================
    class HttpRequest {
        -m_method: HttpMethod
        -m_version: uint8_t
        -m_close: bool
        -m_path: std::string
        -m_query: std::string
        -m_fragment: std::string
        -m_body: std::string
        -m_headers: MapType
        -m_params: MapType
        -m_cookies: MapType
        
        +HttpRequest(version: uint8_t, close: bool)
        +getMethod(): HttpMethod const
        +getVersion(): uint8_t const
        +getPath(): const std::string& const
        +getQuery(): const std::string& const
        +getBody(): const std::string& const
        +getHeaders(): const MapType& const
        +getParams(): const MapType& const
        +getCookies(): const MapType& const
        +setMethod(v: HttpMethod): void
        +setVersion(v: uint8_t): void
        +setPath(v: const std::string&): void
        +setQuery(v: const std::string&): void
        +setFragment(v: const std::string&): void
        +setBody(v: const std::string&): void
        +isClose(): bool const
        +setClose(v: bool): void
        +setHeaders(v: const MapType&): void
        +setParams(v: const MapType&): void
        +setCookies(v: const MapType&): void
        +getHeader(key: const std::string&, def: const std::string&): std::string const
        +getParam(key: const std::string&, def: const std::string&): std::string const
        +getCookie(key: const std::string&, def: const std::string&): std::string const
        +setHeader(key: const std::string&, val: const std::string&): void
        +setParam(key: const std::string&, val: const std::string&): void
        +setCookie(key: const std::string&, val: const std::string&): void
        +delHeader(key: const std::string&): void
        +delParam(key: const std::string&): void
        +delCookie(key: const std::string&): void
        +hasHeader(key: const std::string&, val: std::string*): bool
        +hasParam(key: const std::string&, val: std::string*): bool
        +hasCookie(key: const std::string&, val: std::string*): bool
        +checkGetHeaderAs~T~(key: const std::string&, val: T&, def: const T&): bool
        +getHeaderAs~T~(key: const std::string&, def: const T&): T
        +checkGetParamAs~T~(key: const std::string&, val: T&, def: const T&): bool
        +getParamAs~T~(key: const std::string&, def: const T&): T
        +checkGetCookieAs~T~(key: const std::string&, val: T&, def: const T&): bool
        +getCookieAs~T~(key: const std::string&, def: const T&): T
        +dump(os: std::ostream&): std::ostream& const
        +toString(): std::string const
    }

    class HttpResponse {
        -m_status: HttpStatus
        -m_version: uint8_t
        -m_close: bool
        -m_body: std::string
        -m_reason: std::string
        -m_headers: MapType

        +HttpResponse(version: uint8_t, close: bool)
        +getStatus(): HttpStatus const
        +getVersion(): uint8_t const
        +getBody(): const std::string& const
        +getReason(): const std::string& const
        +getHeaders(): const MapType& const
        +setStatus(v: HttpStatus): void
        +setVersion(v: uint8_t): void
        +setBody(v: const std::string&): void
        +setReason(v: const std::string&): void
        +setHeaders(v: const MapType&): void
        +isClose(): bool const
        +setClose(v: bool): void
        +getHeader(key: const std::string&, def: const std::string&): std::string const
        +setHeader(key: const std::string&, val: const std::string&): void
        +delHeader(key: const std::string&): void
        +checkGetHeaderAs~T~(key: const std::string&, val: T&, def: const T&): bool
        +getHeaderAs~T~(key: const std::string&, def: const T&): T
        +dump(os: std::ostream&): std::ostream& const
        +toString(): std::string const
    }

    %% ================= C语言风格回调 =================
    class ParserCallbacks {
        <<C Functions>>
        +on_request_method(data: void*, at: const char*, length: size_t): void $
        +on_request_uri(data: void*, at: const char*, length: size_t): void $
        +on_request_fragment(data: void*, at: const char*, length: size_t): void $
        +on_request_path(data: void*, at: const char*, length: size_t): void $
        +on_request_query(data: void*, at: const char*, length: size_t): void $
        +on_request_version(data: void*, at: const char*, length: size_t): void $
        +on_request_header_done(data: void*, at: const char*, length: size_t): void $
        +on_request_http_field(data: void*, field: const char*, flen: size_t, value: const char*, vlen: size_t): void $
        
        +on_response_reason(data: void*, at: const char*, length: size_t): void $
        +on_response_status(data: void*, at: const char*, length: size_t): void $
        +on_response_chunk(data: void*, at: const char*, length: size_t): void $
        +on_response_version(data: void*, at: const char*, length: size_t): void $
        +on_response_header_done(data: void*, at: const char*, length: size_t): void $
        +on_response_last_chunk(data: void*, at: const char*, length: size_t): void $
        +on_response_http_field(data: void*, field: const char*, flen: size_t, value: const char*, vlen: size_t): void $
    }

    %% ================= HTTP 解析器 =================
    class HttpRequestParser {
        -m_parser: http_parser
        -m_data: HttpRequest::ptr
        -m_error: int

        +HttpRequestParser()
        +execute(data: char*, len: size_t): size_t
        +isFinished(): int
        +hasError(): int
        +getData(): HttpRequest::ptr const
        +setError(v: int): void
        +getContentLength(): uint64_t
        +getParser(): const http_parser& const
        +GetHttpRequestBufferSize(): uint64_t $
        +GetHttpRequestMaxBodySize(): uint64_t $
    }

    class HttpResponseParser {
        -m_parser: httpclient_parser
        -m_data: HttpResponse::ptr
        -m_error: int

        +HttpResponseParser()
        +execute(data: char*, len: size_t, chunk: bool): size_t
        +isFinished(): int
        +hasError(): int
        +getData(): HttpResponse::ptr const
        +setError(v: int): void
        +getContentLength(): uint64_t
        +getParser(): const httpclient_parser& const
        +GetHttpResponseBufferSize(): uint64_t $
        +GetHttpResponseMaxBodySize(): uint64_t $
    }

    HttpRequest "1" o-- "n" CaseInsensitiveLess : 使用其作为 Header 字典比较器
    HttpResponse "1" o-- "n" CaseInsensitiveLess : 使用其作为 Header 字典比较器
    HttpRequestParser "1" *-- "1" HttpRequest : 包含被解析的数据对象
    HttpResponseParser "1" *-- "1" HttpResponse : 包含被解析的数据对象
```

---

### 核心实现细节与底层原理深度剖析

#### 1. 宏驱动的枚举系统 (`HTTP_METHOD_MAP` / `HTTP_STATUS_MAP`)

* **设计痛点**：HTTP 有 30 多种方法和 60 多种状态码，手写 `switch-case` 进行字符串映射不仅极易出错，而且后期维护极其痛苦。

* **宏的艺术 (`#define XX`)**：

  * 在 `http.h` 中统一定义了一份宏列表。

  * **玩法 1：生成枚举**。`#define XX(num, name, string) name = num,`，展开后自动生成类似 `GET = 1, POST = 3` 的 `enum class HttpMethod`。

  * **玩法 2：生成解析逻辑**。在 `http.cc` 中：

    ```cpp
    #define XX(num, name, string) if(strcmp(#string, m.c_str()) == 0) return HttpMethod::name;
    ```

    这里极其巧妙地利用了预处理器操作符 `#`（字符串化），瞬间生成了 34 个 `if` 语句，零错误率实现了字符串到枚举的极速转换。

  * **玩法 3：生成数组映射**。通过 `const char* s_method_string[]` 数组索引，实现了从枚举到字符串 $O(1)$ 时间复杂度的反射。

#### 2. 大小写不敏感的字典树 (`CaseInsensitiveLess`)

* **网络规范要求**：RFC 规定，HTTP 的 Header 字段（如 `Content-Length` 和 `content-length`）**必须忽略大小写**。但是 C++ 标准库的 `std::map` 默认是大小写敏感的。
* **自定义比较器**：
  * 定义了 `CaseInsensitiveLess` 仿函数，重载 `operator()`。
  * 内部调用 C 标准库的高效函数 `strcasecmp` 进行比较。
  * 在 `HttpRequest` 中将 `MapType` 声明为 `std::map<std::string, std::string, CaseInsensitiveLess>`。这样无论业务层用什么大小写调用 `getHeader()` 或是解析器压入数据，底层都能准确命中，极其优雅。

#### 3. 泛型类型转换 (`getAs` / `checkGetAs`)

* HTTP 请求传来的参数全部是字符串（例如 `"1024"`），业务层经常需要将其转为 `int`, `float`, `uint64_t` 等。
* **实现细节**：借助 `boost::lexical_cast<T>` 强大的泛型能力，封装了异常安全的提取函数。当转换失败（如把字母转为数字）时，自动捕获 `catch (...)` 并返回调用方指定的默认值 `def`，彻底避免了因畸形网络包导致服务端崩溃的风险。

#### 4. Ragel 状态机解析引擎与 C 回调粘合

这是本模块最核心的性能引擎。

* **Ragel 简介**：Ragel 能够将自定义的正则语法（`.rl` 文件）编译成极其紧凑的纯 C 语言 goto/状态矩阵代码。它的运行速度极快，无需在运行时构建语法树。
* **底层解析原理 (`execute`)**：
  * 调用 Ragel 生成的 `http_parser_execute` (或 `httpclient_parser_execute`)。该函数内部是一个巨大的状态机矩阵。它逐个读取 `char* data` 中的字符。
  * 一旦状态机成功识别出一个 Token（比如识别出遇到了空格，前面的单词就是 `GET` Method），就会**立即触发注册的 C 语言回调函数**（如 `on_request_method`）。
* **回调与上下文绑定 (`static_cast`)**：
  * 因为 Ragel 生成的解析器是纯 C 代码，不支持面向对象。
  * **破局之法**：在 `HttpRequestParser` 构造时，将 `this` 指针赋给了 `m_parser.data`。当触发全局 C 回调（如 `on_request_method(void *data, ...)`）时，强制将其 `static_cast<HttpRequestParser*>(data)` 转换回来。这样就在纯 C 的回调流中拿到了 C++ 的类实例，进而愉快地调用 `parser->getData()->setMethod(m)` 修改对象属性。
* **零拷贝与指针游标 (`at`, `length`)**：
  * 注意回调函数的签名：它只传给了你字符串的起始地址 `at` 和长度 `length`。**它并没有把字符串拷贝出来给你**！这是极致性能的体现。
  * 在回调内部，只有在最终存入 `m_data` 字典时，才会构造 `std::string`。如果不需要保存（例如 `on_request_uri` 暂时留空），则发生零拷贝。

#### 5. 缓冲区处理与截断 (`memmove`)

网络包可能会发生 TCP 粘包（一次收到 1.5 个 HTTP 请求）。

* **截断机制 (`memmove`)**：
  * `execute` 跑完后，会返回它成功解析的**总字节数** `offset`。
  * `memmove(data, data + offset, (len - offset));`
  * 此时，`data` 中已解析完毕的 Headers 会被覆盖丢弃，剩下的只有 Body（如果存在）或者是下一个 HTTP 报文的头部。这种原地平移技术极其高效，彻底避免了重新 `malloc` 新缓冲区。

#### 6. 联动热更新配置系统 (`_RequestSizeIniter`)

* 为了防范诸如 **HTTP Header 恶意超长攻击 (Slowloris / 缓冲区溢出)**，我们在 `http_parser.cc` 中通过匿名命名空间定义了 `_RequestSizeIniter`。
* 它利用 Version 2 中的 `Config::Lookup` 创建了全局变量 `http.request.buffer_size` 等配置项，并直接**注册了 `addListener` 监听器**。
* 只要配置文件发生变动，静态缓存 `s_http_request_buffer_size` 就会被实时修改，整个过程无锁化，热更新即刻生效。

---

### 单元测试核心分析 (Tests)

#### `test_http.cc`

* **基础对象测试**：
  * 手动装配了一个 `HttpRequest` 和 `HttpResponse` 对象，设置了诸如 `Host`, `Content-Length` 和 Body 等属性。
  * 调用 `dump` 进行序列化，输出标准的 HTTP 报文。验证了底层换行符 `\r\n` 的正确拼接以及 `CaseInsensitiveLess` 字典树的行为。

#### `test_http_parser.cc`

* **状态机解析压测**：
  * 定义了一段包含完整 Request 行、Headers 和 Body 的合法网络字节流：`const char test_request_data[]`。
  * 通过 `parser.execute(&tmp[0], tmp.size());` 一口吞入整个字符串。
  * 测试结果将显示：`isFinished() == 1` (代表 Header 解析完美结束)，且返回了正确的 `offset` 偏移量。
  * 最精彩的是打印截断后的 `tmp`，会发现此时 Header 已经被完全抹去，只剩下了 `"1234567890"`（这是原本的 Body），完美验证了 `memmove` 的原地截断逻辑。同理验证了 Response 返回包的解析正确性。

---

##  Version 10: 服务端封装 (TcpServer)

### 版本差异与核心演进亮点

在以往的网络编程中，编写一个高性能的服务器需要处理海量的 `epoll_wait`、`accept`、线程池分发以及连接保活问题。业务逻辑往往被淹没在这些底层的状态机管理代码中。

**Version 10 的核心升级如下：**

1. **纯异步面向对象抽象**：彻底屏蔽了 `epoll` 和底层的协程切换细节。用户只需要继承 `TcpServer` 并重写 `handleClient` 虚函数，即可获得一个具备 10 万+ 并发处理能力的非阻塞 TCP 服务器。
2. **多核/多线程反应堆模型 (Multi-Reactor)**：`TcpServer` 内部剥离出了 `m_acceptWorker`（专门负责处理连接接入）和 `m_worker`（专门负责处理客户端数据读写）。这正是 Nginx / Netty 的核心架构思想。
3. **多地址并发监听**：不仅支持绑定单个 IP:Port，还支持通过 `std::vector<Address::ptr>` 一次性同时监听 IPv4、IPv6 甚至是本地的 UNIX 域套接字，极大地增强了部署的灵活性。
4. **安全与优雅停机 (Graceful Shutdown)**：通过 `shared_from_this` 结合 Lambda 闭包的生命周期延长技术，完美解决了服务器在关闭时因连接尚未断开而导致的悬空指针和 Core Dump 灾难。

---

### 模块全量核心类图 (UML)

以下是严格包含所有的结构、重载方法、默认参数及成员变量的详尽 UML 类图：

```mermaid
classDiagram
    %% ================= 基础组件回顾 =================
    class Noncopyable {
        <<class>>
        +Noncopyable() default
        +~Noncopyable() default
        -Noncopyable(const Noncopyable&) : deleted
        -operator=(const Noncopyable&) : deleted
    }

    %% ================= TCP 服务器骨架 =================
    class TcpServer {
        <<enable_shared_from_this, Noncopyable>>
        
        -m_socks: std::vector~Socket::ptr~
        -m_worker: IOManager*
        -m_acceptWorker: IOManager*
        -m_recvTimeout: uint64_t
        -m_name: std::string
        -m_isStop: bool

        +TcpServer(woker: IOManager*, accept_woker: IOManager*)
        +~TcpServer() virtual
        
        +bind(addr: Address::ptr): bool virtual
        +bind(addrs: const std::vector~Address::ptr~&, fails: std::vector~Address::ptr~&): bool virtual
        +start(): bool virtual
        +stop(): void virtual

        +getRecvTimeout(): uint64_t const
        +getName(): std::string const
        +setRecvTimeout(v: uint64_t): void
        +setName(v: const std::string&): void
        +isStop(): bool const

        #handleClient(client: Socket::ptr): void virtual
        #startAccept(sock: Socket::ptr): void virtual
    }

    TcpServer --|> Noncopyable : 继承以防拷贝
    TcpServer "1" *-- "n" Socket : 维护一组正在监听的Socket句柄
    TcpServer "1" o-- "2" IOManager : 依赖外部分配的Reactor协程池

    %% ================= 具体业务服务器示例 =================
    class EchoServer {
        -m_type: int
        +EchoServer(type: int)
        +handleClient(client: Socket::ptr): void override
    }

    EchoServer --|> TcpServer : 继承并重写核心业务逻辑
```

---

### 核心实现细节与底层原理深度剖析

#### 1. 双 Reactor 架构设计 (`m_acceptWorker` & `m_worker`)

* **架构痛点**：如果让处理 `accept` 的线程同时处理客户端的读写请求，一旦某个恶意的客户端发送超大报文或者业务处理极慢，会导致该线程无法及时处理新的 `accept` 请求，造成连接排队甚至被操作系统丢弃。
* **Sylar 的解决方案**：
  * 在构造函数 `TcpServer(woker, accept_woker)` 中，默认采用当前执行流所在的 `IOManager`。
  * 但在超高并发场景下，用户可以传入两个**不同的 `IOManager` 实例**。
  * **`m_acceptWorker`（Boss 线程组）**：专门负责运行 `startAccept` 协程，它的唯一任务就是在监听端口上“死循环”等待新连接。
  * **`m_worker`（Worker 线程组）**：一旦 `accept` 拿到新连接的 Socket，就将该 Socket 打包，抛入 `m_worker` 中。这样，后续几十万条连接的数据解析、编解码、业务处理全部由 Worker 线程组在协程中并发执行，彻底解放 Boss 线程。

#### 2. 多端口/多协议并发绑定 (`bind` 方法)

* `TcpServer` 的内部成员 `m_socks` 是一个 `std::vector<Socket::ptr>` 而不是单个 Socket。
* **实现逻辑**：
  * `bind` 重载方法接收一个 `vector<Address::ptr>`，可以在一个 `TcpServer` 实例上同时绑定 `0.0.0.0:80`（IPv4）、`[::]:80`（IPv6）甚至 `/tmp/sylar.sock`（UNIX Domain Socket）。
  * 函数会遍历地址列表，依次创建 Socket，调用 `sock->bind()` 和 `sock->listen()`。
  * **容错机制**：如果某个端口被占用导致绑定失败，函数会将失败的地址塞入 `fails` 数组并继续绑定下一个。最后如果 `fails` 不为空，不仅返回 `false`，还会立刻将之前已绑定的所有 Socket 执行 `clear()` 回收，保证了服务器启动状态的原子性（要么全部绑定成功，要么全部失败退出）。

#### 3. 永不停歇的监听协程 (`startAccept` 方法)

* 它是整个服务器连接接入的“发动机”。在 `start()` 方法中被投入到 `m_acceptWorker` 执行。
* **核心代码**：`while(!m_isStop) { Socket::ptr client = sock->accept(); ... }`
* **巧妙之处**：
  * 这里的 `sock->accept()` 并不是原生的阻塞 C 函数，而是 Version 7 中封装的 `Socket::accept`，其内部会触发 Version 6 的 `IO Hook`！
  * 当没有新连接时，这个 `accept` 会立刻返回 `EAGAIN`，然后底层将其注册到 epoll 中等待 `EPOLLIN` 事件，最后调用 `YieldToHold()` 将当前协程挂起。
  * **结论**：虽然代码看起来像是一个暴力的死循环，但在底层，它**完全不消耗任何 CPU**。这就是 C++ 协程黑魔法的魅力所在。
  * 当客户端连接成功后，获取到一个全新的客户端 Socket 实例，设置其默认读写超时时间（`m_recvTimeout`），然后通过 `std::bind` 组装好参数，扔给 `m_worker->schedule()` 分配新的协程去处理。

#### 4. 业务处理的核心抽象 (`handleClient` 虚函数)

* 这个方法被定义为 `virtual void`，是整个 `TcpServer` 留给开发者的扩展点。
* 在基类 `TcpServer` 中，它只做了一件事：打个日志。
* 开发者只需继承 `TcpServer`，重写这个方法，即可在这个函数里书写同步的读写逻辑。一旦执行到这个函数，代表底层已经完成了一次完美的非阻塞连接建立，并且当前代码是运行在一个**独立的轻量级协程**中的。

#### 5. 生命周期的极致保护 (`stop` 安全停机机制)

* 服务器退出时，最容易发生段错误的地方就是异步回调中访问了已经被析构的对象。

* **Sylar 的终极防护**：

  ```cpp
  auto self = shared_from_this();
  m_acceptWorker->schedule([this, self]() {
      for(auto& sock : m_socks) {
          sock->cancelAll();
          sock->close();
      }
      m_socks.clear();
  });
  ```

  * **为什么有了 `this` 还要捕获 `self`？**
    Lambda 闭包中捕获 `this` 只是拿到了当前对象的一个裸指针。当 `schedule` 把这个关闭任务异步扔给 `m_acceptWorker` 去执行时，由于是多线程环境，外层的 `TcpServer` 可能已经被析构。如果此时闭包里执行 `sock->cancelAll()` 就会触发 Core Dump。
  * 捕获 `self` (即 `shared_ptr`) 使得 Lambda 闭包强制持有了 `TcpServer` 实例的一个强引用计数。这确保了在整个停机逻辑执行完毕之前，`TcpServer` 对象**绝对不会在内存中被销毁**。安全、优雅、滴水不漏。

---

### 实际业务演练与单元测试 (Tests)

#### `test_tcp_server.cc`

* 这是一个极简的服务器启动测试。
* 定义了监听地址 `0.0.0.0:8080`，通过 `while(!tcp_server->bind(addrs, fails)) { sleep(2); }` 实现了端口被占用时的自动重试逻辑。
* 最终调用 `tcp_server->start()`，此时服务器协程在后台默默运转，主线程优雅退出或等待。

#### `echo_server.cc` (性能压测演示代码)

这是基于 `TcpServer` 构建的第一个真实业务服务器：**回显服务器 (Echo Server)**。

1. **继承与重写**：定义了 `EchoServer` 继承自 `TcpServer`，并在构造函数中接收 `-t` 或 `-b` 参数以决定输出文本流还是十六进制。
2. **重写 `handleClient`**：
   * 在这个独立协程中，首先通过 `sylar::ByteArray::ptr ba` 分配了一块底层链表内存池。
   * **零拷贝读**：进入 `while(true)` 死循环，调用 `ba->getWriteBuffers(iovs, 1024)` 获取底层的空闲物理碎片内存。然后调用封装好的 `client->recv`，使用 `iovec` 将客户端发来的数据**直接写入内存碎块，0 次用户态拷贝**。
   * **错误处理**：`recv` 返回 `0` 代表对端合法关闭（如客户端断开 WiFi 或关闭进程），返回 `< 0` 代表超时或其他异常。这两种情况都会 `break` 退出循环，协程自然死亡，资源被自动释放回收。
   * **流式处理**：读取完成后，将游标清零，将客户端发来的二进制流转换为 `toHexString()` 并输出到控制台。
3. **命令行交互**：利用 `argc` 和 `argv` 解析启动参数，使得这个微型服务器具备了极强的实用性和极高的并发吞吐能力。

---

## Version 11: 流式接口与 HTTP 服务器架构 (Stream & HttpSession/HttpServer/Servlet)

### 版本差异与核心演进亮点

在以往的网络编程中，我们总是直接操作 `Socket` 的 `send` 和 `recv`。但这会导致代码与具体的网络套接字强耦合，如果未来我们需要在数据流外层加上 TLS/SSL 加密，或者将数据输出到文件中，业务代码将面临灾难性的重构。

**Version 11 的核心升级如下：**

1. **统一流式抽象 (`Stream`)**：模仿 Java/C# 的 IO 流设计，抽象出顶层的 `Stream` 接口。无论是底层的 TCP Socket、加密的 SSL Socket，还是未来的 File Stream，只要实现了该接口，上层业务即可实现**无缝切换与统一读写**。
2. **HTTP 会话封装 (`HttpSession`)**：继承自 `SocketStream`，在普通的字节流之上，赋予了流“解析 HTTP 报文”的高级能力，彻底屏蔽了底层的 Ragel 状态机细节。
3. **HTTP 服务器引擎 (`HttpServer`)**：直接继承自 `TcpServer`，利用双 Reactor 模型处理海量并发连接，支持 Keep-Alive 长连接维持。
4. **Servlet 动态路由分发 (`ServletDispatch`)**：借鉴了 Java EE 的 Servlet 规范。支持**精准匹配** (`/api/login`) 和**通配符模糊匹配** (`/static/*`)，彻底解耦了网络接入层与实际业务处理逻辑。

---

### 模块全量核心类图 (UML)

#### 1. Stream 流式抽象模块类图

```mermaid
classDiagram
    class Stream {
        <<abstract>>
        +~Stream() virtual
        +read(buffer: void*, length: size_t): int *
        +read(ba: ByteArray::ptr, length: size_t): int *
        +readFixSize(buffer: void*, length: size_t): int virtual
        +readFixSize(ba: ByteArray::ptr, length: size_t): int virtual
        +write(buffer: const void*, length: size_t): int *
        +write(ba: ByteArray::ptr, length: size_t): int *
        +writeFixSize(buffer: const void*, length: size_t): int virtual
        +writeFixSize(ba: ByteArray::ptr, length: size_t): int virtual
        +close(): void *
    }

    class SocketStream {
        #m_socket: Socket::ptr
        #m_owner: bool

        +SocketStream(sock: Socket::ptr, owner: bool)
        +~SocketStream()
        +read(buffer: void*, length: size_t): int override
        +read(ba: ByteArray::ptr, length: size_t): int override
        +write(buffer: const void*, length: size_t): int override
        +write(ba: ByteArray::ptr, length: size_t): int override
        +close(): void override
        +getSocket(): Socket::ptr const
        +isConnected(): bool const
    }

    SocketStream --|> Stream : 实现抽象流接口
    SocketStream "1" o-- "1" Socket : 组合包装 Socket 句柄
```

#### 2. HTTP Server 业务处理模块类图

```mermaid
classDiagram
    %% ================= 会话与服务器架构 =================
    class HttpSession {
        +HttpSession(sock: Socket::ptr, owner: bool)
        +recvRequest(): HttpRequest::ptr
        +sendResponse(rsp: HttpResponse::ptr): int
    }

    class HttpServer {
        -m_isKeepalive: bool
        -m_dispatch: ServletDispatch::ptr
        
        +HttpServer(keepalive: bool, worker: IOManager*, accept_worker: IOManager*)
        +getServletDispatch(): ServletDispatch::ptr const
        +setServletDispatch(v: ServletDispatch::ptr): void
        #handleClient(client: Socket::ptr): void override
    }

    HttpSession --|> SocketStream : 继承底层流能力
    HttpServer --|> TcpServer : 继承异步多路复用网络骨架
    HttpServer "1" *-- "1" ServletDispatch : 拥有全局路由分发器

    %% ================= Servlet 路由处理架构 =================
    class Servlet {
        <<abstract>>
        #m_name: std::string
        +Servlet(name: const std::string&)
        +~Servlet() virtual
        +handle(request: HttpRequest::ptr, response: HttpResponse::ptr, session: HttpSession::ptr): int32_t *
        +getName(): const std::string& const
    }

    class FunctionServlet {
        <<typedef callback = std::function<...>>>
        -m_cb: callback
        +FunctionServlet(cb: callback)
        +handle(request: HttpRequest::ptr, response: HttpResponse::ptr, session: HttpSession::ptr): int32_t override
    }

    class ServletDispatch {
        -m_mutex: RWMutexType
        -m_datas: std::unordered_map~std::string, Servlet::ptr~
        -m_globs: std::vector~std::pair<std::string, Servlet::ptr>~
        -m_default: Servlet::ptr

        +ServletDispatch()
        +handle(request: HttpRequest::ptr, response: HttpResponse::ptr, session: HttpSession::ptr): int32_t override
        +addServlet(uri: const std::string&, slt: Servlet::ptr): void
        +addServlet(uri: const std::string&, cb: FunctionServlet::callback): void
        +addGlobServlet(uri: const std::string&, slt: Servlet::ptr): void
        +addGlobServlet(uri: const std::string&, cb: FunctionServlet::callback): void
        +delServlet(uri: const std::string&): void
        +delGlobServlet(uri: const std::string&): void
        +getDefault(): Servlet::ptr const
        +setDefault(v: Servlet::ptr): void
        +getServlet(uri: const std::string&): Servlet::ptr
        +getGlobServlet(uri: const std::string&): Servlet::ptr
        +getMatchedServlet(uri: const std::string&): Servlet::ptr
    }

    class NotFoundServlet {
        +NotFoundServlet()
        +handle(request: HttpRequest::ptr, response: HttpResponse::ptr, session: HttpSession::ptr): int32_t override
    }

    ServletDispatch --|> Servlet : 自身也是一个Servlet(组合模式)
    FunctionServlet --|> Servlet : 实现基于Lambda的快速业务回调
    NotFoundServlet --|> Servlet : 内置兜底404处理逻辑
    ServletDispatch "1" *-- "n" Servlet : 路由表管理多个业务实例
```

---

### 核心实现细节与底层原理深度剖析

#### 1. Stream 流式抽象模块深度剖析

流式抽象的核心目的是为了**解决由于 TCP 底层切包带来的“数据读取不完整”问题**。

* **纯虚函数约束 (`read` / `write`)**：
  * 这四个方法是纯虚函数，子类必须实现。它约定了最基础的内存读写能力。不管是原生的 `char*` 数组，还是我们高度封装的高性能零拷贝 `ByteArray` 链表池，都支持作为数据载体。
* **精准大小读写封装 (`readFixSize` / `writeFixSize`)**：
  * **应用痛点**：在网络编程中，调用 `read(buf, 1000)` 有时只会返回 200 个字节（因为网卡缓冲区就这么多），剩余的 800 字节还在路上。如果业务直接拿去解析必然出错。
  * **实现细节**：这四个方法在基类中给出了**具体实现**。内部采用了一个精妙的 `while(left > 0)` 循环。它会不断调用子类实现的底层 `read/write`，动态累加 `offset` 偏移量并递减剩余大小 `left`。只有当真正读满/写满指定的 `length`，或者底层连接断开/异常时，循环才会退出。这为后续解析定长的 HTTP Body 提供了绝对的安全保障。

* **SocketStream 的实体包装**：
  * **生命周期托管 (`m_owner`)**：构造时支持传入 `owner` 标志位。如果为 `true`，则 `SocketStream` 对象析构时，会自动调用底层 `Socket` 的 `close()`。这是 RAII 思想的延伸。
  * **读写实现**：重写了基类的虚函数。对于 `char*` 直接调用 `Socket::recv/send`；对于 `ByteArray` 载体，极致发挥了 Version 8 中 `iovec` 零拷贝的优势。调用 `ba->getWriteBuffers` 直接索要物理碎片内存，交给底层的聚集/分散 IO 系统调用，并在成功后通过 `ba->setPosition` 智能推移内存游标。

#### 2. HttpSession：协议解析与网络收发的纽带

* **`recvRequest()` 的复杂状态机流转**：
  * 该方法负责从套接字中抽取出完整的 HTTP 报文。
  * 每次循环从 `SocketStream` 读出一段数据，交给 `HttpRequestParser` 去执行 Ragel 状态机解析。
  * **截断与偏移 (`offset`) 管理**：如果状态机报告解析成功了 `nparse` 字节，通过 `offset = len - nparse` 计算出是否有残余数据。这些残余数据往往是粘包带进来的 HTTP Body 或下一个报文的 Header，绝对不能丢弃。
  * **Body 读取**：Header 解析完毕后，通过 `parser->getContentLength()` 获取精准的 Body 大小。如果刚刚由于粘包多读了 `offset` 字节的 Body，先把它剥离出来；剩下的缺失字节，**强制调用流基类的 `readFixSize` 确保读满**。最终生成一个结构完美的 `HttpRequest` 对象。
* **`sendResponse(rsp)`**：
  * 极其轻量优雅。重载了 `std::ostream` 的 `<<` 运算符后，直接把面向对象的 `HttpResponse` 冲刷到底层的字符串流，然后调用 `writeFixSize` 保证全部发送。

#### 3. HttpServer：双 Reactor 与长连接支持

* **架构融合**：继承自上一版的超级骨架 `TcpServer`。
* **长连接逻辑 (`handleClient`)**：
  * 这是重写的最核心的业务逻辑，在这个被分配到 Worker 线程的独立协程中运行。
  * 用 `do-while(m_isKeepalive)` 包裹了整个会话。只要客户端不主动掐断，且没有发来 `Connection: close`，这个协程就不会结束，会不断地从 `HttpSession` 中剥离出下一个请求。
  * 获取请求后，将生杀大权移交给核心的路由系统：`m_dispatch->handle(req, rsp, session)`。
  * 路由执行完毕后，调用 `sendResponse` 将响应打回客户端。

#### 4. Servlet：优雅的动态路由分发系统

参照 Java 经典的 Tomcat/Spring MVC 架构，我们构建了一套纯 C++ 的高性能 Servlet 容器。

* **`Servlet` 基类**：规定了任何业务处理器都必须实现 `handle` 虚函数。
* **`FunctionServlet`**：
  * **设计哲学**：在现代 C++ (C++11及以后) 编程中，人们更喜欢用轻量的 Lambda 闭包而非沉重的继承类来编写简单逻辑。
  * 此类包装了 `std::function`。它允许开发者直接向路由中塞入一个 Lambda 表达式。在 `handle` 被调用时，透传给内部绑定的闭包函数。
* **`NotFoundServlet` (兜底策略)**：
  * 默认实例化。当黑客扫描或者路径不存在时，自动返回 `404 Not Found` 状态码和一段默认的 HTML 代码。
* **`ServletDispatch` 核心路由匹配器**：
  * **并发安全**：由于服务器运行时可能有别的线程在动态添加/删除路由，内部维护了 `RWMutex` 读写锁。
  * **精准匹配 (`m_datas`)**：采用 `std::unordered_map`（哈希表）。当请求路径如 `/login` 到来时，实现 $O(1)$ 的极限检索速度。
  * **模糊匹配 (`m_globs`)**：采用 `std::vector<pair>`。由于模糊匹配可能存在多个规则（例如 `/static/*` 和 `/static/js/*`），内部使用 C 语言经典的 `fnmatch` 库函数进行通配符模式匹配，遍历查找最优解。
  * **匹配优先级 (`getMatchedServlet`)**：
    首先尝试在哈希表中进行**精准查找**；如果失败，再遍历数组进行**通配符模糊匹配**；如果均失败，返回默认的 `NotFoundServlet`。整个匹配逻辑严丝合缝。

---

### 单元测试核心分析 (Tests)

#### `test_http_server.cc`

* **全流程联调验证**：
  1. 初始化带有 2 个线程的协程调度器 `IOManager`。
  2. 创建 `HttpServer` 实例并绑定本机所有的 `8020` 端口。
  3. **注册精准路由**：`sd->addServlet("/yinghuo/xx", ...)`。利用 Lambda 函数优雅地实现了业务逻辑。当客户端访问此路径时，服务器会将完整的请求报文（转换成 string）作为 Response Body 返回给客户端。
  4. **注册模糊路由**：`sd->addGlobServlet("/yinghuo/*", ...)`。无论客户端访问 `/yinghuo/123` 还是 `/yinghuo/test/abc`，只要前缀匹配，都会被此 Lambda 拦截。并在返回体前面加上 `Glob:\r\n` 作为验证标记。
  5. 启动服务器协程 `server->start()`，此时你可以通过 `curl` 或浏览器任意发起高并发长连接测试，底层将在全异步非阻塞状态下完美承载。

---

## Version 12: HTTP 客户端、连接池 (HTTPConnection)

### 版本差异与核心演进亮点

在以往的 C++ 框架中，发起 HTTP 请求通常依赖笨重的 `libcurl`。而在 Sylar 框架中，我们完全基于自己的 `SocketStream` 和 `Hook` 机制，实现了一个纯血的、百分百无阻塞的协程级别 HTTP Client。

**Version 12 的核心升级如下：**

1. **URI 极速解析引擎 (`uri.rl`)**：再次请出大杀器 **Ragel**。根据 RFC 3986 规范编写状态机，一键极速拆解 `http://user:pass@host:port/path?query#fragment`，甚至打破原生 RFC 限制，加入了对中文 URL 的完美解析支持。
2. **HTTP 客户端连接对象 (`HttpConnection`)**：对标服务端的 `HttpSession`，它负责客户端视角的装配：发送 `HttpRequest`，接收并解析 `HttpResponse`。并且支持了对 `Chunked` (分块传输) 编码的完美解析。
3. **全局防并发雪崩连接池 (`HttpConnectionPool`)**：
   * 建立 HTTP 连接（TCP 三次握手）极其耗时。高并发调用第三方接口时，必须使用连接池。
   * 实现了 **最大连接数控制**、**最大存活时间 (Keep-Alive) 淘汰** 以及 **最大请求数淘汰** 的多维度连接复用与过期回收策略，保障高并发下的网络资源绝对安全。
4. **一键调用极简接口**：封装了大量诸如 `DoGet`、`DoPost`、`DoRequest` 的静态方法。一行代码即可发起带超时控制的异步网络请求，对业务层屏蔽所有细节。

---

### 模块全量核心类图 (UML)

#### 1. URI 解析抽象类图

```mermaid
classDiagram
    class Uri {
        -m_scheme: std::string
        -m_userinfo: std::string
        -m_host: std::string
        -m_path: std::string
        -m_query: std::string
        -m_fragment: std::string
        -m_port: int32_t

        +Create(uristr: const std::string&): Uri::ptr $
        +Uri()
        +getScheme(): const std::string& const
        +getUserinfo(): const std::string& const
        +getHost(): const std::string& const
        +getPath(): const std::string& const
        +getQuery(): const std::string& const
        +getFragment(): const std::string& const
        +getPort(): int32_t const
        +setScheme(v: const std::string&): void
        +setUserinfo(v: const std::string&): void
        +setHost(v: const std::string&): void
        +setPath(v: const std::string&): void
        +setQuery(v: const std::string&): void
        +setFragment(v: const std::string&): void
        +setPort(v: int32_t): void
        +dump(os: std::ostream&): std::ostream& const
        +toString(): std::string const
        +createAddress(): Address::ptr const
        -isDefaultPort(): bool const
    }
    
    %% Ragel内部生成的C状态机代码不会体现在UML中，但在实际架构中紧密支撑Uri::Create
```

#### 2. HTTP 客户端与连接池类图

```mermaid
classDiagram
    %% ================= 提取出的错误码枚举 =================
    class Error {
        <<enumeration>>
        OK
        INVALID_URL
        INVALID_HOST
        CONNECT_FAIL
        SEND_CLOSE_BY_PEER
        SEND_SOCKET_ERROR
        TIMEOUT
        CREATE_SOCKET_ERROR
        POOL_GET_CONNECTION
        POOL_INVALID_CONNECTION
    }

    %% ================= HTTP 响应结果包装 =================
    class HttpResult {
        <<struct>>
        +result: int
        +response: HttpResponse::ptr
        +error: std::string

        +HttpResult(_result: int, _response: HttpResponse::ptr, _error: const std::string&)
        +toString(): std::string const
    }

    HttpResult ..> Error : 依赖错误码枚举

    %% ================= HTTP 客户端单连接 =================
    class HttpConnection {
        <<SocketStream>>
        -m_createTime: uint64_t
        -m_request: uint64_t

        +HttpConnection(sock: Socket::ptr, owner: bool)
        +~HttpConnection()
        +recvResponse(): HttpResponse::ptr
        +sendRequest(req: HttpRequest::ptr): int

        +DoGet(url: const std::string&, timeout_ms: uint64_t, headers: const std::map~std::string, std::string~&, body: const std::string&): HttpResult::ptr $
        +DoGet(uri: Uri::ptr, timeout_ms: uint64_t, headers: const std::map~std::string, std::string~&, body: const std::string&): HttpResult::ptr $
        +DoPost(url: const std::string&, timeout_ms: uint64_t, headers: const std::map~std::string, std::string~&, body: const std::string&): HttpResult::ptr $
        +DoPost(uri: Uri::ptr, timeout_ms: uint64_t, headers: const std::map~std::string, std::string~&, body: const std::string&): HttpResult::ptr $
        +DoRequest(method: HttpMethod, url: const std::string&, timeout_ms: uint64_t, headers: const std::map~std::string, std::string~&, body: const std::string&): HttpResult::ptr $
        +DoRequest(method: HttpMethod, uri: Uri::ptr, timeout_ms: uint64_t, headers: const std::map~std::string, std::string~&, body: const std::string&): HttpResult::ptr $
        +DoRequest(req: HttpRequest::ptr, uri: Uri::ptr, timeout_ms: uint64_t): HttpResult::ptr $
    }

    %% ================= HTTP 并发连接池 =================
    class HttpConnectionPool {
        -m_host: std::string
        -m_vhost: std::string
        -m_port: uint32_t
        -m_maxSize: uint32_t
        -m_maxAliveTime: uint32_t
        -m_maxRequest: uint32_t
        -m_mutex: MutexType
        -m_conns: std::list~HttpConnection*~
        -m_total: std::atomic~int32_t~

        +HttpConnectionPool(host: const std::string&, vhost: const std::string&, port: uint32_t, max_size: uint32_t, max_alive_time: uint32_t, max_request: uint32_t)
        +getConnection(): HttpConnection::ptr
        +doGet(url: const std::string&, timeout_ms: uint64_t, headers: const std::map~std::string, std::string~&, body: const std::string&): HttpResult::ptr
        +doGet(uri: Uri::ptr, timeout_ms: uint64_t, headers: const std::map~std::string, std::string~&, body: const std::string&): HttpResult::ptr
        +doPost(url: const std::string&, timeout_ms: uint64_t, headers: const std::map~std::string, std::string~&, body: const std::string&): HttpResult::ptr
        +doPost(uri: Uri::ptr, timeout_ms: uint64_t, headers: const std::map~std::string, std::string~&, body: const std::string&): HttpResult::ptr
        +doRequest(method: HttpMethod, url: const std::string&, timeout_ms: uint64_t, headers: const std::map~std::string, std::string~&, body: const std::string&): HttpResult::ptr
        +doRequest(method: HttpMethod, uri: Uri::ptr, timeout_ms: uint64_t, headers: const std::map~std::string, std::string~&, body: const std::string&): HttpResult::ptr
        +doRequest(req: HttpRequest::ptr, timeout_ms: uint64_t): HttpResult::ptr

        -ReleasePtr(ptr: HttpConnection*, pool: HttpConnectionPool*): void $
    }

    HttpConnection --|> SocketStream : 继承底层流读写能力
    HttpConnectionPool "1" *-- "n" HttpConnection : 维护可用空闲连接列表
    HttpConnectionPool ..> HttpResult : 返回统一结果结构
```

---

### 核心实现细节与底层原理深度剖析

#### 1. URI 极速状态机解析 (`uri.rl`)

我们通常在业务里见到的 URL，如 `http://admin:123456@www.sylar.top:8080/api/login?name=sylar#fragment`，包含了极多成分。

* **痛点**：传统使用正则引擎解析，每次请求都会进行大量的回溯和动态内存分配。
* **Ragel 状态机实现 (`Uri::Create`)**：
  * 在 `uri.rl` 中，严格遵循 RFC 3986 标准，定义了 `scheme`, `authority`, `path`, `query` 等 token。
  * **黑科技注入：支持中文解析**：在标准的 RFC 中，Path 和 Query 通常是不允许出现纯中文的（需要 urlencode）。但为了兼容国内宽松的业务环境，我们修改了 Ragel 状态机规则：
    `pchar = ( (any -- ascii ) | unreserved | pct_encoded | sub_delims | ":" | "@" ) ;`
    这里的 `(any -- ascii)` 意味着将所有非 ASCII 字符（如中文的 UTF-8 编码多字节）直接视为合法字符一并提取，省去了业务层先解码的麻烦。
  * 当状态机解析出一个 token（如解析完了 host），会立刻执行 `action` 块内的 C++ 代码 `uri->setHost(...)`。一次遍历 $O(N)$ 搞定所有解析，性能极致。
* **端口自适应 (`isDefaultPort`)**：如果没有显式写明端口，解析器会根据 Scheme（http 返回 80，https 返回 443）智能推导默认端口，并配合 `createAddress()` 方法直接发起 DNS 请求，返回 `Address::ptr`，供 `Socket` 模块直接使用。

#### 2. HTTP Client 的封包与解包 (`HttpConnection`)

不同于服务端，客户端的任务是发送 Request，并接受 Response。

* **发包 (`sendRequest`)**：
  直接复用了基类 `SocketStream` 的 `writeFixSize`，借助之前实现的 `ostream::operator<<` 将组装好的 `HttpRequest` 序列化发往网卡缓冲区。
* **解包 (`recvResponse`) 核心难题：Chunked 分块传输**：
  服务端在发送大型文件或动态生成的内容时，由于无法预知 `Content-Length`，会使用 `Transfer-Encoding: chunked` 协议。
  * **难点**：数据被切成了无数个 `长度 \r\n 数据块 \r\n` 的碎片，必须一边读一边解。
  * **Sylar 解决方案**：
    在 `recvResponse` 方法中，进入一个双层 `do-while` 循环。
    * 外层循环不断调用 `read` 读取流数据，送入 Ragel `HttpResponseParser`。一旦 Header 解析完成即跳出。
    * 判断 `client_parser.chunked` 标志位。如果是 Chunked 模式，进入针对性解析。
    * **核心截断还原逻辑**：再次喂数据给 parser 解析 chunk 头部。`client_parser.content_len` 会解析出当前这个碎块的真实长度。利用 `body.append` 和 `memmove` 将这个碎块的数据“榨取”出来并拼接到最终的 `string body` 中。循环往复直到遇到 `0 \r\n\r\n`（表示 chunk 结束）。
    * 完美实现了将碎乱的流重新还原成一块完整的内存，对业务层完全透明。

#### 3. 短连接与全功能一键接口 (`DoGet` / `DoRequest`)

* 框架封装了一系列静态方法。例如：`HttpConnection::DoGet("http://www.baidu.com/xx", 3000)`。
* 内部实现了完整的闭环：
  1. `Uri::Create` 解析域名。
  2. `uri->createAddress` 触发内部的 DNS 域名寻址。
  3. `Socket::CreateTCP` 分配 Socket。
  4. `sock->connect` 发起 TCP 握手。由于底层的 `Hook` 魔法，这个 `connect` 会自动**非阻塞挂起当前协程**，不会阻塞业务线程。
  5. 握手成功后，序列化并发送报文，随后 `recvResponse`，同样在遇到 `EAGAIN` 时非阻塞挂起。
  6. 成功拿到 Response 后封装进 `HttpResult` 结构体返回。
* **完美解决了 C++ 发起 HTTP 请求代码冗长、且容易阻塞线程导致服务挂死的问题**。

#### 4. 高级进阶：HTTP 连接池的智能管理 (`HttpConnectionPool`)

TCP 握手极其耗时（1~1.5 RTT），在高并发的微服务互调场景中，必须复用已有连接。

* **连接池结构**：维护了一个 `m_conns` 的 `list<HttpConnection*>`。这里存的都是当前**空闲且存活**的 TCP 连接。
* **智能分配 (`getConnection`)**：
  * 当业务层请求连接时，加读写锁，从 `m_conns` 头部取出一个。
  * **双重过期检查**：不仅检查 `conn->isConnected()`（是否被对端断开），还检查 `conn->m_createTime + m_maxAliveTime > now_ms`（是否超过了配置的最大保活时间）。如果不符合，直接 `delete` 回收，并从池中拿下一个。
  * 若池子空了（或全被借走），则现场新创建一个 `HttpConnection` 并触发非阻塞的 DNS 和 TCP 握手。
* **生命周期劫持：智能指针的神奇应用**：
  * **痛点**：业务代码从连接池借走一个连接，用完后怎么保证它能乖乖还回来，而不是直接被析构关掉 Socket？
  * **解法**：`getConnection` 方法返回的不是裸指针，而是 `HttpConnection::ptr`。
  * 在封装时，利用了 `std::shared_ptr` 的**自定义删除器 (Custom Deleter)** 特性：
    `HttpConnection::ptr(ptr, std::bind(&HttpConnectionPool::ReleasePtr, std::placeholders::_1, this))`
  * 当业务代码的局部变量（智能指针）作用域结束准备销毁时，并不会触发真正的 `delete`，而是触发了静态方法 `ReleasePtr`。
* **智能归还与淘汰算法 (`ReleasePtr`)**：
  * 检查归还的连接：如果对端已经主动断开 (`!isConnected`)，或者存活时间超过了 `m_maxAliveTime`，或者这个连接**已经复用处理过了 `m_maxRequest` 次请求**，则触发真正的 `delete ptr` 进行物理销毁。
  * 如果各项指标都很健康，加锁将其推回 `m_conns` 链表尾部，供下一次协程借用。

---

### 单元测试核心分析 (Tests)

#### `test_uri.cc`

* 测试了包含特殊字符、中文、账号密码等各种变态格式的 URI。
* 例如 `"http://admin@www.yinghuo.website/test/中文/uri?id=100&name=sylar&vv=中文#frg中文"`。
* 控制台输出展示了 Ragel 状态机不仅能毫秒级提取出 `Host`, `Path`, `Query`, `Fragment`，还利用 C++ 接口成功触发了 DNS 解析，打印出了对应的公网 IPv4。

#### `test_http_connection.cc`

* **直连模式测试 (`run`)**：
  * 手动建立 `HttpConnection` 发起一个 HTTP 报文，拉取第三方 API (`httpspot.dev`) 的数据。
  * 通过一键调用静态方法 `sylar::http::HttpConnection::DoGet`，只需传一个 URL 字符串和超时时间 300 毫秒，底层协程就在极短时间内拿回了数据，并存入了 `rsp.dat` 物理文件中。验证了静态工具类极致的易用性。
* **连接池高并发测试 (`test_pool`)**：
  * 初始化了一个连接池（域名：`httpspot.dev`，最大保持 10 个连接，单连接最长保活 30 秒，单连接最多复用 5 次）。
  * 创建了一个间隔 1 秒的**后台循环定时器**，在这个定时器的回调函数中，不断地从 `pool` 中调用 `doGet`。
  * 在程序运行期间，你会发现虽然每秒都在发请求，但底层并没有一直报 `connect` 的系统调用，而是默默复用了内存中的 TCP 句柄，直到复用满 5 次才断开重连。这完美验证了智能指针劫持与连接池淘汰算法的协同生效。

---




这份文档为您极其详尽地梳理了 **Version 13 (守护进程 Daemon 与进程级容灾模块)** 的全部细节。

在高性能的生产环境服务器中，仅仅依靠协程捕获异常（`try-catch`）是不够的。如果程序发生底层的段错误（Segmentation Fault / Core Dump）、内存溢出（OOM 被内核 Kill）或者手动意外退出，整个服务器进程就会彻底死亡。
为了保证服务 **7x24 小时高可用**，我们必须引入 Linux 系统编程中经典的**守护进程（Daemon）**与**父子进程监控（Supervisor）机制**。

按照您的最高标准要求，我为您绘制了**完全独立、坚决不遗漏任何底层字段、函数签名甚至局部静态函数**的 UML 类图，并深度剖析了 `daemon` 系统调用、`fork` 机制以及防 Fork 炸弹的实现原理。

您可以直接将以下内容追加到您的 `Readme.md` 的末尾。

---

## 守护进程 (Daemon) 与进程级容灾保障 (Version 13)

### 版本差异与核心演进亮点

在之前的版本中，我们的服务器都是在前台运行的（Foreground）。一旦关闭 SSH 终端窗口，或者按下 `Ctrl+C`，服务就会立刻停止。

**Version 13 的核心升级如下：**
1. **脱离终端束缚 (Daemonize)**：通过 Linux 系统调用，将进程转化为真正的后台守护进程。使其生命周期与当前登录的 Shell 终端彻底剥离，实现静默运行。
2. **Supervisor 父子进程监控模型**：引入了一主（Monitor 父进程）一从（Worker 子进程）的容灾架构。父进程不处理任何业务，只负责“死等”并监控子进程的状态。
3. **毫秒级崩溃拉起**：当子进程因为任何异常（如代码 Bug 导致 Core Dump）死亡时，父进程能瞬间感知，并在短暂休眠后重新 `fork` 出一个全新的业务子进程，实现服务的自动“满血复活”。
4. **进程运行状态追踪**：内置 `ProcessInfo` 单例，精准记录父子进程的 PID、启动时间以及历史重启次数，为运维监控和报警提供第一手数据。

---

### 模块全量核心类图 (UML)

以下是严格包含所有结构体、全局函数、内部静态函数、单例模式及依赖关系的详尽 UML 类图：

```mermaid
classDiagram
    %% ================= 进程信息数据结构 =================
    class ProcessInfo {
        <<struct>>
        +parent_id: pid_t
        +parent_start_time: uint64_t
        +main_id: pid_t
        +main_start_time: uint64_t
        +restart_count: uint32_t

        +toString(): std::string const
    }

    class ProcessInfoMgr {
        <<Singleton>>
        +GetInstance(): ProcessInfo* $
    }

    ProcessInfoMgr ..|> ProcessInfo : 实例化全局单例

    %% ================= 守护进程核心函数集 =================
    class sylar_daemon {
        <<namespace>>
        
        %% 暴露给外部的核心 API
        +start_daemon(argc: int, argv: char**, main_cb: std::function~int(int, char**)~, is_daemon: bool): int $
        
        %% 内部隐藏的静态功能函数 (定义在 .cc 中)
        -real_start(argc: int, argv: char**, main_cb: std::function~int(int, char**)~): int $
        -real_daemon(argc: int, argv: char**, main_cb: std::function~int(int, char**)~): int $
    }

    sylar_daemon ..> ProcessInfoMgr : 记录并更新进程 PID 与启动状态
    sylar_daemon ..> sylar_config : 读取重启间隔配置
```

---

### 核心算法与底层原理深度剖析

#### 1. 进程状态大管家 (`ProcessInfo` 与 `ProcessInfoMgr`)
* **设计初衷**：在父子进程模型中，PID 随时可能因为崩溃重启而发生变化。业务层（例如 HTTP 的状态上报 API）需要一种手段随时获取当前真实的进程信息。
* **实现细节**：
  * 将 `parent_id` (监视器进程)、`main_id` (实际干活的业务进程)、它们的启动时间 (`start_time`) 以及至关重要的 `restart_count` (崩溃重启次数) 封装在 `ProcessInfo` 结构体中。
  * 借助之前写好的 `Singleton` 模板，将其声明为全局唯一的 `ProcessInfoMgr`。
  * `toString()` 借助 `Time2Str` 格式化时间，以极度可读的格式输出，方便随时记录到 Log 文件中。

#### 2. 程序入口分发器 (`start_daemon`)
* 这是暴露给 `main` 函数的唯一接口。
* **参数设计**：巧妙地接收了一个 `std::function<int(int argc, char** argv)> main_cb`。把原本应该写在 `main` 里的所有初始化和协程调度的业务代码，打包成一个回调函数传进来。
* **路由逻辑**：通过判断 `is_daemon` 标志位。如果为 `false`（例如开发环境调试），直接调用 `real_start` 走前台同步运行；如果为 `true`（生产环境部署），则切入核心的 `real_daemon` 逻辑。

#### 3. Linux 守护进程 (`daemon(1, 0)`)
在 `real_daemon` 函数的第一行，调用了 Linux 底层 API `daemon(nochdir, noclose)`。
* **参数 `nochdir = 1`**：告诉内核**不要**将进程的当前工作目录切换到根目录 `/`。
  * **深度剖析**：如果切换到 `/`，我们通过相对路径读取的配置文件（如 `../bin/conf/`）和生成的 Core Dump 文件将会直接失效或写入系统根目录导致权限不足报错。设为 `1` 完美解决了路径依赖问题。
* **参数 `noclose = 0`**：告诉内核**重定向标准输入/输出/错误到 `/dev/null`**。
  * **深度剖析**：守护进程必须脱离终端控制。如果终端关闭，进程试图往 `std::cout` 写日志，会收到 `SIGPIPE` 导致崩溃。设为 `0` 后，所有未被日志系统接管的 `printf` 都会被安全地丢入 Linux 的黑洞 `/dev/null` 中。

#### 4. 父子进程监控体系 (`fork` 与 `waitpid`)
这是容灾系统的绝对核心，代码虽然简短，但每一行都是系统编程的精华：
* **第一步：主循环与 Fork**：进入 `while(true)` 死循环，紧接着调用 `pid_t pid = fork()`。此时进程一分为二。
* **第二步：子进程执行 (`pid == 0`)**：
  * 这是真正干活的业务进程。它首先更新 `ProcessInfoMgr`，记录自己是第几次被拉起的，记录启动时间。
  * 然后调用 `real_start(argc, argv, main_cb)`，**一头扎进协程调度器和网络 IO 的死循环中**。只要业务不出大问题，这行代码永远不会返回。
* **第三步：父进程守望 (`pid > 0`)**：
  * 这是监视器（Supervisor）。它的业务极简，只有一行阻塞调用：`waitpid(pid, &status, 0)`。
  * **深度剖析**：`waitpid` 会让父进程在内核态陷入深度睡眠，**消耗的 CPU 资源严格等于 0**。直到它监控的那个子进程死亡（无论是正常退出还是被信号杀死），父进程才会被内核立刻唤醒。
* **第四步：死亡判定与防 Fork 炸弹 (`status` 检查)**：
  * 被唤醒后，父进程检查 `status`。如果 `status == 0`，说明子进程是主动安全退出（比如收到了停机命令），父进程直接 `break` 退出死循环，整个服务器和平关机。
  * 如果 `status != 0`，说明子进程**意外惨死**（段错误、被 OOM Kill 等）。
  * 父进程将 `restart_count` 加 1。
  * 调用 `sleep(g_daemon_restart_interval->getValue())` (默认 5 秒)。如果代码有致命 Bug（比如一启动就崩溃），如果没有这个 `sleep`，父进程会一秒钟疯狂拉起几万个子进程，瞬间耗尽系统资源导致死机。加入休眠后，实现了平滑的安全拉起。

---

### 单元测试核心分析 (Tests)

#### `test_daemon.cc`
这个测试用例极其巧妙地模拟了生产环境中最恶劣的**进程崩溃场景**。

* **业务逻辑 (`server_main`)**：
  * 启动后，首先打印当前的 `ProcessInfo` 信息。
  * 初始化了一个 `IOManager` 协程调度器。
  * **模拟崩溃机制**：挂载了一个周期为 1000 毫秒（1 秒）的循环定时器。定时器内部维护了一个静态计数器 `static int count = 0`。
  * 每一秒 `count` 加 1。当 `++count > 10`（即程序平稳运行了 10 秒）时，**悍然调用 `exit(1)` 强制异常退出整个子进程！**
* **启动与验证逻辑 (`main`)**：
  * `main` 函数将 `argc` 和 `argv` 传入。判断规则是：如果不带任何参数 (`argc == 1`)，则前台运行；如果带任意参数（如 `./test_daemon -d`，此时 `argc != 1`），则触发守护进程模式。
* **预期运行效果**：
  1. 编译后执行 `./test_daemon -d`。终端会立刻返回，程序转入后台静默运行。
  2. 打开你的日志文件（前提是你在 `log.yml` 里配置了 FileAppender）。
  3. 你会看到程序打印了启动日志，`restart_count = 0`。
  4. 随后每秒打印一次 `onTimer`。
  5. 10 秒后，日志戛然而止（子进程执行了 `exit(1)` 自杀了）。
  6. 大约等待 5 秒钟（配置的拉起间隔），日志文件中会再次冒出全新的启动日志！但是这次打印的 `ProcessInfo` 中，`parent_id` 没变，`main_id` 变为了全新的 PID，且 **`restart_count` 变成了 `1`**！
  7. 完美证明了我们的 `Daemon` 模块具备强悍的毫秒级故障感知和进程满血复活能力！