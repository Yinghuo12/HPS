#include "ddt_player.h"

namespace ddt {

Player::Player(uint32_t id, const std::string& name, sylar::http::WSSession::ptr session)
    : m_id(id), m_name(name), m_session(session) {}

void Player::sendMessage(const std::string& data) {
    if (m_session) {
        m_session->sendMessage(data, sylar::http::WSFrameHead::BIN_FRAME);
    }
}

} // namespace ddt
