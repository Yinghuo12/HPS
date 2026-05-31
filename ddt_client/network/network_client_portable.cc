#include "network_client_portable.h"
#include "ws_client.h"
#include "ddt.pb.h"
#include <iostream>

namespace ddt {

NetworkClient& NetworkClient::Instance() {
    static NetworkClient inst;
    return inst;
}

bool NetworkClient::connect(const std::string& url) {
    try {
        auto* ws = new WSClient();
        if (!ws->connect(url)) {
            std::cerr << "WS connect failed: " << url << std::endl;
            delete ws;
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(m_sendMutex);
            m_wsClient = ws;
            m_connected = true;
        }

        std::cout << "WS connected to " << url << std::endl;

        m_ioThread.reset(new std::thread([this, ws]() {
            recvLoop();
        }));

        return true;
    } catch (const std::exception& e) {
        std::cerr << "Connect error: " << e.what() << std::endl;
        return false;
    }
}

void NetworkClient::disconnect() {
    m_connected = false;
    if (m_ioThread && m_ioThread->joinable()) {
        m_ioThread->join();
    }
    {
        std::lock_guard<std::mutex> lock(m_sendMutex);
        if (m_wsClient) {
            auto* ws = static_cast<WSClient*>(m_wsClient);
            ws->disconnect();
            delete ws;
            m_wsClient = nullptr;
        }
    }
}

void NetworkClient::recvLoop() {
    auto* ws = static_cast<WSClient*>(m_wsClient);
    std::vector<uint8_t> buf;

    while (m_connected) {
        int ret = ws->recvBinary(buf, 100);
        if (ret < 0) {
            std::cerr << "WS disconnected" << std::endl;
            break;
        }
        if (ret == 0 || buf.empty()) continue;

        auto gameMsg = std::unique_ptr<ddt::GameMessage>(new ddt::GameMessage());
        if (!gameMsg->ParseFromArray(buf.data(), buf.size())) {
            std::cerr << "Failed to parse protobuf" << std::endl;
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
}

void NetworkClient::sendLogin(const std::string& name, const std::string& password) {
    ddt::GameMessage msg;
    auto* req = msg.mutable_login_request();
    req->set_name(name);
    if (!password.empty()) req->set_password(password);
    std::string data;
    msg.SerializeToString(&data);
    std::lock_guard<std::mutex> lock(m_sendMutex);
    if (auto* ws = static_cast<WSClient*>(m_wsClient)) {
        ws->sendBinary(data);
    }
}

void NetworkClient::sendRegister(const std::string& name, const std::string& password) {
    ddt::GameMessage msg;
    auto* req = msg.mutable_register_request();
    req->set_name(name);
    req->set_password(password);
    std::string data;
    msg.SerializeToString(&data);
    std::lock_guard<std::mutex> lock(m_sendMutex);
    if (auto* ws = static_cast<WSClient*>(m_wsClient)) {
        ws->sendBinary(data);
    }
}

void NetworkClient::sendChat(int channel, const std::string& message) {
    ddt::GameMessage msg;
    auto* req = msg.mutable_chat_request();
    req->set_channel((ddt::ChannelType)channel);
    req->set_message(message);
    std::string data;
    msg.SerializeToString(&data);
    std::lock_guard<std::mutex> lock(m_sendMutex);
    if (auto* ws = static_cast<WSClient*>(m_wsClient)) {
        ws->sendBinary(data);
    }
}

void NetworkClient::sendPrivateChat(uint32_t targetId, const std::string& message) {
    ddt::GameMessage msg;
    auto* req = msg.mutable_private_chat_request();
    req->set_target_id(targetId);
    req->set_message(message);
    std::string data;
    msg.SerializeToString(&data);
    std::lock_guard<std::mutex> lock(m_sendMutex);
    if (auto* ws = static_cast<WSClient*>(m_wsClient)) {
        ws->sendBinary(data);
    }
}

void NetworkClient::sendChatHistory(int channel, int count) {
    ddt::GameMessage msg;
    auto* req = msg.mutable_chat_history_request();
    req->set_channel((ddt::ChannelType)channel);
    req->set_count(count);
    std::string data;
    msg.SerializeToString(&data);
    std::lock_guard<std::mutex> lock(m_sendMutex);
    if (auto* ws = static_cast<WSClient*>(m_wsClient)) {
        ws->sendBinary(data);
    }
}

void NetworkClient::sendJoinRoom(uint32_t room_id, int team) {
    ddt::GameMessage msg;
    auto* req = msg.mutable_join_room_request();
    req->set_room_id(room_id);
    if (team >= 0) req->set_team((ddt::TeamSide)team);
    std::string data;
    msg.SerializeToString(&data);
    std::lock_guard<std::mutex> lock(m_sendMutex);
    if (auto* ws = static_cast<WSClient*>(m_wsClient)) {
        ws->sendBinary(data);
    }
}

void NetworkClient::sendSwitchTeam(int team) {
    ddt::GameMessage msg;
    auto* req = msg.mutable_switch_team_request();
    req->set_team((ddt::TeamSide)team);
    std::string data;
    msg.SerializeToString(&data);
    std::lock_guard<std::mutex> lock(m_sendMutex);
    if (auto* ws = static_cast<WSClient*>(m_wsClient)) {
        ws->sendBinary(data);
    }
}

void NetworkClient::sendRoomList() {
    ddt::GameMessage msg;
    msg.mutable_room_list_request();
    std::string data;
    msg.SerializeToString(&data);
    std::lock_guard<std::mutex> lock(m_sendMutex);
    if (auto* ws = static_cast<WSClient*>(m_wsClient)) {
        ws->sendBinary(data);
    }
}

void NetworkClient::sendCreateRoom(const std::string& room_name) {
    ddt::GameMessage msg;
    auto* req = msg.mutable_create_room_request();
    req->set_room_name(room_name);
    std::string data;
    msg.SerializeToString(&data);
    std::lock_guard<std::mutex> lock(m_sendMutex);
    if (auto* ws = static_cast<WSClient*>(m_wsClient)) {
        ws->sendBinary(data);
    }
}

void NetworkClient::sendReady(bool ready) {
    ddt::GameMessage msg;
    auto* req = msg.mutable_ready_request();
    req->set_ready(ready);
    std::string data;
    msg.SerializeToString(&data);
    std::lock_guard<std::mutex> lock(m_sendMutex);
    if (auto* ws = static_cast<WSClient*>(m_wsClient)) {
        ws->sendBinary(data);
    }
}

void NetworkClient::sendLeaveRoom() {
    ddt::GameMessage msg;
    msg.mutable_leave_room_request();
    std::string data;
    msg.SerializeToString(&data);
    std::lock_guard<std::mutex> lock(m_sendMutex);
    if (auto* ws = static_cast<WSClient*>(m_wsClient)) {
        ws->sendBinary(data);
    }
}

void NetworkClient::sendFriendList() {
    ddt::GameMessage msg;
    msg.mutable_friend_list_request();
    std::string data;
    msg.SerializeToString(&data);
    std::lock_guard<std::mutex> lock(m_sendMutex);
    if (auto* ws = static_cast<WSClient*>(m_wsClient)) {
        ws->sendBinary(data);
    }
}

void NetworkClient::sendFriendAdd(const std::string& target_name) {
    ddt::GameMessage msg;
    auto* req = msg.mutable_friend_add_request();
    req->set_target_name(target_name);
    std::string data;
    msg.SerializeToString(&data);
    std::lock_guard<std::mutex> lock(m_sendMutex);
    if (auto* ws = static_cast<WSClient*>(m_wsClient)) {
        ws->sendBinary(data);
    }
}

void NetworkClient::sendShoot(int angle, double force, bool is_fly) {
    ddt::GameMessage msg;
    auto* req = msg.mutable_shoot_request();
    req->set_angle(angle);
    req->set_force(force);
    req->set_is_fly(is_fly);
    std::string data;
    msg.SerializeToString(&data);
    std::lock_guard<std::mutex> lock(m_sendMutex);
    if (auto* ws = static_cast<WSClient*>(m_wsClient)) {
        ws->sendBinary(data);
    }
}

void NetworkClient::sendMove(float delta_x) {
    ddt::GameMessage msg;
    auto* req = msg.mutable_move_request();
    req->set_delta_x(delta_x);
    std::string data;
    msg.SerializeToString(&data);
    std::lock_guard<std::mutex> lock(m_sendMutex);
    if (auto* ws = static_cast<WSClient*>(m_wsClient)) {
        ws->sendBinary(data);
    }
}

void NetworkClient::sendPass() {
    ddt::GameMessage msg;
    msg.mutable_pass_request();
    std::string data;
    msg.SerializeToString(&data);
    std::lock_guard<std::mutex> lock(m_sendMutex);
    if (auto* ws = static_cast<WSClient*>(m_wsClient)) {
        ws->sendBinary(data);
    }
}

void NetworkClient::sendPing() {
    std::lock_guard<std::mutex> lock(m_sendMutex);
    if (auto* ws = static_cast<WSClient*>(m_wsClient)) {
        ws->sendPing();
    }
}

void NetworkClient::sendHeartbeat() {
    ddt::GameMessage msg;
    auto* hb = msg.mutable_heartbeat_request();
    (void)hb;

    std::string data;
    msg.SerializeToString(&data);

    std::lock_guard<std::mutex> lock(m_sendMutex);
    if (auto* ws = static_cast<WSClient*>(m_wsClient)) {
        ws->sendBinary(data);
    }
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
