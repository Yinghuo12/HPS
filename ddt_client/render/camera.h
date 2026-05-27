#ifndef CAMERA_H
#define CAMERA_H

// #include <GL/gl.h>
#include <glad/glad.h>
#include <glm/glm.hpp>

class Camera {
public:
    Camera(GLuint vpW, GLuint vpH, GLuint worldW, GLuint worldH);

    glm::vec2 worldToScreen(const glm::vec2& worldPos) const;
    glm::vec2 screenToWorld(const glm::vec2& screenPos) const;

    void setCenter(float worldX, float worldY);
    void snapTo(float worldX, float worldY);
    void panTo(float worldX, float worldY, float speed);
    void clamp();
    void update(float dt);

    float getCamX() const { return m_camX; }
    float getCamY() const { return m_camY; }
    GLuint getWorldW() const { return m_worldW; }
    GLuint getWorldH() const { return m_worldH; }
    GLuint getViewportW() const { return m_vpW; }
    GLuint getViewportH() const { return m_vpH; }
    bool isPanning() const { return m_panning; }

private:
    float m_camX, m_camY;
    float m_targetX, m_targetY;
    float m_panSpeed;
    bool  m_panning;
    GLuint m_vpW, m_vpH;
    GLuint m_worldW, m_worldH;
};

#endif
