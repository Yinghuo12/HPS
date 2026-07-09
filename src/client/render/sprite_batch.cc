#include "sprite_batch.h"
#include <algorithm>

SpriteBatch::SpriteBatch(Shader& shader)
    : m_shader(shader), m_vao(0), m_vbo(0), m_inBatch(false) {
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, INITIAL_VBO_SIZE * sizeof(BatchVertex),
                 nullptr, GL_DYNAMIC_DRAW);

    // position
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(BatchVertex),
                          (void*)0);
    // texCoord
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(BatchVertex),
                          (void*)(2 * sizeof(GLfloat)));
    // color
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(BatchVertex),
                          (void*)(4 * sizeof(GLfloat)));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    m_vertices.reserve(INITIAL_VBO_SIZE);
}

SpriteBatch::~SpriteBatch() {
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
}

void SpriteBatch::Begin(const glm::mat4& projection) {
    m_projection = projection;
    m_entries.clear();
    m_inBatch = true;
}

void SpriteBatch::Draw(Texture2D& texture, glm::vec2 position,
                       glm::vec2 size, GLfloat rotate, glm::vec4 color) {
    if (!m_inBatch) return;
    m_entries.push_back({&texture, position, size, rotate, color});
}

void SpriteBatch::End() {
    if (!m_inBatch) return;
    m_inBatch = false;
    if (m_entries.empty()) return;

    buildVertices();
    flush();
}

void SpriteBatch::buildVertices() {
    m_vertices.clear();

    // Sort by texture ID for batching
    std::stable_sort(m_entries.begin(), m_entries.end(),
        [](const BatchEntry& a, const BatchEntry& b) {
            return a.texture->ID < b.texture->ID;
        });

    // Standard quad UVs
    static const glm::vec2 quadUV[6] = {
        glm::vec2(0.0f, 1.0f), glm::vec2(0.0f, 0.0f), glm::vec2(1.0f, 0.0f),
        glm::vec2(0.0f, 1.0f), glm::vec2(1.0f, 0.0f), glm::vec2(1.0f, 1.0f)
    };

    // Quad corners (before transform)
    static const glm::vec2 quadPos[6] = {
        glm::vec2(0.0f, 1.0f), glm::vec2(0.0f, 0.0f), glm::vec2(1.0f, 0.0f),
        glm::vec2(0.0f, 1.0f), glm::vec2(1.0f, 0.0f), glm::vec2(1.0f, 1.0f)
    };

    for (auto& e : m_entries) {
        // Build model matrix (same as SpriteRenderer)
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, glm::vec3(e.position, 0.0f));
        model = glm::translate(model, glm::vec3(0.5f * e.size.x, 0.5f * e.size.y, 0.0f));
        model = glm::rotate(model, e.rotate, glm::vec3(0.0f, 0.0f, 1.0f));
        model = glm::translate(model, glm::vec3(-0.5f * e.size.x, -0.5f * e.size.y, 0.0f));
        model = glm::scale(model, glm::vec3(e.size, 1.0f));

        for (int i = 0; i < 6; i++) {
            glm::vec4 transformed = model * glm::vec4(quadPos[i], 0.0f, 1.0f);
            BatchVertex v;
            v.position = glm::vec2(transformed.x, transformed.y);
            v.texCoord = quadUV[i];
            v.color = e.color;
            m_vertices.push_back(v);
        }
    }
}

void SpriteBatch::flush() {
    if (m_vertices.empty()) return;

    m_shader.Use();
    m_shader.SetMatrix4("projection", m_projection);

    glActiveTexture(GL_TEXTURE0);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    size_t neededSize = m_vertices.size() * sizeof(BatchVertex);
    // Reallocate if needed
    glBufferData(GL_ARRAY_BUFFER, neededSize, nullptr, GL_DYNAMIC_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, neededSize, m_vertices.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glBindVertexArray(m_vao);

    // Draw grouped by texture
    size_t vertIdx = 0;
    size_t entryIdx = 0;
    while (entryIdx < m_entries.size()) {
        GLuint texId = m_entries[entryIdx].texture->ID;
        glBindTexture(GL_TEXTURE_2D, texId);

        size_t startVert = vertIdx;
        while (entryIdx < m_entries.size() &&
               m_entries[entryIdx].texture->ID == texId) {
            vertIdx += 6;
            entryIdx++;
        }
        glDrawArrays(GL_TRIANGLES, startVert, vertIdx - startVert);
    }

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
}
