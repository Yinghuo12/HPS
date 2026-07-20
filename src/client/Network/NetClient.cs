// NetClient.cs — TCP 长连接客户端(收发 + 粘包 + 主线程回调 + 自动重连)
//
// 线程模型(主线程永不阻塞):
//   - 接收: RecvLoop(子线程) 阻塞读 → 解帧 → mainThreadQueue(Action) → 主线程排空
//   - 发送: Send() 只 Enqueue 到 sendQueue_(主线程, O(1) 无锁); SendLoop(子线程)
//           排空队列做 stream_.Write。TCP 反压导致的 Write 阻塞只卡发送线程, 不卡主线程。
//
// 自动重连: RecvLoop 退出(断线)后, 若 AutoReconnect 且曾连接成功过, 按指数退避
// (1s/2s/4s/8s/15s 上限)自动重连。重连成功触发 OnReconnectSuccess(主线程),
// 由 NetworkManager 重发 LOGIN 重新校验。Close() 主动关闭不重连。
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Net.Sockets;
using System.Threading;
using UnityEngine;

namespace Ddt.Net {
public class NetClient {
    public event Action OnConnected;
    public event Action OnDisconnected;
    public event Action<ushort, byte[]> OnFrame;     // 收到完整帧(子线程上下文)
    public event Action OnReconnectStart;            // 重连开始(主线程)
    public event Action<int> OnReconnectAttempt;      // 第 N 次重连尝试(N 从 1)(主线程)
    public event Action OnReconnectSuccess;           // 重连成功(主线程)

    public bool Connected { get; private set; }
    /// <summary>是否启用自动重连(默认开)。Close() 会置 false。</summary>
    public bool AutoReconnect { get; set; } = true;

    private TcpClient tcp_;
    private NetworkStream stream_;
    private Thread recvThread_;
    private Thread sendThread_;               // 独立发送线程: 消费 sendQueue_, 主线程永不阻塞
    private volatile bool running_;
    private volatile bool userClosed_;        // 主动 Close, 不重连
    private readonly List<byte> recvBuf_ = new List<byte>(4096);

    // 发送队列(线程安全): 主线程 Send 只 Enqueue, 由 sendThread_ 排空 → 主线程不再因
    // TCP 反压在 stream_.Write 上阻塞(原 SendTimeout=2000 的硬阻塞是"画面冻结+按键无响应"
    // 的根因)。队列上限防积压: 超限丢弃最旧的非关键包(心跳/移动节流)。
    private readonly System.Collections.Concurrent.ConcurrentQueue<byte[]> sendQueue_ = new System.Collections.Concurrent.ConcurrentQueue<byte[]>();
    private const int MAX_SEND_QUEUE = 200;
    // 唤醒信号: sendThread_ 阻塞等待, 有数据或退出时 Set。
    private readonly AutoResetEvent sendSignal_ = new AutoResetEvent(false);

    // 当前连接目标(重连用)
    private string host_;
    private int port_;

    // 重连成功标志: NetworkManager.Update 轮询此标志, 为 true 则补发 token 登录。
    // 不依赖 mainThreadQueue(切桌面恢复时 ClearMainThreadQueue 会清空队列,
    // 导致 OnReconnectSuccess 回调丢失 → 不发 LOGIN → 连接无法恢复)。
    private volatile bool needRelogin_ = false;
    public bool ConsumeRelogin() {
        if(needRelogin_) { needRelogin_ = false; return true; }
        return false;
    }

    /// <summary>异步连接。完成后在主线程触发 OnConnected(经 mainThreadQueue)。</summary>
    public void Connect(string host, int port) {
        Debug.Log($"[NetClient] Connect host={host} port={port}");
        host_ = host; port_ = port;
        userClosed_ = false;
        running_ = true;
        BeginConnect();
    }

    private void BeginConnect() {
        ThreadPool.QueueUserWorkItem(_ => {
            try {
                Debug.Log($"[NetClient] connecting (background thread)...");
                tcp_ = new TcpClient();
                tcp_.Connect(host_, port_);
                SetupSocket(tcp_);   // 连接建立后再设 socket 选项(KeepAlive 需要已连接)
                stream_ = tcp_.GetStream();
                Connected = true;
                recvBuf_.Clear();
                Debug.Log($"[NetClient] connected, starting recv/send threads");
                StartSendThread();   // 启动发送线程(每次(重)连接都要)
                EnqueueMain(() => OnConnected?.Invoke());
                recvThread_ = new Thread(RecvLoop) { IsBackground = true };
                recvThread_.Start();
            } catch (Exception e) {
                EnqueueMain(() => {
                    Debug.LogError("[NetClient] connect fail: " + e.Message);
                    OnDisconnected?.Invoke();
                });
                running_ = false;
            }
        });
    }

