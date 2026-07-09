#ifndef WS_CLIENT_H
#define WS_CLIENT_H

#include <string>
#include <vector>
#include <cstdint>
#include <mutex>

namespace ddt {

class WSClient {
public:
    WSClient();
    ~WSClient();

    bool connect(const std::string& url);
    void disconnect();
    bool sendBinary(const std::vector<uint8_t>& data);
    bool sendBinary(const std::string& data);
    int recvBinary(std::vector<uint8_t>& outData, int timeoutMs = 100);
    bool isConnected() const { return m_connected; }
    bool sendPing();

private:
    int m_fd;
    bool m_connected;
    std::mutex m_frameMutex;
    std::string m_host;
    int m_port;
    std::string m_path;

    bool tcpConnect(const std::string& host, int port);
    bool wsHandshake(const std::string& host, const std::string& path);
    bool sendWSFrame(int opcode, const uint8_t* data, size_t len, bool mask);
    int recvWSFrame(std::vector<uint8_t>& outData);

    static std::string base64Encode(const std::string& input);
    static std::string sha1Digest(const std::string& input);
    static uint32_t swapEndian32(uint32_t v);
    static uint64_t swapEndian64(uint64_t v);
};

} // namespace ddt

#endif
