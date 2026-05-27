#ifndef SPRITE_BATCH_H
#define SPRITE_BATCH_H

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>
#include "texture.h"
#include "shader.h"

struct BatchVertex {
    glm::vec2 position;
    glm::vec2 texCoord;
    glm::vec4 color;
};

struct BatchEntry {
    Texture2D* texture;
    glm::vec2 position;
    glm::vec2 size;
    GLfloat rotate;
    glm::vec4 color;
};

class SpriteBatch {
public:
    SpriteBatch(Shader& shader);
    ~SpriteBatch();

    void Begin(const glm::mat4& projection);
    void Draw(Texture2D& texture, glm::vec2 position,
              glm::vec2 size = glm::vec2(10.0f, 10.0f),
              GLfloat rotate = 0.0f,
              glm::vec4 color = glm::vec4(1.0f));
    void End();

private:
    void flush();
    void buildVertices();

    Shader& m_shader;
    GLuint m_vao, m_vbo;
    bool m_inBatch;
    glm::mat4 m_projection;
    std::vector<BatchEntry> m_entries;
    std::vector<BatchVertex> m_vertices;

    static const size_t INITIAL_VBO_SIZE = 4096;
};

#endif
