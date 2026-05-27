#ifndef NETWORK_CLIENT_H
#define NETWORK_CLIENT_H

#include <string>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <memory>

// 前向声明避免在头文件中 include protobuf
namespace ddt { class GameMessage; }

namespace ddt {

class NetworkClient {
public:
    typedef std::function<void(const ddt::GameMessage&)> MessageCallback;

    static NetworkClient& Instance();

    bool connect(const std::string& url = "ws://127.0.0.1:8073/ddt/game");
    void disconnect();

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
    void sendShoot(int angle, double force);
    void sendMove(float delta_x);
    void sendChat(int channel, const std::string& message);
    void sendPrivateChat(uint32_t targetId, const std::string& message);
    void sendChatHistory(int channel, int count = 50);
    void sendPing();

    void setCallback(MessageCallback cb);

    // 线程安全：从 GL 主线程调用
    bool hasMessages();
    std::unique_ptr<ddt::GameMessage> pollMessage();
    bool isConnected() const { return m_connected; }

private:
    NetworkClient() = default;
    void recvLoop();

    std::mutex m_queueMutex;
    std::queue<std::unique_ptr<ddt::GameMessage>> m_messageQueue;

    std::mutex m_sendMutex;
    void* m_connection;  // sylar::http::WSConnection::ptr
    std::unique_ptr<std::thread> m_ioThread;
    bool m_connected = false;
    std::condition_variable m_connectCv;
    bool m_connectResult = false;
    bool m_connectDone = false;
    MessageCallback m_callback;
};

} // namespace ddt

#endif
