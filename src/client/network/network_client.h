#ifndef NETWORK_CLIENT_H
#define NETWORK_CLIENT_H

#include <string>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <memory>
#include <atomic>

// 前向声明避免在头文件中 include protobuf
namespace ddt { class GameMessage; }

namespace sylar { namespace http { class WSConnection; } }

namespace ddt {

class NetworkClient {
public:
    typedef std::function<void(const ddt::GameMessage&)> MessageCallback;

    static NetworkClient& Instance();

    bool connect(const std::string& url = "ws://127.0.0.1:8073/ddt/game");
    void disconnect();

    // 自动重连：设置服务端地址后，断线时自动以指数退避重连
    void enableAutoReconnect(const std::string& url);
    void disableAutoReconnect();
    bool shouldAutoReconnect() const;
    const std::string& getReconnectUrl() const;

    void sendLogin(const std::string& name, const std::string& password = "");
    void sendRegister(const std::string& name, const std::string& password);
    void sendJoinRoom(uint32_t room_id = 0, int team = 0);
    void sendSwitchTeam(int team);
    void sendRoomList();
    void sendCreateRoom(const std::string& room_name);
    void sendReady(bool ready);
    void sendLeaveRoom();
    void sendFriendList();
    void sendFriendAdd(const std::string& target_name);
    void sendShoot(int angle, double force, bool is_fly = false);
    void sendMove(float delta_x);
    void sendPass();  // [新增] 跳过回合
    void sendChat(int channel, const std::string& message);
    void sendPrivateChat(uint32_t targetId, const std::string& message);
    void sendChatHistory(int channel, int count = 50);
    void sendPing();
    void sendHeartbeat();

    void setCallback(MessageCallback cb);

    // 线程安全：从 GL 主线程调用
    bool hasMessages();
    std::unique_ptr<ddt::GameMessage> pollMessage();
    bool isConnected() const { return m_connected; }

private:
    NetworkClient() = default;
    void sendRaw(const std::string& data);

    std::mutex m_queueMutex;
    std::queue<std::unique_ptr<ddt::GameMessage>> m_messageQueue;

    std::mutex m_connMutex;
    std::shared_ptr<sylar::http::WSConnection> m_connection;  // 类型安全替代 void*
    std::unique_ptr<std::thread> m_ioThread;
    std::atomic<bool> m_connected{false};
    std::condition_variable m_connectCv;
    bool m_connectResult = false;
    bool m_connectDone = false;
    MessageCallback m_callback;

    // 自动重连
    std::atomic<bool> m_autoReconnect{false};
    std::string m_reconnectUrl;
    static constexpr int kMaxRetryDelaySec = 30;
};

} // namespace ddt

#endif
