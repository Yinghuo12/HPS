// NetworkManager.cs — 单例 MonoBehaviour: 网络层入口
//
// 职责:
//   - 持 NetClient + MessageDispatcher 全局单例(DontDestroyOnLoad)
//   - Update() 排空主线程回调队列(NetClient 子线程 → 主线程 marshal)
//   - 把 NetClient 收到的帧交给 dispatcher 分发
//   - 心跳定时器
//
// 用法:
//   在 Bootstrap 场景放一个空 GameObject, 挂 NetworkManager。
//   业务代码通过 NetworkManager.Instance 发消息/订阅。
using System.Collections.Generic;
using UnityEngine;
using Google.Protobuf;

namespace Ddt.Net {
public class NetworkManager : MonoBehaviour {
    public static NetworkManager Instance { get; private set; }

    public NetClient Client { get; private set; }
    public MessageDispatcher Dispatcher { get; private set; }

    [Header("心跳(秒, 0=关)")]
    public float heartbeatInterval = 10f;

    private float heartbeatTimer_ = 0f;
    private bool loggedIn_ = false;   // 收到 LOGIN_RESP 成功后置 true

    public bool IsLoggedIn => loggedIn_;

    // ---- 重连状态(供 UI 显示) ----
    public enum ReconnectState { None, Reconnecting, Reconnected, Failed }
    public ReconnectState ConnState { get; private set; } = ReconnectState.None;
    public int ReconnectAttempt { get; private set; }   // 当前重连次数

    // ---- 场景切换网络包缓存队列 ----
    private bool isTransitioning_ = false;
    private readonly List<System.Tuple<ushort, byte[]>> queuedPackets_ = new List<System.Tuple<ushort, byte[]>>();

    // 开始转场：拦截并缓存所有网络包
    public void StartTransition() {
        isTransitioning_ = true;
        lock (queuedPackets_) {
            queuedPackets_.Clear();
        }
    }

    // 结束转场：排空并顺序重放所有缓存的包
    public void EndTransition() {
        isTransitioning_ = false;
        lock (queuedPackets_) {
            foreach (var p in queuedPackets_) {
                Dispatcher.Dispatch(p.Item1, p.Item2);
            }
            queuedPackets_.Clear();
        }
    }

    void Awake() {
        if (Instance != null && Instance != this) { Destroy(gameObject); return; }
        Instance = this;
        DontDestroyOnLoad(gameObject);
        Debug.Log("[Net] NetworkManager.Awake (singleton set, DontDestroyOnLoad)");
        // 允许后台运行: 窗口最小化/失焦时主循环继续跑, 心跳(跑在 Update 里)不停发,
        // 避免被服务端心跳超时(45s)踢掉 → 最小化后掉线无响应。
        Application.runInBackground = true;
        // 双保险: 禁止屏幕休眠(macOS 上 sleep 会让进程进入低功耗, 可能触发 App Nap 挂起)。
        // 配合 runInBackground, 尽可能阻止最小化时系统挂起进程。
        Screen.sleepTimeout = SleepTimeout.NeverSleep;
        Client = new NetClient();
        Dispatcher = new MessageDispatcher();
        Client.OnConnected += OnConnected;
        Client.OnDisconnected += OnDisconnected;
        Client.OnFrame += (msgId, payload) => {
            if (isTransitioning_) {
                lock (queuedPackets_) {
                    queuedPackets_.Add(System.Tuple.Create(msgId, payload));
                }
            } else {
                Dispatcher.Dispatch(msgId, payload);
            }
        };
        Client.OnReconnectStart += OnReconnectStart;
        Client.OnReconnectAttempt += OnReconnectAttempt;
        Client.OnReconnectSuccess += OnReconnectSuccess;
        // 默认订阅心跳响应(保持登录态)
        Dispatcher.Subscribe(MsgId.HEARTBEAT_RESP, _ => { /* 静默 */ });
    }

    private bool wasPaused_ = false;

