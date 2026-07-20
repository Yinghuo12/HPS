#ifndef __DDT_FRAME_H__
#define __DDT_FRAME_H__

#include <cstdint>
#include <string>

namespace ddt {

// 客户端 ↔ 网关 TCP 帧编解码
// 帧格式(网络序/大端):
//   [ 4B length ][ 2B msg_id ][ protobuf payload ]
//   length = 2 + payload.size()   (即不含自身 4 字节)
struct Frame {
    // 组帧: 返回完整可发送的字节串
    static std::string encode(uint16_t msg_id, const std::string& payload);

    // 解帧: body = length 之后的全部字节(长度应为 length)
    // 成功返回 true 并填充 out_msg_id / out_payload
    static bool decode(const std::string& body, uint16_t& out_msg_id, std::string& out_payload);
    static bool decode(const char* data, size_t len, uint16_t& out_msg_id, std::string& out_payload);
};

} // namespace ddt

#endif