    /// <summary>设置 TCP socket 选项(必须在 Connect 之后调用): NoDelay + KeepAlive。</summary>
    private static void SetupSocket(TcpClient tcp) {
        try {
            tcp.NoDelay = true;   // 禁用 Nagle, 操作零延迟
            tcp.Client.SetSocketOption(SocketOptionLevel.Socket, SocketOptionName.KeepAlive, true);
            // KeepAlive 探测参数: 10 秒空闲后开始, 每 3 秒探测, 3 次失败判定断开
            byte[] ka = new byte[12];
            BitConverter.GetBytes((uint)1).CopyTo(ka, 0);
            BitConverter.GetBytes((uint)10000).CopyTo(ka, 4);
            BitConverter.GetBytes((uint)3000).CopyTo(ka, 8);
            tcp.Client.IOControl(IOControlCode.KeepAliveValues, ka, null);
        } catch (Exception e) {
            Debug.LogWarning("[NetClient] socket option fail (non-fatal): " + e.Message);
        }
    }

    /// <summary>启动发送线程(每次(重)连接建立后调)。保证同一时刻只有一个发送线程。
    /// 旧 sendThread_(若有)会因 running_ 被新连接重置或 stream_ 变化而自然退出。</summary>
    private void StartSendThread() {
        sendSignal_.Set();   // 唤醒可能残留的旧发送线程(让它看到新 stream_ 继续排空)
        // 若旧线程还在(极少), 不重复创建: SendLoop 会排空当前队列
        if (sendThread_ == null || !sendThread_.IsAlive) {
            sendThread_ = new Thread(SendLoop) { IsBackground = true };
            sendThread_.Start();
        }
    }

    /// <summary>发送一帧(组帧后入发送队列)。可在任意线程, **永不阻塞调用方**。
    /// 主线程的 Send(心跳/移动 RPC)只做 Enqueue(无锁, O(1)), 实际 Write 由 sendThread_ 完成。
    /// 这样即使 TCP 反压让 Write 阻塞, 也只卡发送线程, 主线程(Update/HandleInput)不受影响。
    /// 队列超限(MAX_SEND_QUEUE)时丢弃最旧包, 防积压暴涨(断网时队列不会无限增长)。</summary>
    public void Send(ushort msgId, byte[] payload) {
        if (!Connected) return;
        byte[] pkt = FrameCodec.Encode(msgId, payload);
        // 队列满: 丢弃最旧的(心跳/聊天等非关键), 防积压
        if (sendQueue_.Count >= MAX_SEND_QUEUE) {
            byte[] discarded;
            while (sendQueue_.Count >= MAX_SEND_QUEUE - 20 && sendQueue_.TryDequeue(out discarded)) {}
        }
        sendQueue_.Enqueue(pkt);
        sendSignal_.Set();   // 唤醒发送线程
    }

    /// <summary>发送线程主体: 阻塞等待 sendSignal_ → 排空 sendQueue_ → stream_.Write。
    /// 阻塞只发生在此线程(非主线程)。连接断开/异常时静默丢弃剩余包(重连后会重发心跳/登录)。
    /// WriteTimeout=3s: 防止半开连接(如 App Nap 后 socket 半死)上 Write 无限阻塞,
    /// 卡死发送线程 → 队列积压无法恢复。超时后抛异常 → 走 catch 丢弃, 等 RecvLoop 触发重连。</summary>
    private void SendLoop() {
        while (running_) {
            // 无数据时阻塞等待, 避免空转(最多等 1s 醒来检查 running_)
            sendSignal_.WaitOne(1000);
            // 排空队列
            byte[] pkt;
            while (sendQueue_.TryDequeue(out pkt)) {
                if (!running_ || stream_ == null || tcp_ == null) break;
                try {
                    tcp_.SendTimeout = 3000;   // 发送超时(仅本线程): 半开连接不无限卡
                    stream_.Write(pkt, 0, pkt.Length);
                    stream_.Flush();
                } catch (Exception e) {
                    // 发送失败(连接断开/反压超时): 丢弃本包, 记日志。
                    // RecvLoop 会检测到断线并触发重连, 这里不再重复处理。
                    EnqueueMain(() => Debug.LogWarning("[NetClient] send fail (non-fatal): " + e.Message));
                    break;
                }
            }
        }
        // 退出: 清空残留队列
        byte[] dummy;
        while (sendQueue_.TryDequeue(out dummy)) {}
    }

