#ifndef TEXT_RENDERER_H
#define TEXT_RENDERER_H

#include <string>
#include <map>
#include <glad/glad.h>
#include <glm/glm.hpp>

struct Character {
    GLuint TextureID;
    glm::ivec2 Size;
    glm::ivec2 Bearing;
    GLuint Advance;
};

class TextRenderer {
public:
    TextRenderer(GLuint width, GLuint height);
    ~TextRenderer();

    void Load(const std::string& fontPath, GLuint fontSize);
    void DrawText(const std::string& text, float x, float y, float scale,
                  glm::vec3 color = glm::vec3(1.0f));
    float GetTextWidth(const std::string& text, float scale) const;

private:
    void ensureGlyph(uint32_t codepoint);

    std::map<uint32_t, Character> m_characters;
    GLuint m_VAO, m_VBO;
    GLuint m_width, m_height;
    GLuint m_shaderProgram;
    bool m_initialized = false;

    void* m_ftLibrary = nullptr;
    void* m_ftFace = nullptr;
};

#endif
