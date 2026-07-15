// UIChat.cs — 聊天 UI 层(参照案例 UIChat 设计)
//
// 职责:
//   - 自订阅 MSG_CHAT_NOTIFY(网络收发层, 最小化)
//   - 读 ChatManager 数据渲染消息区
//   - 频道 Tab 切换(综合/世界/房间/队伍/私聊)
//   - 发送(委托 ChatManager.SendChannel + GameFacade)
//   - 可折叠/展开
//
// 挂载: LobbyUI.Start / BattleController.BuildHudIfMissing 调 UIChat.Create(canvas)
using System.Collections.Generic;
using System.Text;
using UnityEngine;
using UnityEngine.UI;
using Ddt;
using Ddt.Net;
using Ddt.Net.Game;

namespace Ddt.Net.UI {
public class UIChat : MonoBehaviour {
    private const float PANEL_W = 360f;
    private const float PANEL_H = 220f;
    private const float COLLAPSED_H = 26f;

    private RectTransform rootRt_;
    private GameObject contentArea_;
    private bool collapsed_ = false;

    private Text msgText_;
    private InputField inputField_;
    private ScrollRect scrollRect_;
    private Text titleText_;

    private ChatManager.ChatTab displayTab_ = ChatManager.ChatTab.All;
    private readonly List<Image> channelBtnImgs_ = new List<Image>();
    private readonly List<GameObject> channelBtnGos_ = new List<GameObject>();
    private readonly List<ChatManager.ChatTab> channelTabs_ = new List<ChatManager.ChatTab>();

    // ---- 创建并挂到 canvas ----
    public static UIChat Create(Canvas canvas) {
        if (canvas == null) return null;
        var go = new GameObject("UIChat", typeof(RectTransform));
        go.transform.SetParent(canvas.transform, false);
        var rt = go.GetComponent<RectTransform>();
        rt.anchorMin = rt.anchorMax = new Vector2(0f, 0f);
        rt.pivot = new Vector2(0f, 0f);
        rt.anchoredPosition = new Vector2(12f, 12f);
        rt.sizeDelta = new Vector2(PANEL_W, PANEL_H);

        var ui = go.AddComponent<UIChat>();
        ui.rootRt_ = rt;
        ui.BuildUI(rt);
        return ui;
    }

    void Start() {
        // 订阅聊天通知(网络层最小化: 只做解析 + 转发 Manager)
        NetworkManager.Instance.Dispatcher.Subscribe(MsgId.CHAT_NOTIFY, OnChatNotify);
        // Manager 数据变化时刷新 UI
        ChatManager.Instance.OnChatChanged += RefreshMessages;
        // 首次刷新
        RefreshMessages();
    }

    void OnDestroy() {
        if (NetworkManager.Instance != null)
            NetworkManager.Instance.Dispatcher.Unsubscribe(MsgId.CHAT_NOTIFY);
        ChatManager.Instance.OnChatChanged -= RefreshMessages;
    }

    void Update() {
        // Room/Team 仅在房间内可用
        RefreshChannelAvailability();
    }

    // ---- 网络层: 收到 ChatNotify → 存入 ChatManager ----
    private void OnChatNotify(byte[] bytes) {
        var m = ChatNotify.Parser.ParseFrom(bytes);
        ChatManager.Instance.AddMessage(new ChatManager.ChatMsg {
            channel = m.Channel, senderId = m.SenderId,
            senderName = m.SenderName, message = m.Message, timestamp = m.Timestamp
        });
    }

