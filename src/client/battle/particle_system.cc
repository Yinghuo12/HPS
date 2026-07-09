#include "particle_system.h"
#include "sprite_batch.h"
#include <algorithm>

ParticleEmitter::ParticleEmitter(size_t maxParticles)
    : m_poolIndex(0) {
    m_particles.resize(maxParticles);
    for (auto& p : m_particles) {
        p.life = 0.0f;
        p.maxLife = 0.0f;
    }
}

void ParticleEmitter::emit(glm::vec2 pos, glm::vec2 vel,
                           glm::vec4 color, float life) {
    Particle& p = m_particles[m_poolIndex];
    p.position = pos;
    p.velocity = vel;
    p.color = color;
    p.life = life;
    p.maxLife = life;
    m_poolIndex = (m_poolIndex + 1) % m_particles.size();
}

void ParticleEmitter::update(float dt) {
    for (auto& p : m_particles) {
        if (p.life <= 0.0f) continue;
        p.position += p.velocity * dt;
        p.velocity.y += 50.0f * dt;
        p.life -= dt;
        if (p.life > 0.0f && p.maxLife > 0.0f) {
            p.color.a = (p.life / p.maxLife) * 0.7f;
        }
    }
}

void ParticleEmitter::draw(SpriteBatch& batch, Texture2D& texture) {
    for (auto& p : m_particles) {
        if (p.life <= 0.0f) continue;
        float ratio = p.life / p.maxLife;
        float sz = 8.0f * ratio;
        if (sz < 1.0f) sz = 1.0f;
        batch.Draw(texture, p.position, glm::vec2(sz, sz), 0.0f, p.color);
    }
}
