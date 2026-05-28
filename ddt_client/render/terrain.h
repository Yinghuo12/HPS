#ifndef TERRAIN_H
#define TERRAIN_H

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>

class Terrain {
public:
    Terrain(GLuint worldWidth, GLuint worldHeight);
    ~Terrain();

    void Init();
    void Draw(float camX, float camY, GLuint vpW, GLuint vpH);
    void RemoveCircle(float cx, float cy, float radius);
    bool IsSolid(float x, float y) const;

private:
    void generateHeightMap();
    void renderToFBO();

    GLuint m_width, m_height;
    GLuint m_fbo;
    GLuint m_texture;
    GLuint m_vao, m_vbo;
    bool m_initialized;

    std::vector<float> m_heightMap;
    std::vector<GLubyte> m_pixels;
    float m_quadTemplate[6 * 8];  // pos(2) + uv(2) + color(4) per vertex
};

#endif