    // ---- UI 构建 ----
    private void BuildUI(RectTransform root) {
        var bg = root.gameObject.AddComponent<Image>();
        bg.color = new Color(0, 0, 0, 0.55f);

        // 标题栏(可折叠)
        var titleBar = new GameObject("TitleBar", typeof(RectTransform));
        titleBar.transform.SetParent(root, false);
        titleBar.AddComponent<CanvasRenderer>();
        var titleImg = titleBar.AddComponent<Image>();
        titleImg.color = new Color(0.15f, 0.25f, 0.45f, 0.95f);
        var titleRt = titleBar.GetComponent<RectTransform>();
        titleRt.anchorMin = new Vector2(0, 1f); titleRt.anchorMax = new Vector2(1f, 1f);
        titleRt.pivot = new Vector2(0f, 1f);
        titleRt.offsetMin = new Vector2(0, -24); titleRt.offsetMax = Vector2.zero;
        var titleBtn = titleBar.AddComponent<Button>();
        titleBtn.onClick.AddListener(ToggleCollapse);
        titleText_ = MakeText(titleBar.transform, "Title", "聊天  ▾", 14);
        titleText_.alignment = TextAnchor.MiddleCenter;
        StretchFill(titleText_.rectTransform);

        // 内容区(折叠时隐藏)
        contentArea_ = new GameObject("ContentArea", typeof(RectTransform));
        contentArea_.transform.SetParent(root, false);
        var caRt = contentArea_.GetComponent<RectTransform>();
        caRt.anchorMin = Vector2.zero; caRt.anchorMax = Vector2.one;
        caRt.offsetMin = Vector2.zero; caRt.offsetMax = new Vector2(0, -24);

        // 频道 Tab 行
        var channelRow = new GameObject("Channels", typeof(RectTransform));
        channelRow.transform.SetParent(contentArea_.transform, false);
        var crt = channelRow.GetComponent<RectTransform>();
        crt.anchorMin = new Vector2(0, 1f); crt.anchorMax = new Vector2(1f, 1f);
        crt.pivot = new Vector2(0f, 1f);
        crt.offsetMin = new Vector2(4, -26); crt.offsetMax = new Vector2(-4, -4);
        var hlg = channelRow.AddComponent<HorizontalLayoutGroup>();
        hlg.childControlWidth = true; hlg.childControlHeight = false;
        hlg.childForceExpandWidth = true; hlg.childForceExpandHeight = false;
        hlg.spacing = 4;

        // Tab: 综合 / 世界 / 私聊 始终可用; 房间 / 队伍 仅房间内
        var tabs = new[] {
            (ChatManager.ChatTab.All, "综合"),
            (ChatManager.ChatTab.World, "世界"),
            (ChatManager.ChatTab.Private, "私聊"),
            (ChatManager.ChatTab.Room, "房间"),
            (ChatManager.ChatTab.Team, "队伍"),
        };
        foreach (var item in tabs) {
            ChatManager.ChatTab tab = item.Item1;
            string label = item.Item2;
            var btnGo = new GameObject("Tab_" + tab, typeof(RectTransform));
            btnGo.transform.SetParent(channelRow.transform, false);
            btnGo.AddComponent<CanvasRenderer>();
            var bimg = btnGo.AddComponent<Image>();
            bimg.color = new Color(0.2f, 0.35f, 0.55f, 0.8f);
            btnGo.GetComponent<RectTransform>().sizeDelta = new Vector2(0, 22);
            var bbtn = btnGo.AddComponent<Button>();
            var bc = bbtn.colors; bc.highlightedColor = new Color(0.4f, 0.6f, 0.9f); bbtn.colors = bc;
            ChatManager.ChatTab captured = tab;
            bbtn.onClick.AddListener(() => SwitchTab(captured));
            var lblGo = new GameObject("Label", typeof(RectTransform));
            lblGo.transform.SetParent(btnGo.transform, false);
            lblGo.AddComponent<CanvasRenderer>();
            var lbl = lblGo.AddComponent<Text>();
            lbl.font = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
            lbl.fontSize = 13; lbl.alignment = TextAnchor.MiddleCenter;
            lbl.color = Color.white; lbl.text = label; lbl.raycastTarget = false;
            StretchFill(lbl.rectTransform);
            channelBtnImgs_.Add(bimg);
            channelBtnGos_.Add(btnGo);
            channelTabs_.Add(tab);
        }
        RefreshTabHighlight();

        // 消息滚动区
        var scrollGo = new GameObject("Scroll", typeof(RectTransform));
        scrollGo.transform.SetParent(contentArea_.transform, false);
        scrollGo.AddComponent<CanvasRenderer>();
        var scrollImg = scrollGo.AddComponent<Image>();
        scrollImg.color = new Color(0, 0, 0, 0.3f);
        var srt = scrollGo.GetComponent<RectTransform>();
        srt.anchorMin = new Vector2(0, 0); srt.anchorMax = new Vector2(1f, 1f);
        srt.offsetMin = new Vector2(4, 36); srt.offsetMax = new Vector2(-4, -30);
        scrollRect_ = scrollGo.AddComponent<ScrollRect>();
        scrollRect_.horizontal = false; scrollRect_.vertical = true;

        var contentGo = new GameObject("MsgContent", typeof(RectTransform));
        contentGo.transform.SetParent(scrollGo.transform, false);
        var csf = contentGo.AddComponent<ContentSizeFitter>();
        csf.verticalFit = ContentSizeFitter.FitMode.PreferredSize;
        var vlg = contentGo.AddComponent<VerticalLayoutGroup>();
        vlg.childControlWidth = true; vlg.childControlHeight = true;
        vlg.childForceExpandWidth = true; vlg.childForceExpandHeight = false;
        var contentRt = contentGo.GetComponent<RectTransform>();
        contentRt.anchorMin = new Vector2(0, 1); contentRt.anchorMax = new Vector2(1, 1);
        contentRt.pivot = new Vector2(0.5f, 1f);
        contentRt.offsetMin = contentRt.offsetMax = Vector2.zero;
        scrollRect_.content = contentRt;
        scrollRect_.viewport = srt;

        msgText_ = MakeText(contentGo.transform, "MsgText", "");
        msgText_.supportRichText = true;
        msgText_.alignment = TextAnchor.LowerLeft;
        msgText_.horizontalOverflow = HorizontalWrapMode.Wrap;
        msgText_.verticalOverflow = VerticalWrapMode.Overflow;
        StretchFill(msgText_.rectTransform);
        msgText_.rectTransform.offsetMin = msgText_.rectTransform.offsetMax = new Vector2(4, 4);

        // 输入框 + 发送按钮
        inputField_ = CreateInput(contentArea_, "ChatInput", "输入消息回车发送...");
        var inRt = inputField_.GetComponent<RectTransform>();
        inRt.anchorMin = new Vector2(0, 0); inRt.anchorMax = new Vector2(1f, 0);
        inRt.pivot = new Vector2(0.5f, 0f);
        inRt.offsetMin = new Vector2(4, 4); inRt.offsetMax = new Vector2(-90, 30);
        inputField_.onEndEdit.AddListener((s) => {
            if (Input.GetKeyDown(KeyCode.Return) || Input.GetKeyDown(KeyCode.KeypadEnter)) OnSend();
        });

        var sendBtn = CreateButton(contentArea_.transform, "SendBtn", "发送", OnSend);
        var sbRt = sendBtn.GetComponent<RectTransform>();
        sbRt.anchorMin = new Vector2(1f, 0); sbRt.anchorMax = new Vector2(1f, 0);
        sbRt.pivot = new Vector2(1f, 0f);
        sbRt.offsetMin = new Vector2(-84, 4); sbRt.offsetMax = new Vector2(-4, 30);
    }

