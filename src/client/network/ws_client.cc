#include "ws_client.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <sstream>
#include <algorithm>

#ifdef __APPLE__
#include <sys/select.h>
#endif

namespace ddt {

WSClient::WSClient() : m_fd(-1), m_connected(false) {
    srand((unsigned)time(nullptr));
}

WSClient::~WSClient() {
    disconnect();
}

bool WSClient::tcpConnect(const std::string& host, int port) {
    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    std::string portStr = std::to_string(port);
    int rc = getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result);
    if (rc != 0) {
        std::cerr << "DNS resolve failed for " << host << ": " << gai_strerror(rc) << std::endl;
        return false;
    }

    m_fd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (m_fd < 0) {
        freeaddrinfo(result);
        return false;
    }

    // recv timeout (15s for slow proxies like cpolar)
    struct timeval tv;
    tv.tv_sec = 15;
    tv.tv_usec = 0;
    setsockopt(m_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // non-blocking connect with 5s timeout
    int flags = fcntl(m_fd, F_GETFL, 0);
    fcntl(m_fd, F_SETFL, flags | O_NONBLOCK);

    int ret = ::connect(m_fd, result->ai_addr, result->ai_addrlen);
    freeaddrinfo(result);

    if (ret != 0 && errno != EINPROGRESS) {
        std::cerr << "TCP connect failed to " << host << ":" << port << ": " << strerror(errno) << std::endl;
        close(m_fd);
        m_fd = -1;
        return false;
    }
    if (ret != 0) {
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(m_fd, &wfds);
        struct timeval timeout;
        timeout.tv_sec = 10;
        timeout.tv_usec = 0;
        ret = select(m_fd + 1, nullptr, &wfds, nullptr, &timeout);
        if (ret <= 0) {
            std::cerr << "TCP connect timeout to " << host << ":" << port << " (10s)" << std::endl;
            close(m_fd);
            m_fd = -1;
            return false;
        }
        int err = 0;
        socklen_t len = sizeof(err);
        getsockopt(m_fd, SOL_SOCKET, SO_ERROR, &err, &len);
        if (err != 0) {
            std::cerr << "TCP connect error to " << host << ":" << port << ": " << strerror(err) << std::endl;
            close(m_fd);
            m_fd = -1;
            return false;
        }
    }

    // restore blocking mode
    fcntl(m_fd, F_SETFL, flags);
    return true;
}

std::string WSClient::base64Encode(const std::string& input) {
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, valb = -6;
    for (unsigned char c : input) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(table[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back(table[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

std::string WSClient::sha1Digest(const std::string& input) {
    unsigned char digest[20];
    // Inline SHA-1 implementation
    uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE;
    uint32_t h3 = 0x10325476, h4 = 0xC3D2E1F0;

    std::vector<uint8_t> msg(input.begin(), input.end());
    uint64_t bitLen = msg.size() * 8;
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) msg.push_back(0);
    for (int i = 7; i >= 0; i--) msg.push_back((bitLen >> (i * 8)) & 0xFF);

    for (size_t offset = 0; offset < msg.size(); offset += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++) {
            w[i] = (msg[offset + i*4] << 24) | (msg[offset + i*4+1] << 16) |
                    (msg[offset + i*4+2] << 8) | msg[offset + i*4+3];
        }
        for (int i = 16; i < 80; i++) {
            uint32_t tmp = w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16];
            w[i] = (tmp << 1) | (tmp >> 31);
        }
        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20)      { f = (b & c) | ((~b) & d); k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d;             k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else              { f = b ^ c ^ d;             k = 0xCA62C1D6; }
            uint32_t temp = ((a << 5) | (a >> 27)) + f + e + k + w[i];
            e = d; d = c; c = (b << 30) | (b >> 2); b = a; a = temp;
        }
        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }

    for (int i = 0; i < 4; i++) {
        digest[i]    = (h0 >> (24 - i*8)) & 0xFF;
        digest[i+4]  = (h1 >> (24 - i*8)) & 0xFF;
        digest[i+8]  = (h2 >> (24 - i*8)) & 0xFF;
        digest[i+12] = (h3 >> (24 - i*8)) & 0xFF;
        digest[i+16] = (h4 >> (24 - i*8)) & 0xFF;
    }

    return std::string(reinterpret_cast<char*>(digest), 20);
}

uint32_t WSClient::swapEndian32(uint32_t v) {
    return ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) |
           ((v >> 8) & 0xFF00) | ((v >> 24) & 0xFF);
}

uint64_t WSClient::swapEndian64(uint64_t v) {
    return ((v & 0xFFULL) << 56) | ((v & 0xFF00ULL) << 40) |
           ((v & 0xFF0000ULL) << 24) | ((v & 0xFF000000ULL) << 8) |
           ((v >> 8) & 0xFF000000ULL) | ((v >> 24) & 0xFF0000ULL) |
           ((v >> 40) & 0xFF00ULL) | ((v >> 56) & 0xFFULL);
}

bool WSClient::wsHandshake(const std::string& host, const std::string& path) {
    std::string key = base64Encode("DDTClient12345678");
    std::string req =
        "GET " + path + " HTTP/1.1\r\n"
        "Host: " + host + "\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: " + key + "\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";

    if (send(m_fd, req.c_str(), req.size(), 0) != (ssize_t)req.size()) {
        std::cerr << "WS handshake send failed to " << host << std::endl;
        return false;
    }

    char buf[4096];
    ssize_t n = recv(m_fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) {
        std::cerr << "WS handshake recv failed from " << host << ": n=" << n << " errno=" << strerror(errno) << std::endl;
        return false;
    }
    buf[n] = '\0';

    if (strstr(buf, "101") == nullptr) {
        std::cerr << "WS handshake rejected by " << host << ": " << std::string(buf, std::min((ssize_t)200, n)) << std::endl;
        return false;
    }

    return true;
}

bool WSClient::sendWSFrame(int opcode, const uint8_t* data, size_t len, bool mask) {
    std::lock_guard<std::mutex> lock(m_frameMutex);
    std::vector<uint8_t> frame;
    frame.push_back(0x80 | opcode); // FIN + opcode

    if (len < 126) {
        frame.push_back((mask ? 0x80 : 0) | (uint8_t)len);
    } else if (len < 65536) {
        frame.push_back((mask ? 0x80 : 0) | 126);
        frame.push_back((len >> 8) & 0xFF);
        frame.push_back(len & 0xFF);
    } else {
        frame.push_back((mask ? 0x80 : 0) | 127);
        uint64_t len64 = len;
        for (int i = 7; i >= 0; i--)
            frame.push_back((len64 >> (i * 8)) & 0xFF);
    }

    uint8_t maskKey[4] = {
        (uint8_t)(rand() & 0xFF), (uint8_t)(rand() & 0xFF),
        (uint8_t)(rand() & 0xFF), (uint8_t)(rand() & 0xFF)
    };
    if (mask) {
        frame.insert(frame.end(), maskKey, maskKey + 4);
        for (size_t i = 0; i < len; i++)
            frame.push_back(data[i] ^ maskKey[i % 4]);
    } else {
        frame.insert(frame.end(), data, data + len);
    }

    size_t sent = 0;
    while (sent < frame.size()) {
        ssize_t n = send(m_fd, (const char*)(frame.data() + sent),
                         frame.size() - sent, 0);
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

int WSClient::recvWSFrame(std::vector<uint8_t>& outData) {
    uint8_t header[2];
    ssize_t n = recv(m_fd, header, 2, 0);
    if (n <= 0) return -1;
    if (n < 2) return -1;

    int opcode = header[0] & 0x0F;
    bool masked = (header[1] & 0x80) != 0;
    uint64_t payloadLen = header[1] & 0x7F;

    if (payloadLen == 126) {
        uint8_t ext[2];
        if (recv(m_fd, ext, 2, MSG_WAITALL) != 2) return -1;
        payloadLen = (ext[0] << 8) | ext[1];
    } else if (payloadLen == 127) {
        uint8_t ext[8];
        if (recv(m_fd, ext, 8, MSG_WAITALL) != 8) return -1;
        payloadLen = 0;
        for (int i = 0; i < 8; i++)
            payloadLen = (payloadLen << 8) | ext[i];
    }

    uint8_t maskKey[4] = {};
    if (masked) {
        if (recv(m_fd, maskKey, 4, MSG_WAITALL) != 4) return -1;
    }

    outData.resize(payloadLen);
    if (payloadLen > 0) {
        size_t received = 0;
        while (received < payloadLen) {
            ssize_t r = recv(m_fd, (char*)(outData.data() + received),
                            payloadLen - received, 0);
            if (r <= 0) return -1;
            received += r;
        }
        if (masked) {
            for (size_t i = 0; i < payloadLen; i++)
                outData[i] ^= maskKey[i % 4];
        }
    }

    if (opcode == 0x8) { // close
        sendWSFrame(0x8, nullptr, 0, true);
        m_connected = false;
        return 0;
    }
    if (opcode == 0x9) { // ping → pong
        sendWSFrame(0xA, outData.data(), outData.size(), true);
        return 0xA;
    }

    return opcode;
}

bool WSClient::connect(const std::string& url) {
    // Strip common prefixes users might paste (tcp://, ws://)
    std::string cleanUrl = url;
    for (auto& prefix : {"tcp://", "ws://"}) {
        if (cleanUrl.substr(0, strlen(prefix)) == prefix) {
            cleanUrl = cleanUrl.substr(strlen(prefix));
            break;
        }
    }
    // Now build proper ws:// URL
    cleanUrl = "ws://" + cleanUrl;
    if (cleanUrl.find("/ddt/game") == std::string::npos) {
        cleanUrl += "/ddt/game";
    }

    // Parse ws://host:port/path
    std::string rest = cleanUrl.substr(5);

    size_t pathPos = rest.find('/');
    if (pathPos != std::string::npos) {
        m_path = rest.substr(pathPos);
        rest = rest.substr(0, pathPos);
    } else {
        m_path = "/";
    }

    size_t colonPos = rest.rfind(':');
    if (colonPos != std::string::npos) {
        m_host = rest.substr(0, colonPos);
        m_port = std::stoi(rest.substr(colonPos + 1));
    } else {
        m_host = rest;
        m_port = 80;
    }

    if (!tcpConnect(m_host, m_port)) return false;
    if (!wsHandshake(m_host + ":" + std::to_string(m_port), m_path)) {
        close(m_fd);
        m_fd = -1;
        return false;
    }

    m_connected = true;
    return true;
}

void WSClient::disconnect() {
    if (m_connected && m_fd >= 0) {
        sendWSFrame(0x8, nullptr, 0, true);
        close(m_fd);
    }
    m_fd = -1;
    m_connected = false;
}

bool WSClient::sendBinary(const std::vector<uint8_t>& data) {
    if (!m_connected) return false;
    return sendWSFrame(0x2, data.data(), data.size(), true);
}

bool WSClient::sendBinary(const std::string& data) {
    if (!m_connected) return false;
    return sendWSFrame(0x2, (const uint8_t*)data.data(), data.size(), true);
}

bool WSClient::sendPing() {
    if (!m_connected) return false;
    return sendWSFrame(0x9, nullptr, 0, true); // 0x9 表示 Ping 帧，需要掩码加密 [INDEX]
}

int WSClient::recvBinary(std::vector<uint8_t>& outData, int timeoutMs) {
    if (!m_connected || m_fd < 0) return -1;

    // Use select for timeout
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(m_fd, &fds);

    struct timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;

    int ret = select(m_fd + 1, &fds, nullptr, nullptr, &tv);
    if (ret <= 0) return 0; // timeout or error

    int opcode = recvWSFrame(outData);
    if (opcode == 0x2) return 1; // binary frame
    if (opcode == 0) return -1;  // closed
    return 0; // other frame, ignore
}

} // namespace ddt
