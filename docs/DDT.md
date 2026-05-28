# DDT 弹弹堂 — 项目文档 v0.2


## 目录

- [1. 项目概述](#1-项目概述)
- [2. 技术选型](#2-技术选型)
- [3. 项目架构](#3-项目架构)
- [4. 通信协议](#4-通信协议)
- [5. 服务端](#5-服务端)
- [6. 客户端](#6-客户端)
- [7. RPC 框架](#7-rpc-框架)
- [8. 构建与部署](#8-构建与部署)
- [9. 测试指南](#9-测试指南)
- [10. 实现细节](#10-实现细节)
- [11. 版本更新日志](#11-版本更新日志)

---

## 1. 项目概述

DDT（弹弹堂）是一款基于 C/S 架构的实时多人回合制弹道射击游戏。玩家在同一张地形图上轮流发射炮弹，考虑风向和角度，击中对手以减少其 HP，率先将对方 HP 降为零者获胜。

项目基于 sylar C++ 协程框架构建服务端，客户端使用 OpenGL 3.3 渲染，通过 WebSocket 进行实时通信。整体分为服务端（gate/lobby/match/battle 四层）和客户端（逻辑层/渲染层/网络层）两大模块，附带一套基于 Protobuf + ZooKeeper 的 RPC 框架。

---

## 2. 技术选型

### 2.1 服务端

| 组件 | 技术 | 用途 |
|------|------|------|
| 框架 | sylar | C++ 协程框架，提供 IOManager、TcpServer、WebSocket、日志、配置等基础设施 |
| 序列化 | Protobuf | 游戏协议定义与序列化 |
| 通信 | WebSocket | 双向实时通信 |
| 数据库 | MySQL | 账号、玩家资料、聊天记录、对战记录、好友关系持久化 |
| 缓存 | Redis | 会话 token、在线状态、房间缓存 |
| 配置 | YAML | 游戏参数配置（物理、地形、战斗） |
| 服务发现 | ZooKeeper | RPC 服务注册与发现 |
| 构建 | CMake + Make | 编译构建 |

### 2.2 客户端

| 组件 | 技术 | 用途 |
|------|------|------|
| 窗口/输入 | GLFW | 跨平台窗口管理与输入事件 |
| 渲染 | OpenGL 3.3 Core | 2D 精灵渲染、地形渲染、粒子效果 |
| 文字 | FreeType | 中文文字渲染 |
| 贴图 | stb_image | PNG 纹理加载 |
| 数学 | GLM | 矩阵变换、正交投影 |
| UI | ImGui | 游戏界面面板（登录、房间、聊天等） |
| 网络 | libwebsocket / 原生 Socket | WebSocket 客户端连接 |
| UI 集成 | ImGui + GLFW + OpenGL3 | ImGui 后端绑定 |

### 2.3 选型理由

- **sylar 框架**：基于协程的异步 IO，单线程即可处理大量并发连接，适合游戏服务端
- **WebSocket**：浏览器级双向通信协议，穿透防火墙能力强，客户端实现简单
- **Protobuf**：强类型 schema，自动生成序列化代码，向后兼容
- **OpenGL**：跨平台图形 API，精灵批量渲染性能优异
- **FreeType**：支持 CJK 字符渲染，弹弹堂需要中文 UI

---

## 3. 项目架构

### 3.1 目录结构

```
sylar/
├── sylar/                       # sylar 框架源码
│   ├── log.h/.cc                # 日志系统
│   ├── fiber.h/.cc              # 协程
│   ├── iomanager.h/.cc          # IO 调度器
│   ├── tcp_server.h/.cc         # TCP 服务器
│   ├── http/ws_server.h/.cc     # WebSocket 服务器
│   ├── config.h/.cc             # 配置系统
│   └── rpc/                     # RPC 框架扩展
│       ├── rpc_provider.h/.cc   # RPC 服务端
│       ├── rpc_channel.h/.cc    # RPC 客户端通道
│       ├── rpc_controller.h/.cc # RPC 控制器
│       └── rpcheader.proto      # RPC 协议头
├── ddt_server/                  # DDT 游戏服务端
│   ├── gate/                    # 网关层（入口 + 路由）
│   ├── lobby/                   # 大厅层（认证 + 聊天）
│   ├── match/                   # 匹配层（房间 + 玩家管理）
│   ├── battle/                  # 战斗层（物理引擎）
│   ├── common/                  # 共享配置
│   ├── db/                      # 数据库操作
│   ├── conf/                    # 配置文件
│   └── schema.sql               # 数据库建表脚本
├── ddt_client/                  # DDT 游戏客户端
│   ├── logic/                   # 游戏逻辑
│   ├── render/                  # 渲染引擎
│   ├── network/                 # 网络通信
│   ├── battle/                  # 战斗相关
│   ├── ui/                      # UI 界面
│   ├── common/                  # 共享工具
│   ├── shaders/                 # GLSL 着色器
│   ├── assets/                  # 纹理贴图
│   └── cmake/                   # 构建模块
├── rpc_test/                    # RPC 测试代码
├── proto/                       # Protobuf 协议定义
├── scripts/                     # 构建/发布脚本
├── docs/                        # 文档
├── framework/                   # sylar 依赖头文件
└── CMakeLists.txt               # 根构建文件
```

### 3.2 系统架构图

```
┌──────────────┐    WebSocket    ┌──────────────────────────────────┐
│  DDT Client  │ ◄────────────► │          DDT Server              │
│  (OpenGL)    │                 │                                  │
│              │                 │  ┌─────────┐   ┌──────────────┐  │
│  - GLFW      │                 │  │  Gate   │──►│    Lobby     │  │
│  - SpriteBatch│                │  │ (入口)  │   │ (认证/聊天)  │  │
│  - ImGui     │                 │  └─────────┘   └──────┬───────┘  │
│  - FreeType  │                 │                       │          │
└──────────────┘                 │  ┌─────────────┐  ┌──▼────────┐ │
                                 │  │   Battle    │◄─┤  Match    │ │
                                 │  │ (物理引擎) │  │ (房间)    │ │
                                 │  └──────┬──────┘  └───────────┘ │
                                 │         │                       │
                                 │  ┌──────▼──────┐                │
                                 │  │  MySQL/Redis │                │
                                 │  └─────────────┘                │
                                 └──────────────────────────────────┘
```

---

## 4. 通信协议

### 4.1 Protobuf 定义

协议定义在 `proto/ddt.proto`，使用 `oneof payload` 实现多态消息：

```protobuf
message GameMessage {
  oneof payload {
    LoginRequest       login_request       = 1;
    LoginResponse      login_response      = 2;
    JoinRoomRequest    join_room_request   = 3;
    // ... 40+ 消息类型
  }
}
```

### 4.2 消息分类

| 类别 | 消息 | 说明 |
|------|------|------|
| **账号** | Login / Register | 登录注册 |
| **房间** | JoinRoom / CreateRoom / RoomList / LeaveRoom / Ready / SwitchTeam | 房间管理 |
| **战斗** | TurnStart / Shoot / ShootResult / Move / GameOver | 回合制战斗 |
| **聊天** | Chat / ChatHistory / PrivateChat | 多频道聊天 |
| **好友** | FriendAdd / FriendList | 好友系统 |
| **通知** | Error / ServerShutdown / OpponentLeft | 系统通知 |

### 4.3 战斗数据流

```
TurnStartNotify (服务端 → 双方)
  ├── turn_player_id: 当前回合玩家
  ├── wind: 风力值
  └── turn_number: 回合数

ShootRequest (当前玩家 → 服务端)
  ├── angle: 发射角度
  └── force: 发射力度

ShootResultNotify (服务端 → 双方)
  ├── shooter_id: 射手 ID
  ├── points[]: 弹道轨迹点 (x, y, t)
  ├── hit_x, hit_y: 落点坐标
  ├── hit_player: 是否命中玩家
  ├── damage: 伤害值
  ├── damage_type: NORMAL / CRITICAL / BLOCK
  └── updated_player1/2: 更新后的玩家状态
```

---

## 5. 服务端

### 5.1 分层架构

#### Gate 层（`ddt_server/gate/`）

- `ddt_main.cc`：服务端入口，初始化 IOManager、数据库、房间管理器，启动 WebSocket 服务器
- `ddt_servlet.h/.cc`：消息路由，解析 `GameMessage`，根据 `oneof payload` 分发到对应处理器

#### Lobby 层（`ddt_server/lobby/`）

- `ddt_auth.h/.cc`：账号认证，登录/注册处理，密码加盐哈希，token 生成与验证
- `ddt_chat_manager.h/.cc`：聊天管理，支持 Team/All/Room/World/System/Broadcast/Private 七个频道，聊天记录持久化

#### Match 层（`ddt_server/match/`）

- `ddt_room_manager.h/.cc`：房间管理，创建/销毁/列表/匹配，房间状态机
- `ddt_game_room.h/.cc`：单个房间逻辑，玩家加入/离开/准备/切换队伍，游戏开始触发
- `ddt_player.h/.cc`：玩家数据，HP/位置/角度/方向/状态

#### Battle 层（`ddt_server/battle/`）

- `ddt_physics.h/.cc`：物理引擎，弹道模拟（重力+风力+空气阻力），地形碰撞检测，伤害计算

#### Common 层（`ddt_server/common/`）

- `ddt_config.h/.cc`：配置加载，从 `conf/game.yml` 读取物理参数、游戏规则、服务器配置

#### DB 层（`ddt_server/db/`）

- `ddt_database.h/.cc`：MySQL 连接池，CRUD 操作封装

### 5.2 物理引擎

弹道模拟参数（`conf/game.yml`）：

```yaml
physics:
  air_factor: 0.89927083      # 空气阻力系数
  wind_factor: 5.8709153      # 风力系数
  gravity_factor: -172.06527992  # 重力
  force_factor: 41.0          # 力度系数
  dt: 0.01                    # 模拟步长
```

弹道方程（每帧）：
```
vx *= air_factor
vx += wind * wind_factor * dt
vy += gravity * dt
x  += vx * dt
y  += vy * dt
```

### 5.3 数据库

`schema.sql` 定义 5 张表：

| 表 | 用途 |
|----|------|
| `accounts` | 账号（用户名、密码哈希、盐） |
| `player_profiles` | 玩家资料（昵称、等级、经验、胜败） |
| `chat_history` | 聊天记录（频道、发送者、内容、时间） |
| `game_records` | 对战记录 |
| `friends` | 好友关系 |

---

## 6. 客户端

### 6.1 模块划分

#### 逻辑层（`ddt_client/logic/`）

| 文件 | 职责 |
|------|------|
| `game.h/.cc` | 游戏主循环：初始化、渲染、输入、状态管理、网络消息处理 |
| `game_object.h/.cc` | 游戏对象基类（位置、大小、旋转、颜色、纹理） |
| `projectile.h/.cc` | 弹道物体：根据服务端轨迹点插值渲染飞行弹丸 |

#### 渲染层（`ddt_client/render/`）

| 文件 | 职责 |
|------|------|
| `shader.h/.cc` | GLSL 着色器编译、链接、uniform 设置 |
| `texture.h/.cc` | OpenGL 纹理封装：生成、绑定、格式设置 |
| `resource_manager.h/.cc` | 全局资源管理：Shader/Texture 按名称缓存，路径自动检测 |
| `sprite_renderer.h/.cc` | 单精灵渲染器 |
| `sprite_batch.h/.cc` | 批量精灵渲染：按纹理排序合并 draw call，6 顶点/精灵 |
| `terrain.h/.cc` | 地形渲染：高度图生成、地形线段绘制、爆炸破坏 |
| `text_renderer.h/.cc` | FreeType 文字渲染：加载字体、渲染中文文本 |
| `camera.h/.cc` | 摄像机系统：跟随玩家/弹道，右键拖拽平移 |

#### 网络层（`ddt_client/network/`）

| 文件 | 职责 |
|------|------|
| `ws_client.h/.cc` | WebSocket 客户端：连接、发送、接收 |
| `network_client.h/.cc` | 网络客户端：Protobuf 序列化/反序列化，消息回调分发 |

### 6.2 渲染流程

```
Begin(projection)
  ├── 绘制背景（bg_rainbow / bg_ghost）
  ├── 绘制地形（Terrain::Draw）
  ├── 绘制玩家精灵（SpriteBatch::Draw × 2）
  ├── 绘制弹道（Projectile::Draw）
  ├── 绘制爆炸效果
  └── 绘制 UI（ImGui）
End()
  └── flush() — 按纹理排序，合并 draw call
```

### 6.3 摄像机模式

| 模式 | 行为 |
|------|------|
| INTRO | 开场镜头 |
| FOLLOW_TURN | 跟随当前回合玩家 |
| FOLLOW_PROJ | 跟随飞行中的弹丸 |
| MANUAL | 右键拖拽自由平移 |

### 6.4 坐标系统

使用 Y 轴向下的正交投影：

```cpp
glm::ortho(left, right, bottom, top, near, far)
// bottom > top → Y 轴向下
```

纹理加载不翻转（`stbi_set_flip_vertically_on_load` 设为 false），与 Y-down 投影配合直接映射。

### 6.5 纹理资源

共 36 张 PNG 纹理，存放在 `ddt_client/assets/`：

| 类别 | 文件 | 尺寸 |
|------|------|------|
| 角色 | player1.png, player1_r.png, player2.png, player2_r.png, role.png, role_r.png, role2.png, role2_r.png | 40×40 ~ 40×59 |
| 武器 | tri_darts, ice_cream 及其 _r/_bomb 变体, projectile 及其 _r/_bomb 变体 | 30×30 ~ 40×40 |
| 效果 | bow0-3, explosion0-3, bomb, fly, flyAttack, flyAttack_r | 40×40 ~ 80×80 |
| 背景 | bg_rainbow, bg_ghost | 1000×600 |
| 启动画面 | bg_start | 1000×657 |

资源路径自动检测（`findAssetBase()`）：运行时依次尝试 `assets/`、`ddt_client/assets/`、`../ddt_client/assets/`、`../../ddt_client/assets/`。

### 6.6 着色器

`shaders/sprite.vert` / `shaders/sprite.frag`：OpenGL 3.33 Core Profile，顶点着色器完成 model × projection 变换，片段着色器进行纹理采样与颜色混合。

---

## 7. RPC 框架

在 sylar 框架基础上扩展了一套基于 Protobuf + ZooKeeper 的 RPC 框架，位于 `sylar/rpc/`。

### 7.1 架构

```
Caller (客户端)                          Callee (服务端)
    │                                        │
    │  RpcChannel.CallMethod()               │  RpcProvider
    │  ├── ZK 查找服务地址                    │  ├── notifyService()
    │  ├── 序列化请求                         │  ├── ZK 注册服务
    │  ├── Socket 连接                        │  └── handleClient()
    │  └── 发送/接收                          │      ├── 解析 RpcHeader
    │                                        │      ├── 查找 method
    │  <──── TCP ────>                        │      └── CallMethod()
```

### 7.2 线路协议

```
请求: [4字节 header_size (网络序)][RpcHeader protobuf][args protobuf]
响应: [4字节 response_size (网络序)][response protobuf]
```

### 7.3 ZooKeeper 路径

服务注册路径：`/${service_name}/${method_name}`，节点数据为 `host:port`。

### 7.4 测试

测试代码在 `rpc_test/`：

```bash
# 编译
bash scripts/build.sh rpc

# 启动 ZooKeeper
sudo systemctl start zookeeper

# 启动服务端
./bin/rpc_test_callee

# 运行客户端
./bin/rpc_test_caller
```

---

## 8. 构建与部署

### 8.1 环境依赖

**服务端（Linux）：**
```bash
sudo apt install -y build-essential cmake protobuf-compiler \
  libmysqlclient-dev libredis-dev libyaml-cpp-dev \
  libzookeeper-mt-dev zookeeper
```

**客户端（Linux）：**
```bash
sudo apt install -y build-essential cmake \
  libglfw3-dev libfreetype-dev libgl1-mesa-dev
```

**客户端（macOS）：**
```bash
xcode-select --install
brew install glfw freetype protobuf
```

### 8.2 构建

统一构建脚本 `scripts/build.sh`：

```bash
cd /path/to/sylar

# 构建服务端
bash scripts/build.sh server

# 构建客户端（需图形环境）
bash scripts/build.sh client

# 构建全部
bash scripts/build.sh all

# 构建 RPC 测试
bash scripts/build.sh rpc

# 清理
bash scripts/build.sh clean
```

### 8.3 数据库初始化

```bash
mysql -u root -p < ddt_server/schema.sql
```

需在 `ddt_server/conf/game.yml` 中配置数据库连接信息。

### 8.4 发布打包

```bash
# 打包客户端发布版（源码 + 依赖，目标机器自编译）
bash scripts/build.sh release

# 输出: dist/ddt_client_linux.tar.gz, dist/ddt_client_macos.tar.gz
```

发布包内容：
- `src/` — 全部客户端源码（flat 结构）
- `thirdparty/` — glad、stb、imgui、freetype、protobuf_src、glfw、glm
- `assets/` — 字体 + 全部 PNG 纹理
- `shaders/` — GLSL 着色器
- `proto/` — Protobuf 定义
- `CMakeLists.txt` — 独立构建配置（standalone，无系统依赖）
- `ddt.sh` — 一键构建运行脚本

用户在目标机器上：
```bash
tar xzf ddt_client_linux.tar.gz
cd ddt_client_linux
./ddt.sh
```

`ddt.sh` 自动检测编译依赖，执行 cmake + make，创建桌面快捷方式（Linux .desktop / macOS .app）。

### 8.5 运行

```bash
# 启动服务端
./bin/ddt_server

# 启动客户端（需图形环境）
./bin/ddt_client
```

---

## 9. 测试指南

### 9.1 本机测试

```bash
# 终端 1：启动服务端
bash scripts/build.sh server && ./bin/ddt_server

# 终端 2：启动客户端 1
bash scripts/build.sh client && ./bin/ddt_client

# 终端 3：启动客户端 2
./bin/ddt_client
```

客户端连接地址填 `127.0.0.1:8073`。

### 9.2 局域网两台电脑测试

```bash
# 服务端机器
cd /home/yinghuo/code/proj/sylar && bash scripts/build.sh server && ./bin/ddt_server
```

```bash
# 本机终端（端口转发）
# 1. 查看活跃网卡 IP
ifconfig | grep 'inet ' | grep -v 127.0.0.1
# 2. 例如得到局域网 IP: 192.168.31.109

# 3. 端口转发
brew install socat
socat TCP-LISTEN:8074,bind=0.0.0.0,fork TCP:192.168.139.145:8073

# 4. 本机客户端连接 127.0.0.1:8073
```

```bash
# 第二台电脑客户端连接
# 局域网 IP:转发端口
192.168.31.109:8074
```

### 9.3 公网连接 — cpolar

```bash
# 服务端机器安装 cpolar
curl -L https://www.cpolar.com/static/downloads/install-release-cpolar.sh | sudo bash
cpolar authtoken 你的账号令牌
cpolar tcp 8073
# 显示: tcp://2.tcp.cpolar.cn:xxxxx

# 客户端连接
2.tcp.cpolar.cn:xxxxx
```

### 9.4 公网连接 — 樱花 FRP

不使用云服务器，通过免费内网穿透实现公网连接。

**操作步骤：**

1. 在 [Sakura FRP 官网](https://www.natfrp.com/) 注册账号，进入控制台创建一条 **TCP 隧道**
   - 本地 IP：`127.0.0.1`
   - 本地端口：`8073`

2. 下载客户端：https://www.natfrp.com/tunnel/download

3. 登录网页端控制台启动隧道：https://www.natfrp.com/remote/v2

4. 通过控制台日志找到公网 IP 和端口，客户端填入即可连接

---
## 10. 实现细节

### 一、 核心同步机制：权威服务器 + 表现层客户端

这是该项目最亮眼的设计之一。对于弹弹堂这类竞技游戏，防外挂和多端同步是重中之重。

**1. 物理弹道同步方案（源码 `ddt_physics.cpp` & `projectile.cpp`）**
你没有采用两端各自计算物理或者简单的帧同步，而是采用了**绝对的服务器权威（Authoritative Server）模型**：
*   **服务端瞬间推演**：当玩家按下 FIRE 时，服务端 `GameRoom::handleShoot` 会调用 `PhysicsEngine::computeTrajectory`。服务端利用带有空气阻力、风力、重力的微积分公式（完全复刻了弹弹堂的经典物理常数 `AIR_FACTOR`, `WIND_FACTOR`），在极短时间内（通过 `dt=0.01` 循环）计算出炮弹的**完整飞行轨迹点**（包含 x, y, t）和最终落点。
*   **客户端插值播放**：服务端将带有时间戳的轨迹点数组（`TrajectoryPoint`）通过 Protobuf 下发。客户端 `Projectile::Update` 完全不进行物理运算，只根据逝去的时间 `m_elapsedTime` 在两个轨迹点之间进行**线性插值（Lerp）**。
*   **评价**：这种设计**彻底杜绝了客户端物理外挂（如修改重力、穿墙穿地）**，同时解决了网络抖动导致的位置不同步问题。

### 二、 客户端架构分析 (OpenGL 2D 引擎)

你的客户端相当于一个精简版的 Cocos2d-x，麻雀虽小五脏俱全。

**1. 渲染管线与优化（`sprite_batch.cpp` & `sprite_renderer.cpp`）**
*   **亮点 - 实现了 SpriteBatch**：这是 2D 游戏极佳的性能优化点。你没有对每个精灵调用一次 `glDrawArrays`，而是根据 TextureID 进行排序（`std::stable_sort`），将使用相同贴图的顶点数据合并到一个 VBO 中，最后只需一次 DrawCall 即可绘制大量物件。
*   **UI 与字体**：巧妙地集成了 `ImGui` 做游戏 UI，极大地降低了手写 UI 控件的成本。同时手写了 `TextRenderer` 结合 `FreeType` 实现了支持 UTF-8 编码的动态字体渲染。

**2. 核心难点：地形破坏的实现（`terrain.cpp`）**
*   **实现机制**：你使用了**FBO（帧缓冲区对象）**技术。初始化时生成完整的地形贴图。当服务端通知某处发生爆炸时，调用 `RemoveCircle`。
*   **挖洞算法**：通过 `glReadPixels` 将受影响区域的像素读回 CPU，遍历圆范围内的像素，将其 Alpha 通道设为 0，再通过 `glTexSubImage2D` 传回 GPU，实现了经典的“像素级地形破坏”。

**3. 纯手写 WebSocket 客户端（`ws_client.cpp`）**
*   脱离了笨重的库（如 Boost.Asio/websocketpp），你用原生 socket 结合 `select` 实现了非阻塞的 TCP 连接，并手写了 WebSocket 的握手协议（SHA1 + Base64）和帧解析（Masking/Unmasking）。这体现了极强的网络底层功底。

### 三、 服务端架构分析 (基于 Sylar)

**1. 协程并发与异步 IO**
*   服务端入口 `ddt_server.cpp` 结合了 Sylar 的 `IOManager` 和 `WSServer`。这意味着每个玩家的 WebSocket 连接、甚至每一个定时器（如回合倒计时 `m_turnTimer`）都在**用户态协程**中运行，能以极低的资源消耗支撑大量并发房间。

**2. 房间状态机（`ddt_game_room.cpp`）**
*   每个 `GameRoom` 是一个完整的状态机，管理了玩家加入、准备、游戏开始、回合切换、断线重连等生命周期。
*   **防作弊检测**：在 `handleMove` 中，你不仅校验了单次移动步长（`move_speed`），还记录了 `m_playerMoveUsed` 限制一回合内的总移动距离（`max_move_per_turn`），逻辑非常严密。

**3. 数据持久化与缓存（`ddt_database.cpp`）**
*   **MySQL 连接池**：手写了基于 `sylar::Semaphore` 的阻塞式连接池，并在操作时使用了 RAII 机制（`MySQLGuard`），避免了连接泄漏。
*   **Redis 会话管理**：登录后生成 32 位随机 Token 存入 Redis 并设置过期时间，处理了单点登录和心跳状态。
*   **密码安全**：内嵌了纯 C++ 的 SHA-256 算法，并结合随机 Salt 存储密码（`hashPassword`），达到了商业级的安全规范。

---

### 四、 深度 Code Review 与优化建议（重点）

尽管代码非常优秀，但在面对更高并发和更复杂的游戏需求时，存在以下几个可以优化的瓶颈：

#### 1. 客户端地形破坏的性能瓶颈 (`terrain.cpp`)
*   **问题**：`RemoveCircle` 中使用了 `glReadPixels`。这是一个**极其昂贵的操作**，因为它会强制 CPU 等待 GPU 清空渲染流水线（Pipeline Stall）。如果在战斗密集的场景下，每次爆炸都会导致游戏瞬间卡顿（掉帧）。
*   **优化建议**：**在 CPU 端维护一份地形的 Bitmap（如 `std::vector<uint8_t>`）副本**。当爆炸发生时，直接在 CPU 端的这个 `vector` 中修改 Alpha 值，然后单向调用 `glTexSubImage2D` 提交给 GPU。删掉 `glReadPixels`，你会发现挖坑的性能有质的飞跃。

#### 2. 服务端与客户端地形模型的差异 (`ddt_game_room.cpp` vs `terrain.cpp`)
*   **问题**：我注意到服务端 `GameRoom::generateHeightMap()` 使用的是一维数组 `std::vector<float> m_heightMap` 记录高度，当爆炸发生时，`applyExplosion` 是将该位置的高度**直接拉低**（`m_heightMap[x] = bottomEdge + 1.0f`）。
    *   这就意味着你的服务端目前只支持**“U型地形”**，不支持**“O型洞穴”**或者**“悬空岛屿”**。
    *   而你的客户端是通过像素 Alpha 扣图，实际上是支持洞穴和悬空岛的。这就导致了**前后端物理模型不一致**：如果玩家打出一个横向的洞，客户端显示上面有土，但服务端的炮弹却会直接穿过去（因为服务端只有 1D 高度）。
*   **优化建议**：服务端也需要将 1D 的 `HeightMap` 升级为 2D 的 `Bitset` 掩码（或者用一维数组+位运算表示）。每次碰撞检测时，像客户端一样检测具体像素是否为固体。

#### 3. 客户端网络层的阻塞风险 (`ws_client.cpp`)
*   **问题**：在 `recvBinary` 中，你使用了 `select` 并设置了 Timeout 来进行轮询。虽然用了非阻塞模型，但这仍然在一个独立的 `std::thread` 中运行。
*   **建议**：既然你的服务端用了 Sylar，你的客户端其实也可以引入一个轻量级的跨平台 EventLoop（比如 `libuv` 或者你把 sylar 的 iomanager 剥离到客户端），用 Epoll/Kqueue 替代 Select 做到真正的事件驱动，降低 CPU 占用。

#### 4. 内存管理优化
*   在 `ddt_servlet.cpp` 中，`Player` 被大量使用 `std::shared_ptr` 管理。在 C++ 游戏服务器中，玩家对象的频繁创建和销毁容易产生内存碎片。建议引入一个类似 `ObjectPool<Player>` 的对象池来复用内存。

### 总结
这是一个**可以直接写入大厂校招甚至社招简历核心位置的神仙项目**。它不仅涵盖了全栈架构，还深入了计算机图形学、游戏物理学和底层网络协议。如果你把上面提到的”地形前后端不一致”和”glReadPixels 性能瓶颈”修复，这个项目在技术深度上将几乎无懈可击。

---

## 11. 版本更新日志

### v0.2 — Bug Fix & 体验优化

**稳定性修复：**

| # | 问题 | 根因 | 修复 |
|---|------|------|------|
| 1 | WebSocket 发送竞态闪退 | UI/IO 线程同时写 socket，帧数据交错 | `ws_client.h` 添加 `std::mutex`，`sendWSFrame` 加锁 |
| 2 | ESC 退出 Segfault | `detach()` 放飞线程，主线程退出后访问已销毁对象 | `shutdown()` + `join()` 安全回收 |
| 3 | 子弹贴在身上直接跳回合 | 物理起点在脚底，t=0 命中地形 | 枪口偏移 ±45px/X, -40px/Y |
| 4 | 登录偶尔卡住 | MySQL 连接池耗尽阻塞协程 | 增加连接池大小和超时配置 |
| 5 | 一方莫名退出 | Bug 1 的竞态导致帧损坏 | 同 Bug 1 的 mutex 修复 |
| 6 | 服务端日志不落盘 | 仅控制台输出 | 添加 `FileLogAppender` → `logs/ddt_server.log` |
| 7 | PING 帧导致 `invalid opcode` 断连 | sylar `ws_session.cc` 未消费 PING 的 mask+payload，帧错位 | PING/PONG 分支添加 `readFixSize` 消费数据 |
| 8 | macOS Bus Error | RGB 纹理缺少行对齐设置 | `glPixelStorei(GL_UNPACK_ALIGNMENT, 1)` |

**游戏体验优化：**

| # | 改动 | 说明 |
|---|------|------|
| 9 | Mac Retina 屏幕适配 | 每帧 `glfwGetFramebufferSize` 获取物理像素尺寸，解决高 DPI 屏幕画面只占左下角 |
| 10 | 重力系统 | 服务端出生 Y=0（天空），客户端添加 1500 重力加速度自由落体，着地停稳 |
| 11 | 防连发开火 | 客户端 `m_hasShot` 锁 + 服务端 `m_playerShootLocked` 双重拦截，新回合重置 |

**构建改进：**

- CMake FetchContent 自动下载 Boost、yaml-cpp、GLFW、FreeType
- `scripts/build.sh` 支持 `setup/server/client/all` 子命令

详细修复记录见 [BugFix.md](BugFix.md)

### v0.1 — 初始版本

- OpenGL 3.3 客户端 + sylar WebSocket 服务端
- 回合制弹道射击、房间系统、聊天系统
- 账号注册/登录（MySQL + Redis）
- Protobuf 通信协议
- RPC 框架（Protobuf + ZooKeeper）
