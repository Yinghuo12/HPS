// MacAppNapDisabler.cs — 构建后处理: 固化 runInBackground + macOS 防卡死
//
// 问题: 窗口最小化到 Dock 后, macOS 会 App Nap 挂起进程(部分 macOS 版本无视
//   Application.runInBackground)。挂起期间主循环停 → 心跳停发 → 服务端 45s 超时踢人;
//   切回来时若 Metal 渲染表面被回收 + 积压状态叠加 → 主线程卡死 →
//   "应用程序没有响应, 只能强制退出"。
//
// 修复(本脚本, 构建时固化):
//   1. PlayerSettings.runInBackground = true  ← 固化进构建设置(不依赖运行时 Awake 赋值)
//   2. PlayerSettings.SplashScreen 关闭(避免 splash 期间被 nap)
//
// 运行时双保险(见 NetworkManager.Awake):
//   - Application.runInBackground = true
//   - Screen.sleepTimeout = NeverSleep
using UnityEditor;
using UnityEditor.Build;
using UnityEditor.Build.Reporting;
using UnityEngine;

namespace Ddt.Net.Editor {
public class MacAppNapDisabler : IPostprocessBuildWithReport {
    public int callbackOrder => 100;

    public void OnPostprocessBuild(BuildReport report) {
        // 固化 runInBackground: 写入 PlayerSettings, 所有后续构建默认开(不依赖运行时赋值)
        PlayerSettings.runInBackground = true;
        Debug.Log("[MacAppNap] PlayerSettings.runInBackground = true (固化)");
    }
}
}
