// ChatManager.cs — 聊天数据管理(参照案例 ChatManager 设计, 适配本游戏)
//
// 职责:
//   - 按频道/Tab 存储消息(综合/世界/房间/队伍/私聊)
//   - 频道过滤(综合 Tab 显示所有频道; 其他 Tab 只显示对应频道)
//   - 当前发送频道 + 私聊目标管理
//   - 消息到达时通知 UI 刷新(OnChat 回调)
//
// 不含: 网络收发(由 ChatPanel 直接订阅 Dispatcher)、UI 渲染(由 UIChat 负责)
using System;
using System.Collections.Generic;
using UnityEngine;
using Ddt;

namespace Ddt.Net.Game {
public class ChatManager {
    public static readonly ChatManager Instance = new ChatManager();

    // ---- Tab 定义(与案例 LocalChannel 对应) ----
    // 0=综合 1=世界 2=房间 3=队伍 4=私聊
    public enum ChatTab { All = 0, World = 1, Room = 2, Team = 3, Private = 4 }

    // ---- 消息结构 ----
    public struct ChatMsg {
        public ChannelType channel;     // 原始频道(WORLD/ROOM/TEAM/PRIVATE/SYSTEM)
        public ulong senderId;
        public string senderName;
        public string message;
        public ulong timestamp;
    }

    // 每 Tab 的消息列表
    private readonly List<ChatMsg>[] messages_ = new List<ChatMsg>[5] {
        new List<ChatMsg>(), new List<ChatMsg>(), new List<ChatMsg>(),
        new List<ChatMsg>(), new List<ChatMsg>()
    };
    private const int MAX_MSG_PER_TAB = 200;   // 每 Tab 最多缓存条数

    // ---- 发送状态 ----
    private ChatTab sendTab_ = ChatTab.World;
    private ulong privateTargetId_ = 0;
    private string privateTargetName_ = "";

    // UI 刷新回调
    public event Action OnChatChanged;

    // ---- 当前发送 Tab ----
    public ChatTab SendTab {
        get => sendTab_;
        set => sendTab_ = value;
    }

    // 当前发送对应的频道
    public ChannelType SendChannel {
        get {
            switch (sendTab_) {
                case ChatTab.World: return ChannelType.ChannelWorld;
                case ChatTab.Room: return ChannelType.ChannelRoom;
                case ChatTab.Team: return ChannelType.ChannelTeam;
                case ChatTab.Private: return ChannelType.ChannelPrivate;
                default: return ChannelType.ChannelWorld;
            }
        }
    }

    // ---- 私聊目标 ----
    public ulong PrivateTargetId => privateTargetId_;
    public string PrivateTargetName => privateTargetName_;
    public void SetPrivateTarget(ulong id, string name) {
        privateTargetId_ = id;
        privateTargetName_ = name ?? "";
    }

    // ---- 获取某 Tab 的消息列表(只读) ----
    public IReadOnlyList<ChatMsg> GetMessages(ChatTab tab) {
        return messages_[(int)tab];
    }

    // ---- 添加一条收到的消息(按频道分发到对应 Tab + 综合 Tab) ----
    public void AddMessage(ChatMsg msg) {
        // 综合 Tab: 所有消息都进
        AddToTab(ChatTab.All, msg);
        // 对应频道 Tab
        ChatTab targetTab = ChannelToTab(msg.channel);
        if (targetTab != ChatTab.All) AddToTab(targetTab, msg);
        // 触发刷新
        OnChatChanged?.Invoke();
    }

    // 系统消息(仅综合 Tab)
    public void AddSystemMessage(string text) {
        var msg = new ChatMsg {
            channel = ChannelType.ChannelSystem,
            senderId = 0, senderName = "系统", message = text, timestamp = 0
        };
        AddToTab(ChatTab.All, msg);
        OnChatChanged?.Invoke();
    }

    // ---- 清空所有消息(切场景/重连时) ----
    public void Clear() {
        foreach (var list in messages_) list.Clear();
        OnChatChanged?.Invoke();
    }

    // ---- 内部 ----
    private void AddToTab(ChatTab tab, ChatMsg msg) {
        var list = messages_[(int)tab];
        list.Add(msg);
        // 超出上限截断(保留最新的)
        if (list.Count > MAX_MSG_PER_TAB) {
            list.RemoveRange(0, list.Count - MAX_MSG_PER_TAB);
        }
    }

    // 频道 → Tab 映射
    public static ChatTab ChannelToTab(ChannelType ch) {
        switch (ch) {
            case ChannelType.ChannelWorld: return ChatTab.World;
            case ChannelType.ChannelRoom: return ChatTab.Room;
            case ChannelType.ChannelTeam: return ChatTab.Team;
            case ChannelType.ChannelPrivate: return ChatTab.Private;
            case ChannelType.ChannelSystem: return ChatTab.All;   // 系统只在综合
            default: return ChatTab.World;
        }
    }

    // Tab → 频道颜色(hex)
    public static string TabColorHex(ChannelType ch) {
        switch (ch) {
            case ChannelType.ChannelWorld: return "#00FF80";    // 绿
            case ChannelType.ChannelTeam: return "#FFCC33";     // 黄
            case ChannelType.ChannelPrivate: return "#CC66FF";  // 紫
            case ChannelType.ChannelSystem: return "#FFAA00";   // 橙
            default: return "#FFFFFF";                          // 房间 白
        }
    }
}
}