    /// <summary>主动关闭(不触发重连)。</summary>
    public void Close() {
        Debug.Log("[NetClient] Close (user-initiated, no reconnect)");
        userClosed_ = true;
        AutoReconnect = false;
        running_ = false;
        Connected = false;
        sendSignal_.Set();   // 唤醒发送线程让它退出
        try { stream_?.Close(); } catch {}
        try { tcp_?.Close(); } catch {}
    }

    /// <summary>从后台恢复时检查并恢复连接(温和方案, 不主动断开好连接)。
    ///
    /// 1. 清空 mainThreadQueue(后台积压的过期回调)
    /// 2. 清空 recvBuf_(可能有半截包残留)
    /// 3. 连接活性检测: 只看 Connected 标志(由 RecvLoop 维护)。
    ///    不用 Poll 检测——Poll(0, SelectRead) 对刚收到数据的 socket 会误判为"可读",
    ///    导致好连接被当成半死关掉, 触发疯狂重连风暴。
    ///    RecvLoop 如果检测到对端关闭会自己设 Connected=false 并触发重连。
    /// </summary>
    public void CheckAndRecover() {
        // 清空积压(无论连接是否活着, 后台积压的回调都是过期的)
        ClearMainThreadQueue();
        lock(recvBuf_) { recvBuf_.Clear(); }

        // 只看 Connected 标志: RecvLoop 检测到断线会自己设 false + 触发重连。
        // 这里不主动断开连接——避免 Poll 误判导致重连风暴。
        if(!Connected) {
            Debug.Log("[NetClient] recover: connection disconnected, relying on reconnect loop");
        } else {
            Debug.Log("[NetClient] recover: connection alive, cleared backlog");
        }
    }

    // ---- 接收线程主体: 阻塞读 → 喂缓冲 → 尝试解帧 ----
    private void RecvLoop() {
        byte[] tmp = new byte[4096];
        while (running_) {
            int n;
            try {
                n = stream_.Read(tmp, 0, tmp.Length);
            } catch {
                break;   // 连接断开/异常
            }
            if (n <= 0) break;   // 对端关闭
            lock (recvBuf_) {
                // 仅添加实际接收到的 n 字节，绝不添加后面多余的 0 字节
                for (int i = 0; i < n; i++) {
                    recvBuf_.Add(tmp[i]);
                }
                while (recvBuf_.Count > 0) {
                    ushort msgId;
                    byte[] payload;
                    bool ok;
                    try {
                        ok = FrameCodec.TryReadFrame(recvBuf_, out msgId, out payload);
                    } catch (Exception e) {
                        EnqueueMain(() => Debug.LogError("[NetClient] frame decode: " + e.Message));
                        running_ = false;
                        break;
                    }
                    if (!ok) break;
                    ushort id = msgId;
                    byte[] pl = payload;
                    EnqueueMain(() => OnFrame?.Invoke(id, pl));
                }
            }
        }
        Connected = false;
        bool wasUserClosed = userClosed_;
        Debug.Log($"[NetClient] RecvLoop exit (wasUserClosed={wasUserClosed} AutoReconnect={AutoReconnect})");
        EnqueueMain(() => OnDisconnected?.Invoke());
        // 自动重连(非主动关闭 + 开关开 + 曾连过)
        if (!wasUserClosed && AutoReconnect && !string.IsNullOrEmpty(host_)) {
            StartReconnectLoop();
        }
    }

    // 重连环: 防止多个 RecvLoop 退出同时触发多个重连线程(重连风暴)。
    private int isReconnecting_ = 0;   // 0=空闲, 1=正在重连(Interlocked)