    // ---- 切换显示 Tab ----
    private void SwitchTab(ChatManager.ChatTab tab) {
        displayTab_ = tab;
        // 发送 Tab 跟随(综合 Tab 不能发送, 保持世界)
        if (tab != ChatManager.ChatTab.All) ChatManager.Instance.SendTab = tab;
        RefreshTabHighlight();
        RefreshMessages();
    }

    // ---- Tab 高亮 ----
    private void RefreshTabHighlight() {
        for (int i = 0; i < channelTabs_.Count && i < channelBtnImgs_.Count; i++) {
            channelBtnImgs_[i].color = (channelTabs_[i] == displayTab_)
                ? new Color(0.3f, 0.55f, 0.95f, 1f)
                : new Color(0.2f, 0.35f, 0.55f, 0.7f);
        }
    }

    // ---- Room/Team 仅在房间内可用 ----
    private void RefreshChannelAvailability() {
        bool inRoom = Session.InRoom;
        for (int i = 0; i < channelTabs_.Count; i++) {
            bool needRoom = channelTabs_[i] == ChatManager.ChatTab.Room || channelTabs_[i] == ChatManager.ChatTab.Team;
            bool active = !needRoom || inRoom;
            if (channelBtnGos_[i].activeSelf != active) channelBtnGos_[i].SetActive(active);
        }
        // 当前 Tab 不可用时回退到综合
        bool curNeedRoom = displayTab_ == ChatManager.ChatTab.Room || displayTab_ == ChatManager.ChatTab.Team;
        if (curNeedRoom && !inRoom) {
            displayTab_ = ChatManager.ChatTab.All;
            RefreshTabHighlight();
            RefreshMessages();
        }
    }

