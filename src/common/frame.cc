#include "frame.h"

#include <cstring>

namespace ddt {

// 工具: 主机序 <-> 网络序(避免依赖htonl, 便于跨平台)
static void writeBE32(char* p, uint32_t v) {
    p[0] = (char)((v >> 24) & 0xFF);
    p[1] = (char)((v >> 16) & 0xFF);
    p[2] = (char)((v >> 8)  & 0xFF);
    p[3] = (char)( v        & 0xFF);
}
static uint32_t readBE32(const char* p) {
    return ((uint32_t)(uint8_t)p[0] << 24)
         | ((uint32_t)(uint8_t)p[1] << 16)
         | ((uint32_t)(uint8_t)p[2] << 8)
         |  (uint32_t)(uint8_t)p[3];
}
static void writeBE16(char* p, uint16_t v) {
    p[0] = (char)((v >> 8) & 0xFF);
    p[1] = (char)( v       & 0xFF);
}
static uint16_t readBE16(const char* p) {
    return ((uint16_t)(uint8_t)p[0] << 8) | (uint16_t)(uint8_t)p[1];
}

std::string Frame::encode(uint16_t msg_id, const std::string& payload) {
    // length = sizeof(msg_id) + sizeof(payload) = 2 + payload.size()
    uint32_t length = (uint32_t)(2 + payload.size());
    std::string out;
    out.resize(4 + 2 + payload.size());
    writeBE32(&out[0], length);
    writeBE16(&out[4], msg_id);
    if (!payload.empty()) {
        memcpy(&out[6], payload.data(), payload.size());
    }
    return out;
}

bool Frame::decode(const std::string& body, uint16_t& out_msg_id, std::string& out_payload) {
    return decode(body.data(), body.size(), out_msg_id, out_payload);
}

bool Frame::decode(const char* data, size_t len, uint16_t& out_msg_id, std::string& out_payload) {
    // body 至少含 2 字节 msg_id
    if (len < 2) return false;
    out_msg_id = readBE16(data);
    out_payload.assign(data + 2, len - 2);
    return true;
}

// 头部长度前缀的工具: 从 4 字节读出 length 字段值
// (供接收侧先读 4 字节后判断还需读多少)
uint32_t frameLengthFromHeader(const char* header4) {
    return readBE32(header4);
}

} // namespace ddt
