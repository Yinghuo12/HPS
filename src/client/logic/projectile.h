#ifndef PROJECTILE_H
#define PROJECTILE_H

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>

struct TrajectoryPoint {
    float x, y, t;
};

class Texture2D;
class SpriteBatch;

class Projectile {
public:
    Projectile();

    void Start(const std::vector<TrajectoryPoint>& points);
    void Update(float dt);
    void Draw(class SpriteRenderer& renderer);
    void Draw(SpriteBatch& batch, Texture2D* tex = nullptr);

    bool IsActive() const { return m_active; }
    bool IsFinished() const { return m_finished; }
    glm::vec2 GetCurrentPos() const { return m_currentPos; }
    glm::vec2 GetHitPos() const { return m_hitPos; }

    void Reset();

private:
    std::vector<TrajectoryPoint> m_points;
    float m_elapsedTime;
    bool m_active;
    bool m_finished;
    glm::vec2 m_currentPos;
    glm::vec2 m_hitPos;
    GLuint m_texture;
};

#endif
