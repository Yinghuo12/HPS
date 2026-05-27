#include "camera.h"
#include <cmath>
#include <algorithm>

Camera::Camera(GLuint vpW, GLuint vpH, GLuint worldW, GLuint worldH)
    : m_camX(0), m_camY(0)
    , m_targetX(0), m_targetY(0)
    , m_panSpeed(0), m_panning(false)
    , m_vpW(vpW), m_vpH(vpH)
    , m_worldW(worldW), m_worldH(worldH) {
}

glm::vec2 Camera::worldToScreen(const glm::vec2& worldPos) const {
    return glm::vec2(worldPos.x - m_camX, worldPos.y - m_camY);
}

glm::vec2 Camera::screenToWorld(const glm::vec2& screenPos) const {
    return glm::vec2(screenPos.x + m_camX, screenPos.y + m_camY);
}

void Camera::setCenter(float worldX, float worldY) {
    m_camX = worldX - m_vpW / 2.0f;
    m_camY = worldY - m_vpH / 2.0f;
    m_panning = false;
    clamp();
}

void Camera::snapTo(float worldX, float worldY) {
    setCenter(worldX, worldY);
}

void Camera::panTo(float worldX, float worldY, float speed) {
    m_targetX = worldX - m_vpW / 2.0f;
    m_targetY = worldY - m_vpH / 2.0f;
    m_panSpeed = speed;
    m_panning = true;
}

void Camera::clamp() {
    float maxX = (float)(m_worldW - m_vpW);
    float maxY = (float)(m_worldH - m_vpH);
    if (maxX < 0) maxX = 0;
    if (maxY < 0) maxY = 0;
    m_camX = std::max(0.0f, std::min(m_camX, maxX));
    m_camY = std::max(0.0f, std::min(m_camY, maxY));
}

void Camera::update(float dt) {
    if (!m_panning) return;

    float dx = m_targetX - m_camX;
    float dy = m_targetY - m_camY;
    float dist = std::sqrt(dx * dx + dy * dy);

    if (dist < 2.0f) {
        m_camX = m_targetX;
        m_camY = m_targetY;
        m_panning = false;
        clamp();
        return;
    }

    float step = m_panSpeed * dt;
    if (step >= dist) {
        m_camX = m_targetX;
        m_camY = m_targetY;
        m_panning = false;
    } else {
        m_camX += dx / dist * step;
        m_camY += dy / dist * step;
    }
    clamp();
}
