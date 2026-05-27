#include "projectile.h"
#include "sprite_renderer.h"
#include "sprite_batch.h"
#include "resource_manager.h"
#include <algorithm>

Projectile::Projectile()
    : m_elapsedTime(0.0f)
    , m_active(false)
    , m_finished(false)
    , m_currentPos(0.0f, 0.0f)
    , m_hitPos(0.0f, 0.0f)
    , m_texture(0) {
}

void Projectile::Start(const std::vector<TrajectoryPoint>& points) {
    m_points = points;
    m_elapsedTime = 0.0f;
    m_active = true;
    m_finished = false;
    if (!points.empty()) {
        m_currentPos = glm::vec2(points[0].x, points[0].y);
    }
}

void Projectile::Update(float dt) {
    if (!m_active || m_points.empty()) return;

    m_elapsedTime += dt * 1.0f;  // 1x real-time

    // Binary search for the segment containing m_elapsedTime
    size_t idx = 0;
    for (size_t i = 0; i < m_points.size(); i++) {
        if (m_points[i].t >= m_elapsedTime) {
            idx = i;
            break;
        }
        idx = i;
    }

    if (idx >= m_points.size() - 1) {
        m_active = false;
        m_finished = true;
        m_currentPos = glm::vec2(m_points.back().x, m_points.back().y);
        m_hitPos = m_currentPos;
        return;
    }

    // Interpolate between idx and idx+1
    // idx is the first point with t >= elapsed, so interpolate between idx-1 and idx
    size_t i0 = (idx > 0) ? idx - 1 : 0;
    size_t i1 = idx;
    if (m_elapsedTime < m_points[i0].t) i0 = 0;

    auto& p0 = m_points[i0];
    auto& p1 = m_points[i1];
    float segLen = p1.t - p0.t;
    float frac = (segLen > 0.0f) ? (m_elapsedTime - p0.t) / segLen : 0.0f;
    frac = std::min(1.0f, std::max(0.0f, frac));

    m_currentPos.x = p0.x + (p1.x - p0.x) * frac;
    m_currentPos.y = p0.y + (p1.y - p0.y) * frac;
}

void Projectile::Draw(SpriteRenderer& renderer) {
    if (!m_active) return;

    GLubyte img[8 * 8 * 4];
    for (int i = 0; i < 8 * 8; i++) {
        img[i*4+0] = 255;
        img[i*4+1] = 200;
        img[i*4+2] = 0;
        img[i*4+3] = 255;
    }
    static bool texCreated = false;
    if (!texCreated) {
        ResourceManager::GetTexture("projectile").Generate(8, 8, img);
        texCreated = true;
    }

    renderer.DrawSprite(ResourceManager::GetTexture("projectile"),
                        m_currentPos, glm::vec2(12.0f, 12.0f),
                        0.0f, glm::vec3(1.0f));
}

void Projectile::Draw(SpriteBatch& batch, Texture2D* tex) {
    if (!m_active) return;

    Texture2D& t = tex ? *tex : ResourceManager::GetTexture("proj_p1");
    batch.Draw(t,
               m_currentPos, glm::vec2(24.0f, 24.0f),
               0.0f, glm::vec4(1.0f));
}

void Projectile::Reset() {
    m_points.clear();
    m_elapsedTime = 0.0f;
    m_active = false;
    m_finished = false;
    m_currentPos = glm::vec2(0.0f);
    m_hitPos = glm::vec2(0.0f);
}
