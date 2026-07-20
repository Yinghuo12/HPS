# 客户端诊断日志（DDT_DBG）

客户端提供编译期开关的详细诊断日志，用于定位卡死/异常/网络问题。

## 启用方式

### 方法 1：Unity Editor 菜单

1. 打开 Unity Editor，加载 `DDTClient` 工程
2. `Edit → Project Settings → Player → Other Settings`
3. 找到 **Scripting Define Symbols**（脚本定义符号）
4. 添加 `DDT_DBG`（多个符号用分号分隔，如 `DDT_DBG;OTHER_SYMBOL`）
5. 重新 Build Player（File → Build And Run）

### 方法 2：直接在 DebugLog.cs 改

把 `src/client/Game/DebugLog.cs` 里：

```csharp
public const bool ENABLED =
#if DDT_DBG
    true
#else
    false
#endif
    ;
```

改成无条件 `true`，重新 Build。

## 关闭

默认就是关闭的（`ENABLED=false`）。所有 `DBGLog/DBGWarn/DBGErr/DBGLogT` 调用由于使用 `[Conditional("DDT_DBG")]`，编译期会被完全消除，**零运行时开销**，发布包无需改动。

## 日志在哪看

Build Player 跑出来的应用，日志路径：

- **macOS**: `~/Library/Logs/DefaultCompany/DDTClient/Player.log`
- **Windows**: `%USERPROFILE%\AppData\LocalLow\DefaultCompany\DDTClient\Player.log`
- **Linux**: `~/.config/unity3d/DefaultCompany/DDTClient/Player.log`

Editor Play Mode：直接看 Console 窗口。

**注意**：Unity 每次启动 Player 会覆盖 Player.log。卡死发生后**立刻**读取日志（不要先关 Player），否则日志被覆盖。

## 日志格式

所有诊断日志统一以 `[DBG]` 前缀输出，便于 grep：

```
[DBG][Battle] PlayLanding START players=2
[DBG][Battle] PlayLanding DONE landingDone_=true
[DBG][Battle] PlayIntro START n=2 turnAcc=90001 introDone_=True
[DBG][Battle] PlayIntro pan#0 → acc=90001 pos=(200,382)
[DBG][Battle] PlayIntro final pan → turn=90001 pos=(200,382)
[DBG][Battle] PlayIntro DONE introDone_=true, handoff to BattleController
[DBG][Battle] BattleController.Start me=90001 hasPendingRoomReady=True hasPendingTurnStart=True
[DBG][Battle] OnTurnStart recv: resolving=False field.IsBusy=False (→ APPLY)
[DBG][Battle] ApplyTurnStart: turn=2 turnAcc=90001 myTurn=True wind=-4.3 timeLeft=10
```

分类前缀（`[Battle]`/`[Login]`/`[Lobby]`/`[Net]`/`[NetClient]` 等）便于过滤：

```bash
grep "\[DBG\]\[Battle\]" Player.log   # 只看战斗
grep "\[DBG\]\[Net" Player.log        # 只看网络
grep "WATCHDOG\|main thread hung" Player.log   # 看门狗触发
```

## 关键诊断点（按场景）

### 卡死现场（"准备中..."不消失、操作无响应）

主要看 `[DBG][Battle]` 日志，关键状态机点：

| 日志 | 含义 |
|------|------|
| `PlayLanding START/DONE` | 降落动画 |
| `PlayIntro START/pan#N/final pan/DONE` | 开场巡视 4 阶段 |
| `intro pan timeout after 3s` | 巡视超时（边界外奇点） |
| `OnTurnStart recv: ... (→ BUFFER/APPLY)` | TurnStart 收到时的分支 |
| `ApplyTurnStart: turn=N ... timeLeft=10` | 回合切换 |
| `DelayedApplyTurnStart START/.../applying buffered` | 延迟回合切换协程 |
| `OnShootResult: resolvingShoot_=true` | 射击结果回放开始 |
| `KeepBusy +1/-1 → busyCount=N` | 忙碌计数变化 |
| `Explosion +1/-1` | 爆炸动画 |
| `Projectile START` / `FollowProjectile START/DONE` | 弹道飞行 |

**典型卡死征兆**：日志到 `OnTurnStart recv` 或 `OnShootResult` 后再无任何 `[DBG]` 输出 → 协程被挂起 / 主线程冻结。

### 网络断连 / 重连

主要看 `[NetClient]` 日志：

| 日志 | 含义 |
|------|------|
| `Connect host=... port=...` | 主动连接 |
| `connected, starting recv/send threads` | 连接成功 |
| `RecvLoop exit (wasUserClosed=False ...)` | 连接断开 |
| `StartReconnectLoop (delays=1/2/4/8/15s exponential)` | 重连开始 |
| `reconnect attempt #N in Xms` | 第 N 次重连尝试 |
| `reconnect #N SUCCESS` | 重连成功 |
| `*** WATCHDOG TRIGGERED *** main thread hung for Xs` | 主线程冻结超 5s，强制重连 |

### 切换应用 / 最小化

主要看 `[Net]` 日志：

| 日志 | 含义 |
|------|------|
| `[Net] lost focus (app switch?)` | 失焦（被移除：默认不再打） |
| `[Net] regained focus after Xs, checking connection` | 长时切回（>10s）才打 |
| `[Net] resumed from background (OnApplicationPause)` | 从 background 恢复 |
| `[NetClient] recover: connection alive, cleared backlog` | 连接还活着，仅清积压 |
| `[NetClient] recover: connection disconnected, relying on reconnect loop` | 连接死了，靠重连 |

## 修改建议

新增日志请用 `DBGLog/DBGWarn/DBGErr/DBGLogT`，不要直接 `Debug.Log`（避免污染发布包）。

```csharp
using static Ddt.Net.Game.DebugLog;

void SomeStateChange() {
    // ...
    DBGLogT("Battle", $"SomeState → newValue={x}");
}
```

**原则**：只打**状态变化**和**关键事件**（收发、协程 start/done、生命周期），不要在 Update 里每帧打。
