// MessageDispatcher.cs — msg_id → handler 注册表
//
// handler 签名 Action<byte[]>: 收到的 payload 字节, handler 内自行 protobuf 反序列化。
// 一个 msg_id 只能注册一个 handler(后注册覆盖);未注册的消息忽略并警告。
using System;
using System.Collections.Generic;
using UnityEngine;

namespace Ddt.Net {
public class MessageDispatcher {
    private readonly Dictionary<ushort, Action<byte[]>> handlers = new Dictionary<ushort, Action<byte[]>>();

    /// <summary>订阅某 msg_id 的消息。覆盖已有。</summary>
    public void Subscribe(ushort msgId, Action<byte[]> handler) {
        handlers[msgId] = handler;
    }

    /// <summary>取消订阅。</summary>
    public void Unsubscribe(ushort msgId) {
        handlers.Remove(msgId);
    }

    /// <summary>分发(在主线程调用)。</summary>
    public void Dispatch(ushort msgId, byte[] payload) {
        // --- 新增下面这行：如果是空包，用空数组代替 null，防止 Protobuf 报空指针异常 ---
        if (payload == null) {
            payload = new byte[0];
        }
        Action<byte[]> h;
        if (handlers.TryGetValue(msgId, out h)) {
            try { h?.Invoke(payload); }
            catch (Exception e) { Debug.LogError($"[Dispatch] msg_id={msgId} exception: {e}"); }
        } else {
            Debug.Log($"[Dispatch] no handler for msg_id={msgId} ({payload?.Length ?? 0} bytes)");
        }
    }
}
}
