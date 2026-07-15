// ClientLogger.cs — 客户端日志(分级 + 内存环形缓冲 + UI 面板)
//
// 同时输出到 Unity Console(Debug.Log)和内存缓冲(最近 N 条), 供 LobbyUI 日志面板显示。
// 用法: ClientLogger.Info("...") / Warn("...") / Error("...")
//
// 缓冲用环形数组(固定大小 + 写入游标), 替代旧 List + RemoveAt(0):
// 后者满 500 条后每条新日志都要移动 499 个元素(O(n)), 高频日志时拖慢主线程。
// 环形缓冲写入 O(1), 读取时按写入顺序回放。
using System.Collections.Generic;
using UnityEngine;

namespace Ddt.Net {
public static class ClientLogger {
    public enum Level { INFO, WARN, ERROR }

    public struct Entry {
        public Level level;
        public string msg;
        public float time;   // Time.time
    }

    private const int MAX_ENTRIES = 500;
    // 环形缓冲: 固定大小数组 + 已写入条数。满了就覆盖最旧(等价 List.RemoveAt(0), 但 O(1))。
    private static readonly Entry[] entries_ = new Entry[MAX_ENTRIES];
    private static int entryCount_ = 0;     // 有效条数(<= MAX_ENTRIES)
    private static int writeIdx_ = 0;       // 下一个写入位置(环绕)
    private static readonly Queue<Entry> pending_ = new Queue<Entry>();  // 线程安全投递队列

    // 日志内容变化时 +1, UI 据此判断是否需要刷新
    public static int Version { get; private set; } = 0;

    public static void Info(string msg)  { Log(Level.INFO, msg); }
    public static void Warn(string msg)  { Log(Level.WARN, msg); }
    public static void Error(string msg) { Log(Level.ERROR, msg); }

    private static void Log(Level lv, string msg) {
        var e = new Entry { level = lv, msg = msg, time = Time.time };
        lock (pending_) { pending_.Enqueue(e); }
        // 立即输出到 Unity Console(主线程安全)
        string line = $"[{lv}] {msg}";
        if (lv == Level.ERROR) Debug.LogError(line);
        else if (lv == Level.WARN) Debug.LogWarning(line);
        else Debug.Log(line);
    }

    /// <summary>主线程调用: 把 pending 队列合并进环形缓冲。返回是否变化。</summary>
    public static void Flush() {
        lock (pending_) {
            while (pending_.Count > 0) {
                var e = pending_.Dequeue();
                entries_[writeIdx_] = e;               // 覆盖最旧槽(O(1), 无元素移动)
                writeIdx_ = (writeIdx_ + 1) % MAX_ENTRIES;
                if (entryCount_ < MAX_ENTRIES) entryCount_++;
            }
        }
        Version++;
    }

    /// <summary>获取最近 max 条日志(倒序: 最新在前)。</summary>
    public static List<Entry> GetRecent(int max) {
        int n = System.Math.Min(max, entryCount_);
        var result = new List<Entry>(n);
        // 最新写入的在 writeIdx_-1(回绕), 往前回放 n 条
        for (int i = 1; i <= n; i++) {
            int idx = (writeIdx_ - i + MAX_ENTRIES) % MAX_ENTRIES;
            result.Add(entries_[idx]);
        }
        return result;
    }

    public static string FormatEntry(Entry e) {
        string tag = e.level == Level.ERROR ? "<color=red>ERR</color>"
                   : e.level == Level.WARN  ? "<color=yellow>WRN</color>" : "INF";
        return $"[{tag}] {e.msg}";
    }
}
}
