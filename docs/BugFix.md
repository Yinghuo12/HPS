# DDT BugFix 记录
v0.2
## Bug 1: WebSocket 发送竞态导致闪退 / invalid opcode

**现象**: 创建房间、Join Blue 等操作随机闪退。服务端日志 `invalid opcode=12`。

**原因**: UI 线程和 IO 线程同时调用 `sendWSFrame` 写入同一个 socket，数据帧交错产生乱码。典型场景：UI 发送消息的同时，IO 线程收到 Ping 自动回复 Pong。

**修复**: `ws_client.h` 添加 `std::mutex m_frameMutex`，`sendWSFrame` 入口加 `std::lock_guard`。

```
文件: ddt_client/network/ws_client.h, ws_client.cc
```

---

## Bug 2: ESC 退出时段错误 (Segfault)

**现象**: 按 ESC 退出游戏时控制台报 Segmentation Fault。

**原因**: `NetworkClient::disconnect()` 使用 `detach()` 放飞 IO 线程。主线程退出后全局对象（OpenGL context 等）被销毁，但 detach 的线程仍在 `recvLoop` 中访问已销毁对象。

**修复**: `disconnect()` 中先 `shutdown(socket, SHUT_RDWR)` 使 `recvMessage` 返回，再 `join()` 安全回收线程。

```
文件: ddt_client/network/network_client.cc
```

---

## Bug 3: 开火瞬间子弹贴在身上，直接跳过回合

**现象**: 开火时能看到子弹贴图出现在人物身上，但没有飞行轨迹，直接跳到下一个玩家。

**原因**: 物理引擎起点是玩家脚底坐标。玩家站在地形上（y == heightMap[x]），t=0 瞬间碰撞检测命中地形，原地爆炸。

**修复**: 发射起点偏移到"枪口"位置，X 轴根据朝向偏移 ±25，Y 轴上抬 15 单位。

```
文件: ddt_server/match/ddt_game_room.cc handleShoot()
修改: start_x = shooter->getX() + (dir == 1 ? 25 : -25)
      start_y = shooter->getY() - 15
```

---

## Bug 4: 一方登录有时会卡住

**现象**: 两个客户端连接，其中一个登录后长时间无响应。

**原因**: `loginAccount` 日志显示 MySQL 查询是异步的，在协程中执行。如果 MySQL 连接池耗尽或 Redis 连接超时，协程会阻塞在 `mysql_real_query` 或 Redis 操作上，导致当前协程挂起。由于 sylar IOManager 线程有限（4个），一个阻塞的 MySQL 调用会占住整个线程。

**修复**: 增加 MySQL 连接池大小，添加连接超时配置。同时增加日志帮助定位。

```
文件: ddt_server/gate/ddt_servlet.cc, ddt_server/gate/ddt_main.cc
```

---

## Bug 5: 一个玩家莫名显示失败退出房间

**现象**: 一方突然收到 GameOver 或 OpponentLeft，但另一方仍正常。

**原因**: Bug 1 的竞态条件导致一方的 WebSocket 帧损坏，服务端断开该方连接。`onClose` 触发 `leaveRoom` → `removePlayer` → 向对方发送 `opponent_left_notify`。修复 Bug 1 后此问题应同步解决。

**修复**: 同 Bug 1 的互斥锁修复。

---

## Bug 6: 服务端缺少日志落盘

**现象**: 日志仅输出到控制台，断开 SSH 后无法追溯问题。

**修复**: `ddt_main.cc` 中添加 `FileLogAppender`，日志写入 `logs/ddt_server.log`，格式包含时间、线程、协程、级别、文件:行号。同时在 `handleShoot`、`handleMove`、`joinRoom`、`createRoom` 等关键节点增加 DEBUG 级别日志。

```
文件: ddt_server/gate/ddt_main.cc, ddt_servlet.cc, ddt_server/match/ddt_game_room.cc
日志位置: logs/ddt_server.log
```

---

## Bug 7: WebSocket PING 帧导致 invalid opcode 断连

**现象**: 客户端发送 PING 保活后，服务端日志出现 `invalid opcode=5/7/12`，随后连接断开。规律：每次 PING 后紧接着出现异常帧头。

**原因**: sylar 框架 `ws_session.cc` 的 PING 处理存在 bug。收到 PING 帧后：
1. 读取 2 字节 WSFrameHead（opcode=0x9, mask=1, payload=N）
2. 立即发送 PONG 响应
3. **但没有消费 PING 帧的 mask key（4字节）和 payload 数据**
4. 下一次循环 `readFixSize` 将残留的 mask 字节当作新帧头解析 → 产生乱码 opcode

WebSocket 协议要求客户端→服务端帧必须带 mask。客户端 PING 带 `mask=1`，服务端未读取 mask key 就继续读下一帧，导致帧错位。

**修复**: 在 PING/PONG 处理分支中，发送 PONG 前先消费 mask key 和 payload 数据。PONG 处理同样修复。

```
文件: sylar/http/ws_session.cc WSRecvMessage()
修改: PING/PONG 分支添加 readFixSize 消费 mask + payload
```

---

## Bug 8: macOS Retina 屏幕只显示左下角

**现象**: Mac 上游戏画面只占窗口左下角 1/4，右上角黑色但仍可拖动。

**原因**: macOS Retina 屏幕物理像素是逻辑尺寸的 2 倍。`glViewport(0,0,1200,700)` 使用逻辑尺寸，OpenGL 只渲染到左下角的物理区域。

**修复**: 主循环中每帧用 `glfwGetFramebufferSize` 获取真实物理像素尺寸更新 viewport。

```
文件: ddt_client/main.cc
修改: 渲染循环开头添加 glfwGetFramebufferSize + glViewport
```

---

## Bug 9: 人物悬浮空中无重力

**现象**: 人物站在地形上不会掉落，出生直接站在地面。

**原因**: 客户端没有重力模拟。弹弹堂原版是出生在空中自由落体到地面。

**修复**: 服务端 `startGame` 中出生 Y 坐标改为 0（天空）。客户端 `Update` 中添加重力：检测脚下是否实地，悬空则加速下坠，着地后速度归零并挤出地表。

```
文件: ddt_server/match/ddt_game_room.cc startGame() — spawn_y 改为 0.0f
文件: ddt_client/logic/game.cc Update() — 添加 1500.0f 重力加速度模拟
```

---

## Bug 10: 短时间内可连续蓄力开火

**现象**: 开火后动画播放期间，可以再次蓄力发射。

**原因**: 客户端和服务端都没有锁定"本回合已开火"状态。

**修复**:
- 客户端: 添加 `m_hasShot` 标记，蓄力/发射/按钮均检查。新回合 `kTurnStartNotify` 和 `kRoomReadyNotify` 时重置。
- 服务端: `m_playerShootLocked` map 标记已开火玩家，`nextTurn()` 中 clear。

```
文件: ddt_client/logic/game.h — 添加 m_hasShot
文件: ddt_client/logic/game.cc — ProcessInput/RenderGameHUD/processNetworkMessages
文件: ddt_server/match/ddt_game_room.h — 添加 m_playerShootLocked
文件: ddt_server/match/ddt_game_room.cc — handleShoot/nextTurn
```