    // ---- 渲染消息(读 ChatManager 数据) ----
    private void RefreshMessages() {
        if (msgText_ == null) return;
        var msgs = ChatManager.Instance.GetMessages(displayTab_);
        var sb = new StringBuilder();
        foreach (var m in msgs) {
            string col = ChatManager.TabColorHex(m.channel);
            string prefix = m.channel == ChannelType.ChannelSystem ? "" : $"[{m.senderName}] ";
            sb.Append($"<color={col}>{prefix}{EscapeRich(m.message)}</color>\n");
        }
        msgText_.text = sb.ToString();
        if (scrollRect_ != null) scrollRect_.verticalNormalizedPosition = 0f;
    }

    // ---- 发送 ----
    private void OnSend() {
        if (inputField_ == null) return;
        string text = inputField_.text;
        if (string.IsNullOrEmpty(text)) return;

        ChannelType sendCh = ChatManager.Instance.SendChannel;
        if (sendCh == ChannelType.ChannelSystem) {
            ChatManager.Instance.AddSystemMessage("系统频道不能发送");
            inputField_.text = "";
            return;
        }
        if (sendCh == ChannelType.ChannelPrivate) {
            ulong target = ChatManager.Instance.PrivateTargetId;
            // 总是先尝试解析 "目标ID 消息" 格式(空格分隔)。
            // 这样无论是否已选定目标, 都能用 "新ID 消息" 切换私聊对象;
            // 若解析不出 ID(如纯消息), 则发给已选定的目标。
            int sp = text.IndexOf(' ');
            ulong parsedId = 0;
            bool hasIdPrefix = (sp > 0)
                && ulong.TryParse(text.Substring(0, sp), out parsedId);
            if (hasIdPrefix) {
                // "ID 消息" 格式: 切换/设定目标后发送
                target = parsedId;
                string msg = text.Substring(sp + 1);
                if (string.IsNullOrEmpty(msg)) {
                    ChatManager.Instance.AddSystemMessage("消息内容不能为空");
                    inputField_.text = "";
                    return;
                }
                if (target == Session.MyAccountId) {
                    ChatManager.Instance.AddSystemMessage("不能给自己发私聊");
                } else {
                    ChatManager.Instance.SetPrivateTarget(target, text.Substring(0, sp));
                    GameFacade.SendPrivateChat(target, msg);
                }
            } else if (target != 0 && target != Session.MyAccountId) {
                // 已有选定目标且输入不是 "ID 消息" 格式: 直接发给该目标
                GameFacade.SendPrivateChat(target, text);
            } else {
                ChatManager.Instance.AddSystemMessage("私聊格式: 目标ID 消息(空格分隔)");
            }
        } else {
            GameFacade.SendChat(sendCh, text);
        }
        inputField_.text = "";
    }

    // ---- 折叠/展开 ----
    private void ToggleCollapse() {
        collapsed_ = !collapsed_;
        if (contentArea_ != null) contentArea_.SetActive(!collapsed_);
        if (rootRt_ != null) rootRt_.sizeDelta = new Vector2(PANEL_W, collapsed_ ? COLLAPSED_H : PANEL_H);
        if (titleText_ != null) titleText_.text = collapsed_ ? "聊天  ▴" : "聊天  ▾";
    }

