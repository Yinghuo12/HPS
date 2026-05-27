#include "terrain.h"
#include "resource_manager.h"
#include "shader.h"
#include <cmath>
#include <cstring>
#include <algorithm>

Terrain::Terrain(GLuint worldWidth, GLuint worldHeight)
    : m_width(worldWidth), m_height(worldHeight)
    , m_fbo(0), m_texture(0), m_vao(0), m_vbo(0)
    , m_initialized(false) {
    m_heightMap.resize(worldWidth, 1100.0f);
}

Terrain::~Terrain() {
    if (m_fbo) glDeleteFramebuffers(1, &m_fbo);
    if (m_texture) glDeleteTextures(1, &m_texture);
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
}

void Terrain::generateHeightMap() {
    for (GLuint x = 0; x < m_width; x++) {
        float baseH = 1100.0f;

        // Parabolic valley (bg_rainbow style)
        float dx = (float)x - m_width * 0.5f;
        float a = 2380.0f * 3.0f;
        float sag = a * a - dx * dx;
        if (sag > 0) {
            baseH = 2860.0f * 2.1f - std::sqrt(sag);
        }

        // Rolling hills
        baseH += 25.0f * std::sin(x * 0.008f) + 12.0f * std::sin(x * 0.023f);

        // Platform bumps for gameplay variety
        if (x > 400 && x < 600) baseH -= 40.0f;   // Left platform
        if (x > 1400 && x < 1600) baseH -= 60.0f;  // Center low ground
        if (x > 2400 && x < 2600) baseH -= 40.0f;  // Right platform

        m_heightMap[x] = std::max(600.0f, std::min(1250.0f, baseH));
    }
}

void Terrain::Init() {
    generateHeightMap();

    // Create FBO texture
    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Create FBO
    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture, 0);

    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Fill terrain pixels
    std::vector<GLubyte> pixels(m_width * m_height * 4, 0);
    for (GLuint x = 0; x < m_width; x++) {
        float h = m_heightMap[x];
        GLuint startY = static_cast<GLuint>(h);
        for (GLuint y = startY; y < m_height; y++) {
            GLuint idx = (y * m_width + x) * 4;
            pixels[idx + 0] = 139;
            pixels[idx + 1] = 119;
            pixels[idx + 2] = 101;
            pixels[idx + 3] = 255;
        }
    }

    // Grass top layer
    for (GLuint x = 0; x < m_width; x++) {
        GLuint startY = static_cast<GLuint>(m_heightMap[x]);
        for (int dy = 0; dy < 10 && startY + dy < m_height; dy++) {
            GLuint idx = ((startY + dy) * m_width + x) * 4;
            pixels[idx + 0] = 34;
            pixels[idx + 1] = 139;
            pixels[idx + 2] = 34;
            pixels[idx + 3] = 255;
        }
    }

    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_width, m_height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Quad VAO: position(2f) + uv(2f) + color(4f) per vertex, matches batch shader layout
    // 6 vertices, each with 8 floats
    float vertices[6 * 8] = {};
    // positions and UVs will be updated per frame in Draw(); colors are always white
    for (int i = 0; i < 6; i++) {
        vertices[i * 8 + 6] = 1.0f;  // r
        vertices[i * 8 + 7] = 1.0f;  // g
    }
    // Store the white-color template
    memcpy(m_quadTemplate, vertices, sizeof(vertices));

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
    // position (location 0)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    // uv (location 1)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(2 * sizeof(float)));
    // color (location 2)
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(4 * sizeof(float)));
    glBindVertexArray(0);

    m_initialized = true;
}