    // ---- 重连循环(指数退避) ----
    private void StartReconnectLoop() {
        // CAS 保证同一时刻只有一个重连线程
        if(System.Threading.Interlocked.CompareExchange(ref isReconnecting_, 1, 0) != 0) {
            Debug.Log("[NetClient] reconnect already in progress, skip");
            return;   // 已有重连线程在跑, 不重复启动
        }
        Debug.Log($"[NetClient] StartReconnectLoop (delays=1/2/4/8/15s exponential)");
        EnqueueMain(() => OnReconnectStart?.Invoke());
        Thread rc = new Thread(() => {
            int attempt = 0;
            int[] delays = { 1000, 2000, 4000, 8000, 15000 };   // 毫秒
            while (!userClosed_ && AutoReconnect && !Connected) {
                attempt++;
                int delay = delays[Math.Min(attempt - 1, delays.Length - 1)];
                Debug.Log($"[NetClient] reconnect attempt #{attempt} in {delay}ms");
                EnqueueMain(() => OnReconnectAttempt?.Invoke(attempt));
                Thread.Sleep(delay);
                if (userClosed_ || !AutoReconnect) break;
                try {
                    Debug.Log($"[NetClient] reconnect #{attempt}: connecting...");
                    tcp_ = new TcpClient();
                    tcp_.Connect(host_, port_);
                    SetupSocket(tcp_);   // 连接后再设选项
                    stream_ = tcp_.GetStream();
                    Connected = true;
                    recvBuf_.Clear();
                    running_ = true;
                    Debug.Log($"[NetClient] reconnect #{attempt} SUCCESS");
                    StartSendThread();   // 重启发送线程(排空重连前的积压 + 后续发送)
                    // 重启接收线程
                    recvThread_ = new Thread(RecvLoop) { IsBackground = true };
                    recvThread_.Start();
                    needRelogin_ = true;   // 设标志: Update 轮询补发 token 登录
                    EnqueueMain(() => OnReconnectSuccess?.Invoke());
                    isReconnecting_ = 0;   // 释放重连环
                    return;   // 成功, 退出重连循环
                } catch (Exception e) {
                    Debug.LogWarning($"[NetClient] reconnect #{attempt} fail: {e.Message}");
                    // 继续退避重试
                }
            }
            Debug.Log("[NetClient] reconnect loop ended (cancelled or exhausted)");
            isReconnecting_ = 0;   // 释放重连环(重连取消/失败)
        }) { IsBackground = true };
        rc.Start();
    }

    // ---- 主线程队列(NetworkManager.Update 排空) ----
    private static readonly ConcurrentQueue<Action> mainThreadQueue = new ConcurrentQueue<Action>();
    private const int MAX_QUEUE = 5000;   // 队列上限, 防止后台积压暴涨
    private static void EnqueueMain(Action a) {
        // 队列满时丢弃旧消息(心跳/聊天等非关键消息), 防止后台积压导致切回卡死
        if (mainThreadQueue.Count > MAX_QUEUE) {
            Action discarded;
            while (mainThreadQueue.Count > MAX_QUEUE - 100 && mainThreadQueue.TryDequeue(out discarded)) {}
        }
        mainThreadQueue.Enqueue(a);
    }

    /// <summary>由 NetworkManager 在主线程 Update 里调用,排空待执行回调。
    /// 每帧最多处理 MAX_PER_FRAME 条, 防止后台积压大量消息时一帧卡死。</summary>
    private const int MAX_PER_FRAME = 200;
    public static void DrainMainThread() {
        int processed = 0;
        Action a;
        while (processed < MAX_PER_FRAME && mainThreadQueue.TryDequeue(out a)) {
            processed++;
            try { a?.Invoke(); } catch (Exception e) { Debug.LogError("[NetClient] main-callback: " + e); }
        }
        if (mainThreadQueue.Count > 100) {
            Debug.Log($"[NetClient] main queue backlog: {mainThreadQueue.Count} (processing {processed}/frame)");
        }
    }

    /// <summary>清空主线程回调队列(从后台恢复时调用, 丢弃后台积压的过期消息)。</summary>
    public static void ClearMainThreadQueue() {
        Action a;
        int dropped = 0;
        while (mainThreadQueue.TryDequeue(out a)) dropped++;
        if (dropped > 0) Debug.Log($"[NetClient] cleared {dropped} backlog callbacks on resume");
    }
}
}