    // ---- 工具 ----
    private static string EscapeRich(string s) {
        if (string.IsNullOrEmpty(s)) return "";
        return s.Replace("<", "< ").Replace(">", " >");
    }

    private static void StretchFill(RectTransform rt) {
        rt.anchorMin = Vector2.zero; rt.anchorMax = Vector2.one;
        rt.offsetMin = rt.offsetMax = Vector2.zero;
    }

    private static Text MakeText(Transform parent, string name, string content, int size = 14) {
        var go = new GameObject(name, typeof(RectTransform));
        go.transform.SetParent(parent, false);
        go.AddComponent<CanvasRenderer>();
        var t = go.AddComponent<Text>();
        t.font = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
        t.fontSize = size; t.alignment = TextAnchor.LowerLeft;
        t.color = Color.white; t.raycastTarget = false; t.text = content;
        StretchFill(t.rectTransform);
        return t;
    }

    private static InputField CreateInput(GameObject parent, string name, string placeholder) {
        var go = new GameObject(name, typeof(RectTransform));
        go.transform.SetParent(parent.transform, false);
        go.AddComponent<CanvasRenderer>();
        var img = go.AddComponent<Image>(); img.color = new Color(1, 1, 1, 0.25f);
        var rt = go.GetComponent<RectTransform>();
        rt.sizeDelta = new Vector2(200, 26);
        var input = go.AddComponent<InputField>();
        var txtGo = new GameObject("Text"); txtGo.transform.SetParent(go.transform, false);
        txtGo.AddComponent<CanvasRenderer>();
        var txt = txtGo.AddComponent<Text>();
        txt.font = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
        txt.fontSize = 14; txt.color = Color.white; txt.alignment = TextAnchor.MiddleLeft; txt.raycastTarget = false;
        StretchFill(txt.rectTransform); txt.rectTransform.offsetMin = new Vector2(4, 1); txt.rectTransform.offsetMax = new Vector2(-4, -1);
        input.textComponent = txt;
        var phGo = new GameObject("Placeholder"); phGo.transform.SetParent(go.transform, false);
        phGo.AddComponent<CanvasRenderer>();
        var ph = phGo.AddComponent<Text>();
        ph.font = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
        ph.fontSize = 13; ph.fontStyle = FontStyle.Italic;
        ph.color = new Color(1, 1, 1, 0.4f); ph.text = placeholder; ph.alignment = TextAnchor.MiddleLeft;
        StretchFill(ph.rectTransform); ph.rectTransform.offsetMin = new Vector2(4, 1); ph.rectTransform.offsetMax = new Vector2(-4, -1);
        input.placeholder = ph;
        return input;
    }

    private static Button CreateButton(Transform parent, string name, string label, UnityEngine.Events.UnityAction onClick) {
        var go = new GameObject(name, typeof(RectTransform));
        go.transform.SetParent(parent, false);
        go.AddComponent<CanvasRenderer>();
        var img = go.AddComponent<Image>();
        img.color = new Color(0.2f, 0.35f, 0.55f, 0.8f);
        var rt = go.GetComponent<RectTransform>();
        rt.sizeDelta = new Vector2(80, 22);
        var btn = go.AddComponent<Button>();
        var c = btn.colors; c.highlightedColor = new Color(0.4f, 0.6f, 0.9f); btn.colors = c;
        var lblGo = new GameObject("Label", typeof(RectTransform));
        lblGo.transform.SetParent(go.transform, false);
        lblGo.AddComponent<CanvasRenderer>();
        var lbl = lblGo.AddComponent<Text>();
        lbl.font = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
        lbl.fontSize = 13; lbl.alignment = TextAnchor.MiddleCenter;
        lbl.color = Color.white; lbl.text = label; lbl.raycastTarget = false;
        StretchFill(lbl.rectTransform);
        btn.onClick.AddListener(onClick);
        return btn;
    }
}
}
