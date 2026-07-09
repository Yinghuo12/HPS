#ifndef PARTICLE_SYSTEM_H
#define PARTICLE_SYSTEM_H

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>
#include "texture.h"

struct Particle {
    glm::vec2 position;
    glm::vec2 velocity;
    glm::vec4 color;
    float life;
    float maxLife;
};

class SpriteBatch;

class ParticleEmitter {
public:
    ParticleEmitter(size_t maxParticles = 200);

    void emit(glm::vec2 pos, glm::vec2 vel,
              glm::vec4 color, float life);
    void update(float dt);
    void draw(SpriteBatch& batch, Texture2D& texture);

private:
    std::vector<Particle> m_particles;
    size_t m_poolIndex;
};

#endif
