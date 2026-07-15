# TinyDDT — 协程微服务架构的多人实时弹道射击游戏

## 项目概述
TinyDDT（仿弹弹堂）是一款基于 C/S 架构的实时多人回合制弹道射击游戏。玩家在同一张地形图上轮流发射炮弹，考虑风向和角度，击中对手以减少其 HP，率先将对方 HP 降为零者获胜。

项目服务端基于 sylar C++ 协程框架构建，采用微服务架构拆分为 gate / login / lobby / battle / data 五个独立服务，通过 etcd 做服务注册与发现、Protobuf 定义 RPC 接口；客户端使用 Unity (C#) 实现，通过 TCP 长连接接入网关。整体分为服务端（微服务 + 共享物理引擎）和客户端（战斗/网络/UI 模块）两大模块，附带一套基于 Protobuf + etcd 的 RPC 框架。

## 核心功能
客户端（Unity / C#）
- 回合制弹道射击：采用服务端权威物理引擎，客户端本地复算弹道做视觉回放，逻辑以服务端为准
- 二维体素地形：地形以 bitset 二维网格表达，爆炸只挖圆内格子（平台保留），彻底解决穿地形/误判掉落
- 房间系统：支持创建/加入/离开/准备/切换队伍/切换武器，对局结束后自动返回大厅
- 聊天系统：实现综合/世界/房间/队伍/私聊频道通信
- 账号系统：HTTP 登录拿 token，登录服校验后长连接接入，基于 MySQL 持久化 + Redis 会话管理
- Protobuf 通信协议：网关帧 [length][msg_id][payload]，客户端/服务端共享物理公式与地形位图
- 自动重连：断线后指数退避自动重连，重连后用缓存 token 自动登录恢复状态
- 主线程无阻塞：网络收发在独立线程，发送队列化，杜绝 TCP 反压导致画面冻结

服务端（sylar C++ 协程框架）
- 基于 IOManager 协程调度器，协程化处理大量并发 TCP 连接
- Gate/Login/Lobby/Battle/Data 五服务微服务架构，etcd 注册发现 + Protobuf RPC 互联
- 战斗房间状态用 Mutex 串行化（每房一锁），回合制天然串行，临界区极短
- SessionManager 统一管理连接生命周期，封装踢人、心跳超时、断线清理
- 权威物理引擎 + 二维体素地形：服务端计算弹道碰撞（isSolid 精确到格子）、爆炸挖坑（removeCircle）、AOE 伤害
- MySQL 连接池 + Redis 缓存：账号/对局持久化，会话 token 与在线状态管理


## 展示


### v1.0 20260715更新
客户端使用 `unity` 重构项目，实现基本功能与玩法。服务端由单进程三层演化为 **gate / login / lobby / battle / data 五服务微服务**，服务发现 ZooKeeper → **etcd**，通信 WebSocket → **TCP 长连接 + Protobuf**。

|  |  |
| ---- | ---- |
| ![选择角色](assets/v1.0_1.png) | ![更换武器](assets/v1.0_2.png) |
| ![战斗与聊天](assets/v1.0_3.png) | ![结算](assets/v1.0_4.png) |


### v0.3
纸飞机

![ddt_fly](assets/ddt_fly.png)

基于弹坑地形的优化

![ddt_reverse_throw](assets/ddt_reverse_throw.png)

结算界面

![ddt_game_settlement](assets/ddt_game_settlement.png)

### v0.2
![v0.2_game](assets/ddt_v0.2_game.png)

### v0.1
UI界面

![ddt_ui](assets/ddt_ui.png)

房间界面

![ddt_room](assets/ddt_room.png)

大厅界面

![ddt_lobby](assets/ddt_lobby.png)

聊天界面

![ddt_chat](assets/ddt_chat.png)


## 使用资源和鸣谢
1. [sylar](https://github.com/sylar-yin/sylar) 协程服务器框架
2. [Alpha](https://www.github.com/AlphaMinZ/Alpha) 基于sylar的RPC模块
3. [DDT](https://github.com/iwxyi/DDT) 弹弹堂素材和灵感
4. [浅析弹弹堂物理模型](https://www.52pojie.cn/thread-1132459-1-1.html) 抛物线物理参考
5. [C++ MySQL ORM：数据库操作，数据库增删改查](https://www.bilibili.com/video/BV1bd4y1777d/) C++ ORM参考
6. 鸣谢：[Claude Code] & [GLM Coding Plan] & [Gemini]


## 后记
能力有限，学习了sylar后，一边AI Coding一边学习，代码可能有很多问题，欢迎指正。

## 新分支git提交命令
~~~bash
# 普通提交
git checkout ddt
git add .
git commit -m "xxx"
git push origin ddt

# 撤销并重新提交
git reset --soft HEAD~
git add .
git commit -m "xxx"
git push origin ddt --force

~~~

---

# DDT — 项目文档

各版本完整项目文档，记录每次 GitHub 推送时的项目状态与变更。

## 版本目录

| 版本 | 说明 | 文档 |
|------|------|------|
| v1.0 | 架构级重构 — 客户端重写为 Unity(C#)、服务端五服务微服务化（gate/login/lobby/battle/data）、ZooKeeper→etcd、WebSocket→TCP 长连接 | [v1.0.md](docs/v1.0.md) |
| v0.4 | ORM 模块 + 项目级目录重组 | [v0.4.md](docs/v0.4.md) |
| v0.3 | 架构重构 — Game 模块拆分、SessionManager、14 项架构问题修复、Bug 11-19 修复 | [v0.3.md](docs/v0.3.md) |
| v0.2 | Bug Fix & 体验优化 — 10 项稳定性修复 + 游戏体验改进 | [v0.2.md](docs/v0.2.md) |
| v0.1 | 初始版本 — OpenGL 客户端 + WebSocket 服务端 + RPC 框架 | [v0.1.md](docs/v0.1.md) |

---

# sylar 学习日志

sylar 协程框架的逐模块学习笔记（Version 1-17：日志/配置/线程/协程/调度器/IOManager/Hook/网络/HTTP/TcpServer/ByteArray/WebSocket/守护进程/Env/异步日志/UDP/聊天室）已移至独立文档：

👉 **[docs/sylar-learning-notes.md](docs/sylar-learning-notes.md)**


---

# DDT 弹弹堂 — 项目文档 v1.0

v1.0 为**架构级重构版本**，相对 v0.4 的主要变化：

### 客户端重写：OpenGL(C++/ImGui) → Unity(C#)
- 引擎由自研 OpenGL 3.3 + ImGui + GLFW 改为 **Unity**，全部业务/战斗/网络/UI 脚本用 **C#** 重写（namespace `Ddt.*`）
- 客户端脱离 CMake，改由 Unity Editor 构建；第三方依赖（freetype/glad/glfw/glm/imgui）从 `thirdparty/` 移除
- 渲染改用 SpriteRenderer / Texture2D / LineRenderer + uGUI；网络改用 `TcpClient` + `UnityWebRequest`

### 服务端微服务化：单进程三层 → 五服务微服务
- 由单进程 gate/lobby/match 三层 + db/config 演化为 **5 个独立可执行文件**：gate / login / lobby / battle / data
- 各自在 etcd 注册、经 Protobuf RPC 互联；**data 为唯一持久层**，其余服务经 RPC 访问 MySQL/Redis
- **lobby `tryStart` → battle `EnterBattle`** 跨服务交接；lobby/battle 不持 socket，经 `PushService.NotifyClient(s)` 反推回 gate

### 服务发现 / 通信演进
- 服务发现：**ZooKeeper → etcd v3**（`etcd_client`，lease+keepalive+watcher，PImpl 隔离 gRPC 头）
- 通信：**WebSocket → TCP 长连接**，帧格式 `[4B length][2B msg_id][protobuf payload]`
- 登录：**HTTP `/login` + RPC `ValidateToken`** 双通道（HTTP 拿 token → TCP 首帧 LOGIN → gate 校验）

### 共享层与协议
- 地形：**1D 高度图 → 二维体素位图 `Terrain2D`**（`removeCircle` 挖圆坑但保留上方平台，解决穿地形/误判掉落）
- 协议：v0.4 的 `oneof payload` 聚合 → **`msg_id` 外挂路由**（`common/gate/rpc.proto`，帧层不依赖 proto 头）

### 运维
- 新增 `scripts/fleet.sh` 一键管控五服务（start/stop/status/etcd/logs）
- 新增 `LD_PRELOAD` 垫片 `lib/libetcd_close_fix.so`，规避 sylar hook × gRPC 静态构造的 `close()` 空指针崩溃

> 完整架构、通信协议、服务端/客户端模块、RPC 框架、构建部署、实现细节与版本对比，详见 👉 **[docs/v1.0.md](docs/v1.0.md)**


