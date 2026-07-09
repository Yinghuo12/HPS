#ifndef __DDT_PLAYER_H__
#define __DDT_PLAYER_H__

#include <string>
#include <memory>
#include <cstdint>
#include "sylar/http/ws_session.h"

namespace ddt {

class Player {
public:
    typedef std::shared_ptr<Player> ptr;

    Player(uint32_t id, const std::string& name, sylar::http::WSSession::ptr session);

    uint32_t getId() const { return m_id; }
    const std::string& getName() const { return m_name; }
    sylar::http::WSSession::ptr getSession() const { return m_session; }

    void sendMessage(const std::string& data);

    float getX() const { return m_x; }
    float getY() const { return m_y; }
    int   getHP() const { return m_hp; }
    int   getAngle() const { return m_angle; }
    int   getDirection() const { return m_direction; }

    void setPosition(float x, float y) { m_x = x; m_y = y; }
    void setHP(int hp) { m_hp = hp; }
    void setAngle(int angle) { m_angle = angle; }
    void setDirection(int dir) { m_direction = dir; }
    void addHP(int delta) { m_hp += delta; if (m_hp < 0) m_hp = 0; }

    uint64_t getAccountId() const { return m_accountId; }
    void setAccountId(uint64_t id) { m_accountId = id; }

private:
    uint32_t m_id;
    std::string m_name;
    sylar::http::WSSession::ptr m_session;

    float m_x = 0;
    float m_y = 0;
    int   m_hp = 100;
    int   m_maxHp = 100;
    int   m_angle = 60;
    int   m_direction = 1;  // 0=left, 1=right
    uint64_t m_accountId = 0;
};

} // namespace ddt

#endif