void Terrain::Draw(float camX, float camY, GLuint vpW, GLuint vpH) {
    if (!m_initialized) return;

    // Use batch shader — it's already compiled and handles pos+uv+color
    auto& shader = ResourceManager::GetShader("sprite_batch");
    shader.Use();
    // Identity projection: terrain quad is already in NDC (-1..1)
    glm::mat4 identity(1.0f);
    shader.SetMatrix4("projection", identity);
    shader.SetInteger("image", 0);

    // Calculate UV coords for the visible portion
    // Texture was loaded with row 0 at the bottom (OpenGL default).
    // Our pixel data row y=0 is world Y=0 (top of world).
    // So texture v=0 = world Y=(height-1), v=1 = world Y=0.
    // We need to flip: world Y=camY maps to v = 1 - camY/height
    float u0 = camX / (float)m_width;
    float u1 = (camX + vpW) / (float)m_width;
    float v0 = 1.0f - (camY + vpH) / (float)m_height;
    float v1 = 1.0f - camY / (float)m_height;

    // 6 vertices × 8 floats (pos2 + uv2 + color4)
    float vertices[6 * 8];
    memcpy(vertices, m_quadTemplate, sizeof(vertices));

    // Triangle 1: top-left, bottom-left, bottom-right
    // Triangle 2: top-left, bottom-right, top-right
    // pos (-1,1) = top-left → UV (u0, v1) [high v = low world Y = top]
    // pos (-1,-1) = bottom-left → UV (u0, v0)
    // pos (1,-1) = bottom-right → UV (u1, v0)
    // pos (1,1) = top-right → UV (u1, v1)
    int i = 0;
    // top-left
    vertices[i*8+0] = -1.0f; vertices[i*8+1] = 1.0f;
    vertices[i*8+2] = u0; vertices[i*8+3] = v1;
    i++;
    // bottom-left
    vertices[i*8+0] = -1.0f; vertices[i*8+1] = -1.0f;
    vertices[i*8+2] = u0; vertices[i*8+3] = v0;
    i++;
    // bottom-right
    vertices[i*8+0] = 1.0f; vertices[i*8+1] = -1.0f;
    vertices[i*8+2] = u1; vertices[i*8+3] = v0;
    i++;
    // top-left
    vertices[i*8+0] = -1.0f; vertices[i*8+1] = 1.0f;
    vertices[i*8+2] = u0; vertices[i*8+3] = v1;
    i++;
    // bottom-right
    vertices[i*8+0] = 1.0f; vertices[i*8+1] = -1.0f;
    vertices[i*8+2] = u1; vertices[i*8+3] = v0;
    i++;
    // top-right
    vertices[i*8+0] = 1.0f; vertices[i*8+1] = 1.0f;
    vertices[i*8+2] = u1; vertices[i*8+3] = v1;

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_texture);

    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Terrain::RemoveCircle(float cx, float cy, float radius) {
    if (!m_initialized) return;

    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    int r = static_cast<int>(radius) + 2;
    int x0 = std::max(0, static_cast<int>(cx - r));
    int y0 = std::max(0, static_cast<int>(cy - r));
    int w = std::min(r * 2, static_cast<int>(m_width) - x0);
    int h = std::min(r * 2, static_cast<int>(m_height) - y0);
    if (w <= 0 || h <= 0) { glBindFramebuffer(GL_FRAMEBUFFER, 0); return; }

    std::vector<GLubyte> pixels(w * h * 4);
    glReadPixels(x0, y0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    float r2 = radius * radius;
    for (int py = 0; py < h; py++) {
        for (int px = 0; px < w; px++) {
            float dx = (x0 + px) - cx;
            float dy = (y0 + py) - cy;
            if (dx * dx + dy * dy <= r2) {
                int idx = (py * w + px) * 4;
                pixels[idx + 3] = 0;
            }
        }
    }

    glTexSubImage2D(GL_TEXTURE_2D, 0, x0, y0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool Terrain::IsSolid(float x, float y) const {
    GLuint ix = static_cast<GLuint>(x);
    GLuint iy = static_cast<GLuint>(y);
    if (ix >= m_width || iy >= m_height) return false;

    return iy >= static_cast<GLuint>(m_heightMap[ix]);
}
