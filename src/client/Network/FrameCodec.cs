// FrameCodec.cs — 客户端↔网关 TCP 帧编解码
//
// 帧格式(网络序/大端, 与 C++ src/common/frame.cc 严格一致):
//   [ 4 字节 length ][ 2 字节 msg_id ][ protobuf payload ]
//   length = 2 + payload.Length   (不含自身 4 字节)
//
// 用法:
//   发送: byte[] pkt = FrameCodec.Encode(msgId, protobufBytes);
//   接收: 见 NetClient(粘包缓冲内联用 TryReadFrame)
using System;

namespace Ddt.Net {
public static class FrameCodec {
    /// <summary>组帧: 返回完整可发送的字节序列。</summary>
    public static byte[] Encode(ushort msgId, byte[] payload) {
        int payloadLen = payload?.Length ?? 0;
        uint length = (uint)(2 + payloadLen);   // msg_id(2) + payload
        byte[] out_ = new byte[4 + 2 + payloadLen];
        // 4 字节 length 大端
        out_[0] = (byte)((length >> 24) & 0xFF);
        out_[1] = (byte)((length >> 16) & 0xFF);
        out_[2] = (byte)((length >> 8)  & 0xFF);
        out_[3] = (byte)( length        & 0xFF);
        // 2 字节 msg_id 大端
        out_[4] = (byte)((msgId >> 8) & 0xFF);
        out_[5] = (byte)( msgId       & 0xFF);
        if (payloadLen > 0) Buffer.BlockCopy(payload, 0, out_, 6, payloadLen);
        return out_;
    }

    /// <summary>从缓冲区尝试解出一帧(粘包处理)。
    /// 返回 true 表示成功解出; buf 会移除已消费字节; out msgId/out payload 填充。
    /// buf 不足一帧时返回 false,等待更多数据。</summary>
    public static bool TryReadFrame(System.Collections.Generic.List<byte> buf,
                                    out ushort msgId, out byte[] payload) {
        msgId = 0;
        payload = null;
        // 至少要有 4 字节 length 头
        if (buf.Count < 4) return false;
        uint length = ((uint)buf[0] << 24) | ((uint)buf[1] << 16)
                    | ((uint)buf[2] << 8)  | (uint)buf[3];
        // length 至少含 2 字节 msg_id; 上限防恶意包
        if (length < 2 || length > 16 * 1024 * 1024) {
            throw new InvalidOperationException("invalid frame length: " + length);
        }
        // 整帧所需总字节 = 4(length 头) + length
        if (buf.Count < 4 + length) return false;   // 数据未到齐
        msgId = (ushort)(((ushort)buf[4] << 8) | (ushort)buf[5]);
        int payloadLen = (int)length - 2;
        if (payloadLen > 0) {
            payload = new byte[payloadLen];
            buf.CopyTo(4 + 2, payload, 0, payloadLen);
        }
        buf.RemoveRange(0, 4 + (int)length);   // 滑窗
        return true;
    }
}
}
