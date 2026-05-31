#include "network_client.h"
#include "ddt.pb.h"
#include "sylar/http/ws_connection.h"
#include "sylar/iomanager.h"
#include "sylar/log.h"
#include <iostream>
#include <sys/socket.h>
#include <algorithm>

namespace ddt {

static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

NetworkClient& NetworkClient::Instance() {
    static NetworkClient inst;
    return inst;
}

bool NetworkClient::connect(const std::string& url) {
    // 安全等待旧线程退出
    if (m_ioThread && m_ioThread->joinable()) {
        m_connected = false;
        {
            std::lock_guard<std::mutex> lock(m_connMutex);
            auto conn = m_connection;
            if (conn) {
                auto sock = conn->getSocket();
                if (sock) shutdown(sock->getSocket(), SHUT_RDWR);
            }
        }
        m_ioThread->join();
        m_ioThread.reset();
    }

    m_connectResult = false;
    m_connectDone = false;
    m_connected = false;
    {
        std::lock_guard<std::mutex> lock(m_connMutex);
        m_connection.reset();
    }

    try {
        m_ioThread.reset(new std::thread([this, url]() {
            sylar::IOManager iom(1, false, "ddt_net");

            auto result = sylar::http::WSConnection::Create(url, 0);
            auto conn = result.second;
            if (!conn) {
                SYLAR_LOG_ERROR(g_logger) << "WS connect failed: " << url;
                std::lock_guard<std::mutex> lock(m_connMutex);
                m_connectResult = false;
                m_connectDone = true;
                m_connectCv.notify_one();
                return;
            }

            {
                std::lock_guard<std::mutex> lock(m_connMutex);
                m_connection = conn;  // shared_ptr 安全共享
                m_connected = true;
                m_connectResult = true;
                m_connectDone = true;
                m_connectCv.notify_one();
            }

            SYLAR_LOG_INFO(g_logger) << "WS connected to " << url;

            // 接收循环
            while (m_connected) {
                auto msg = conn->recvMessage();
                if (!msg) {
                    SYLAR_LOG_WARN(g_logger) << "WS recv null, disconnected";
                    break;
                }

                auto gameMsg = std::unique_ptr<ddt::GameMessage>(new ddt::GameMessage());
                if (!gameMsg->ParseFromString(msg->getData())) {
                    SYLAR_LOG_WARN(g_logger) << "Failed to parse protobuf";
                    continue;
                }

                {
                    std::lock_guard<std::mutex> lock(m_queueMutex);
                    m_messageQueue.push(std::move(gameMsg));
                }

                if (m_callback) {
                    std::lock_guard<std::mutex> lock(m_queueMutex);
                    if (!m_messageQueue.empty()) {
                        m_callback(*m_messageQueue.back());
                    }
                }
            }

            {
                std::lock_guard<std::mutex> lock(m_connMutex);
                m_connection.reset();
                m_connected = false;
            }

            // 自动重连逻辑
            if (m_autoReconnect && !url.empty()) {
                std::cerr << "[Network] Connection lost, auto-reconnecting..." << std::endl;
                // 在独立线程中执行重连，避免阻塞 IO 线程的退出
                // 主循环 Update() 会检测 m_connected 并触发重连
            }
        }));
    } catch (const std::exception& e) {
        std::cerr << "Connect error: " << e.what() << std::endl;
        return false;
    }

    // Wait until the IO thread signals connection result (with timeout)
    std::unique_lock<std::mutex> lock(m_connMutex);
    m_connectCv.wait_for(lock, std::chrono::seconds(10),
                         [this]{ return m_connectDone; });
    return m_connectResult;
}

void NetworkClient::disconnect() {
    m_autoReconnect = false;
    m_connected = false;
    m_connectResult = false;
    m_connectDone = true;
    m_connectCv.notify_all();

    {
        std::lock_guard<std::mutex> lock(m_connMutex);
        if (m_connection) {
            auto sock = m_connection->getSocket();
            if (sock) {
                shutdown(sock->getSocket(), SHUT_RDWR);
            }
            m_connection.reset();
        }
    }

    if (m_ioThread && m_ioThread->joinable()) {
        m_ioThread->join();
    }
    m_ioThread.reset();
}

void NetworkClient::enableAutoReconnect(const std::string& url) {
    m_reconnectUrl = url;
    m_autoReconnect = true;
}

void NetworkClient::disableAutoReconnect() {
    m_autoReconnect = false;
}

bool NetworkClient::shouldAutoReconnect() const {
    return m_autoReconnect && !m_connected && !m_reconnectUrl.empty();
}

const std::string& NetworkClient::getReconnectUrl() const {
    return m_reconnectUrl;
}

// ---- 统一发送 ----
void NetworkClient::sendRaw(const std::string& data) {
    std::lock_guard<std::mutex> lock(m_connMutex);
    // 跨平台发送二进制消息：Mac / Linux 通用
#ifdef __APPLE__
    // Mac 客户端
    // Mac 版 client 通常将 m_wsClient 强转为 WSClient*
    if (auto* ws = static_cast<WSClient*>(m_wsClient)) {
        ws->sendBinary(data);
    }
#else
    // Linux 服务器 / Linux 客户端
    if (m_connection) {
        m_connection->sendMessage(data, sylar::http::WSFrameHead::BIN_FRAME);
    }
#endif
}

void NetworkClient::sendLogin(const std::string& name, const std::string& password) {
    ddt::GameMessage msg;
    auto* req = msg.mutable_login_request();
    req->set_name(name);
    if (!password.empty()) req->set_password(password);
    std::string data;
    msg.SerializeToString(&data);
    sendRaw(data);
}

void NetworkClient::sendRegister(const std::string& name, const std::string& password) {
    ddt::GameMessage msg;
    auto* req = msg.mutable_register_request();
    req->set_name(name);
    req->set_password(password);
    std::string data;
    msg.SerializeToString(&data);
    sendRaw(data);
}

void NetworkClient::sendChat(int channel, const std::string& message) {
    ddt::GameMessage msg;
    auto* req = msg.mutable_chat_request();
    req->set_channel((ddt::ChannelType)channel);
    req->set_message(message);
    std::string data;
    msg.SerializeToString(&data);
    sendRaw(data);
}

void NetworkClient::sendPrivateChat(uint32_t targetId, const std::string& message) {
    ddt::GameMessage msg;
    auto* req = msg.mutable_private_chat_request();
    req->set_target_id(targetId);
    req->set_message(message);
    std::string data;
    msg.SerializeToString(&data);
    sendRaw(data);
}

void NetworkClient::sendChatHistory(int channel, int count) {
    ddt::GameMessage msg;
    auto* req = msg.mutable_chat_history_request();
    req->set_channel((ddt::ChannelType)channel);
    req->set_count(count);
    std::string data;
    msg.SerializeToString(&data);
    sendRaw(data);
}

void NetworkClient::sendJoinRoom(uint32_t room_id, int team) {
    ddt::GameMessage msg;
    auto* req = msg.mutable_join_room_request();
    req->set_room_id(room_id);
    if (team >= 0) req->set_team((ddt::TeamSide)team);
    std::string data;
    msg.SerializeToString(&data);
    sendRaw(data);
}

void NetworkClient::sendSwitchTeam(int team) {
    ddt::GameMessage msg;
    auto* req = msg.mutable_switch_team_request();
    req->set_team((ddt::TeamSide)team);
    std::string data;
    msg.SerializeToString(&data);
    sendRaw(data);
}

void NetworkClient::sendRoomList() {
    ddt::GameMessage msg;
    msg.mutable_room_list_request();
    std::string data;
    msg.SerializeToString(&data);
    sendRaw(data);
}

void NetworkClient::sendCreateRoom(const std::string& room_name) {
    ddt::GameMessage msg;
    auto* req = msg.mutable_create_room_request();
    req->set_room_name(room_name);
    std::string data;
    msg.SerializeToString(&data);
    sendRaw(data);
}

void NetworkClient::sendReady(bool ready) {
    ddt::GameMessage msg;
    auto* req = msg.mutable_ready_request();
    req->set_ready(ready);
    std::string data;
    msg.SerializeToString(&data);
    sendRaw(data);
}

void NetworkClient::sendLeaveRoom() {
    ddt::GameMessage msg;
    msg.mutable_leave_room_request();
    std::string data;
    msg.SerializeToString(&data);
    sendRaw(data);
}

void NetworkClient::sendFriendList() {
    ddt::GameMessage msg;
    msg.mutable_friend_list_request();
    std::string data;
    msg.SerializeToString(&data);
    sendRaw(data);
}

void NetworkClient::sendFriendAdd(const std::string& target_name) {
    ddt::GameMessage msg;
    auto* req = msg.mutable_friend_add_request();
    req->set_target_name(target_name);
    std::string data;
    msg.SerializeToString(&data);
    sendRaw(data);
}

void NetworkClient::sendShoot(int angle, double force, bool is_fly) {
    ddt::GameMessage msg;
    auto* req = msg.mutable_shoot_request();
    req->set_angle(angle);
    req->set_force(force);
    req->set_is_fly(is_fly);
    std::string data;
    msg.SerializeToString(&data);
    sendRaw(data);
}

void NetworkClient::sendMove(float delta_x) {
    ddt::GameMessage msg;
    auto* req = msg.mutable_move_request();
    req->set_delta_x(delta_x);
    std::string data;
    msg.SerializeToString(&data);
    sendRaw(data);
}

void NetworkClient::sendPass() {
    ddt::GameMessage msg;
    msg.mutable_pass_request();
    std::string data;
    msg.SerializeToString(&data);
    sendRaw(data);
}

void NetworkClient::sendPing() {
    std::lock_guard<std::mutex> lock(m_connMutex);
    if (m_connection) {
        m_connection->ping();
    }
}

void NetworkClient::sendHeartbeat() {
    ddt::GameMessage msg;
    auto* hb = msg.mutable_heartbeat_request();
    (void)hb;
    std::string data;
    msg.SerializeToString(&data);
    sendRaw(data);
}

void NetworkClient::setCallback(MessageCallback cb) {
    m_callback = cb;
}

bool NetworkClient::hasMessages() {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    return !m_messageQueue.empty();
}

std::unique_ptr<ddt::GameMessage> NetworkClient::pollMessage() {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    if (m_messageQueue.empty()) return nullptr;
    auto msg = std::move(m_messageQueue.front());
    m_messageQueue.pop();
    return msg;
}

} // namespace ddt
