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

        float dx = (float)x - m_width * 0.5f;
        float a = 2380.0f * 3.0f;
        float sag = a * a - dx * dx;
        if (sag > 0) {
            baseH = 2860.0f * 2.1f - std::sqrt(sag);
        }

        baseH += 25.0f * std::sin(x * 0.008f) + 12.0f * std::sin(x * 0.023f);

        if (x > 400 && x < 600) baseH -= 40.0f;   
        if (x > 1400 && x < 1600) baseH -= 60.0f;  
        if (x > 2400 && x < 2600) baseH -= 40.0f;  

        m_heightMap[x] = std::max(600.0f, std::min(1250.0f, baseH));
    }
}

void Terrain::Init() {
    generateHeightMap();

    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture, 0);

    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // 【修改点 2】：填充主像素数组，分配在成员变量 m_pixels 中
    m_pixels.resize(m_width * m_height * 4, 0);
    for (GLuint x = 0; x < m_width; x++) {
        float h = m_heightMap[x];
        GLuint startY = static_cast<GLuint>(h);
        for (GLuint y = startY; y < m_height; y++) {
            GLuint idx = (y * m_width + x) * 4;
            m_pixels[idx + 0] = 139;
            m_pixels[idx + 1] = 119;
            m_pixels[idx + 2] = 101;
            m_pixels[idx + 3] = 255;
        }
    }

    for (GLuint x = 0; x < m_width; x++) {
        GLuint startY = static_cast<GLuint>(m_heightMap[x]);
        for (int dy = 0; dy < 10 && startY + dy < m_height; dy++) {
            GLuint idx = ((startY + dy) * m_width + x) * 4;
            m_pixels[idx + 0] = 34;
            m_pixels[idx + 1] = 139;
            m_pixels[idx + 2] = 34;
            m_pixels[idx + 3] = 255;
        }
    }

    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_width, m_height, GL_RGBA, GL_UNSIGNED_BYTE, m_pixels.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    float vertices[6 * 8] = {};
    for (int i = 0; i < 6; i++) {
        vertices[i * 8 + 4] = 1.0f;  // R
        vertices[i * 8 + 5] = 1.0f;  // G
        vertices[i * 8 + 6] = 1.0f;  // B
        vertices[i * 8 + 7] = 1.0f;  // A
    }
    memcpy(m_quadTemplate, vertices, sizeof(vertices));

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
    
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(4 * sizeof(float)));
    glBindVertexArray(0);

    m_initialized = true;
}

void Terrain::Draw(float camX, float camY, GLuint vpW, GLuint vpH) {
    if (!m_initialized) return;

    auto& shader = ResourceManager::GetShader("sprite_batch");
    shader.Use();
    glm::mat4 identity(1.0f);
    shader.SetMatrix4("projection", identity);
    shader.SetInteger("image", 0);

    float u0 = camX / (float)m_width;
    float u1 = (camX + vpW) / (float)m_width;
    
    float v0 = (camY + vpH) / (float)m_height; 
    float v1 = camY / (float)m_height;         

    float vertices[6 * 8];
    memcpy(vertices, m_quadTemplate, sizeof(vertices));

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

    // 【修改点 3】：
    // 1. 同步更新客户端本地物理高度图，让 IsSolid 判断立刻生效，人物才能自然掉到坑里
    int r = static_cast<int>(radius);
    int x0_hm = std::max(0, static_cast<int>(cx - r));
    int x1_hm = std::min(static_cast<int>(m_width) - 1, static_cast<int>(cx + r));
    float r2_hm = radius * radius;
    for (int x = x0_hm; x <= x1_hm; x++) {
        float dx = (float)x - cx;
        float dx2 = r2_hm - dx * dx;
        if (dx2 <= 0) continue;
        float halfW = std::sqrt(dx2);
        float bottomEdge = cy + halfW;
        if (m_heightMap[x] < bottomEdge) {
            m_heightMap[x] = bottomEdge;
        }
    }

    // 2. 通过内存像素直接挖空圆圈区域，并上传局部的变化矩形给 GPU。
    // 这避开了 glReadPixels 导致的游戏卡顿、黑坑，兼容所有显卡。
    int x0 = std::max(0, static_cast<int>(cx - r - 2));
    int y0 = std::max(0, static_cast<int>(cy - r - 2));
    int w = std::min((r + 2) * 2, static_cast<int>(m_width) - x0);
    int h = std::min((r + 2) * 2, static_cast<int>(m_height) - y0);
    if (w <= 0 || h <= 0) return;

    std::vector<GLubyte> subPixels(w * h * 4, 0);
    float r2 = radius * radius;
    for (int py = 0; py < h; py++) {
        for (int px = 0; px < w; px++) {
            int worldX = x0 + px;
            int worldY = y0 + py;
            
            float dx = (float)worldX - cx;
            float dy = (float)worldY - cy;
            int masterIdx = (worldY * m_width + worldX) * 4;
            
            if (dx * dx + dy * dy <= r2) {
                m_pixels[masterIdx + 0] = 0;
                m_pixels[masterIdx + 1] = 0;
                m_pixels[masterIdx + 2] = 0;
                m_pixels[masterIdx + 3] = 0; // Alpha 置零：抠图挖坑
            }
            
            int subIdx = (py * w + px) * 4;
            subPixels[subIdx + 0] = m_pixels[masterIdx + 0];
            subPixels[subIdx + 1] = m_pixels[masterIdx + 1];
            subPixels[subIdx + 2] = m_pixels[masterIdx + 2];
            subPixels[subIdx + 3] = m_pixels[masterIdx + 3];
        }
    }

    // 快速替换局部贴图
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x0, y0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, subPixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

bool Terrain::IsSolid(float x, float y) const {
    GLuint ix = static_cast<GLuint>(x);
    GLuint iy = static_cast<GLuint>(y);
    if (ix >= m_width || iy >= m_height) return false;

    return iy >= static_cast<GLuint>(m_heightMap[ix]);
}