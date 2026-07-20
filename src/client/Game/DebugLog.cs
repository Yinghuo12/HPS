// DebugLog.cs — 编译期开关的诊断日志工具
//
// 用法:
//   1. Unity Player Settings → Scripting Define Symbols 加 DDT_DBG 启用全部诊断日志
//   2. 不定义 DDT_DBG 时所有调用编译期消除(零运行时开销), 适合发布包
//
// 调用:
//   using static Ddt.Net.Game.DebugLog;
//   DBGLog("msg");            // 普通
//   DBGWarn("msg");           // 警告(黄色)
//   DBGErr("msg");            // 错误(红色)
//   DBGLogT("category", "msg"); // 带分类前缀, 便于 grep
//
// 设计原则:
//   - 仅状态变化/生命周期/收发事件/协程 start-complete 才打, 不在 Update 里每帧打
//   - 调用零开销: 关闭时 using static 不产生方法体
//   - 与 ClientLogger 不冲突: ClientLogger 服务大厅 UI 面板, 本工具服务 Player.log 排错
using UnityEngine;

namespace Ddt.Net.Game {

public static class DebugLog {
    // 编译期开关: 定义 DDT_DBG 时启用全部诊断日志, 否则全部消除
    public const bool ENABLED =
#if DDT_DBG
        true
#else
        false
#endif
    ;

    [System.Diagnostics.Conditional("DDT_DBG")]
    public static void DBGLog(string msg) {
        Debug.Log($"[DBG] {msg}");
    }

    [System.Diagnostics.Conditional("DDT_DBG")]
    public static void DBGLogT(string tag, string msg) {
        Debug.Log($"[DBG][{tag}] {msg}");
    }

    [System.Diagnostics.Conditional("DDT_DBG")]
    public static void DBGWarn(string msg) {
        Debug.LogWarning($"[DBG] {msg}");
    }

    [System.Diagnostics.Conditional("DDT_DBG")]
    public static void DBGErr(string msg) {
        Debug.LogError($"[DBG] {msg}");
    }
}

} // namespace