    void Update() {
        // 检测从后台恢复(OnApplicationPause 在某些平台不可靠, 这里用时间差兜底)
        // 排空子线程投递的回调(连接成功/断开/帧到达都在这里 marshal)
        NetClient.DrainMainThread();

        // 重连补发登录: 切桌面恢复时 ClearMainThreadQueue 会清掉 OnReconnectSuccess 回调,
        // 导致重连成功后不发 token 登录。这里用标志位轮询兜底——即使回调丢了,
        // Update 下一帧也会检测到 needRelogin 并补发。
        if (Client != null && Client.ConsumeRelogin()) {
            if (!string.IsNullOrEmpty(Session.Token)) {
                Debug.Log("[Net] re-login on reconnect (flag-based)");
                Game.GameFacade.SendLogin(Session.Token);
            } else {
                Debug.LogError("[Net] reconnect flag set but no cached token");
                ConnState = ReconnectState.Failed;
            }
        }

        // 心跳
        if (loggedIn_ && heartbeatInterval > 0) {
            heartbeatTimer_ -= Time.deltaTime;
            if (heartbeatTimer_ <= 0f) {
                heartbeatTimer_ = heartbeatInterval;
                SendHeartbeat();
            }
        }
    }

    void OnApplicationPause(bool paused) {
        if (paused) {
            wasPaused_ = true;
        } else if (wasPaused_) {
            // 从后台恢复: 温和恢复方案(不主动断开好连接)。
            //
            // CheckAndRecover 做的事:
            //  1. 清空 mainThreadQueue + recvBuf_ + queuedPackets_(丢弃过期积压)
            //  2. 检测 TCP 连接是否半死:
            //     - 活着 → 继续用(零延迟恢复, 不影响战斗)
            //     - 半死 → 关连接触发重连 + needRelogin 补发登录
            //
            // 这比 ForceReconnect 更温和: 短时最小化(连接还活着)不会断线重连,
            // 只有服务端已踢人(连接死了)才重连。
            wasPaused_ = false;
            Debug.Log("[Net] resumed from background, checking connection");
            lock (queuedPackets_) { queuedPackets_.Clear(); }
            if (Client != null) {
                Client.CheckAndRecover();
            }
        }
    }

    void OnDestroy() {
        Client?.Close();
    }

    // ---- 连接事件(主线程) ----
    private void OnConnected() {
        Debug.Log("[Net] connected to gate");
    }
    private void OnDisconnected() {
        Debug.LogWarning("[Net] disconnected from gate");
        loggedIn_ = false;
        ConnState = ReconnectState.None;
    }

    // ---- 重连事件(主线程) ----
    private void OnReconnectStart() {
        ConnState = ReconnectState.Reconnecting;
        ReconnectAttempt = 0;
        Debug.LogWarning("[Net] reconnecting...");
    }
    private void OnReconnectAttempt(int n) {
        ReconnectAttempt = n;
    }
    private void OnReconnectSuccess() {
        ConnState = ReconnectState.Reconnected;
        Debug.Log("[Net] reconnected! re-login with cached token");
        // TCP 重连成功后, 用缓存的 token 重新 LOGIN(等价首次登录的首包)
        // 若 token 失效(超 24h), LOGIN_RESP 会回 AUTH_FAIL, UI 提示重新登录
        if (!string.IsNullOrEmpty(Session.Token)) {
            Game.GameFacade.SendLogin(Session.Token);
        } else {
            Debug.LogError("[Net] reconnect but no cached token, need manual re-login");
            ConnState = ReconnectState.Failed;
        }
    }

    /// <summary>token 失效(AUTH_FAIL)时调用: 停止重连, 让用户重新登录。</summary>
    public void StopReconnectAndRelogin() {
        Client.AutoReconnect = false;
        ConnState = ReconnectState.Failed;
    }

    /// <summary>标记已登录成功(收到 LOGIN_RESP 且 ok)。心跳从此开始。</summary>
    public void MarkLoggedIn() {
        loggedIn_ = true;
        heartbeatTimer_ = heartbeatInterval;
    }

    // ---- 发送便捷 ----
    public void Send<T>(ushort msgId, T msg) where T : Google.Protobuf.IMessage {
        Client.Send(msgId, msg.ToByteArray());
    }
    public void SendRaw(ushort msgId, byte[] payload) {
        Client.Send(msgId, payload);
    }
    public void SendHeartbeat() {
        var req = new HeartbeatReq { ClientTime = (ulong)System.DateTimeOffset.UtcNow.ToUnixTimeSeconds() };
        Send(MsgId.HEARTBEAT, req);
    }
}
}
