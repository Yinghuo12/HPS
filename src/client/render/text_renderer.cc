#include "text_renderer.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

static const char* TEXT_VERT_SRC =
    "#version 330 core\n"
    "layout (location = 0) in vec4 vertex;\n"
    "out vec2 TexCoords;\n"
    "uniform mat4 projection;\n"
    "void main() {\n"
    "    gl_Position = projection * vec4(vertex.xy, 0.0, 1.0);\n"
    "    TexCoords = vertex.zw;\n"
    "}\n";

static const char* TEXT_FRAG_SRC =
    "#version 330 core\n"
    "in vec2 TexCoords;\n"
    "out vec4 FragColor;\n"
    "uniform sampler2D text;\n"
    "uniform vec3 textColor;\n"
    "void main() {\n"
    "    vec4 sampled = vec4(1.0, 1.0, 1.0, texture(text, TexCoords).r);\n"
    "    FragColor = vec4(textColor, 1.0) * sampled;\n"
    "}\n";

TextRenderer::TextRenderer(GLuint width, GLuint height)
    : m_width(width), m_height(height) {}

TextRenderer::~TextRenderer() {
    for (auto& kv : m_characters) {
        glDeleteTextures(1, &kv.second.TextureID);
    }
    m_characters.clear();
    if (m_VAO) glDeleteVertexArrays(1, &m_VAO);
    if (m_VBO) glDeleteBuffers(1, &m_VBO);
    if (m_shaderProgram) glDeleteProgram(m_shaderProgram);

    if (m_ftFace) {
        FT_Done_Face(static_cast<FT_Face>(m_ftFace));
        m_ftFace = nullptr;
    }
    if (m_ftLibrary) {
        FT_Done_FreeType(static_cast<FT_Library>(m_ftLibrary));
        m_ftLibrary = nullptr;
    }
}

void TextRenderer::Load(const std::string& fontPath, GLuint fontSize) {
    if (m_initialized) return;

    // Compile shader
    auto compile = [](GLenum type, const char* src) -> GLuint {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[512];
            glGetShaderInfoLog(s, 512, nullptr, log);
            std::cerr << "Text shader error: " << log << std::endl;
        }
        return s;
    };

    GLuint vs = compile(GL_VERTEX_SHADER, TEXT_VERT_SRC);
    GLuint fs = compile(GL_FRAGMENT_SHADER, TEXT_FRAG_SRC);
    m_shaderProgram = glCreateProgram();
    glAttachShader(m_shaderProgram, vs);
    glAttachShader(m_shaderProgram, fs);
    glLinkProgram(m_shaderProgram);
    glDeleteShader(vs);
    glDeleteShader(fs);

    // Configure VAO/VBO
    glGenVertexArrays(1, &m_VAO);
    glGenBuffers(1, &m_VBO);
    glBindVertexArray(m_VAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(GLfloat) * 6 * 4, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    // FreeType init
    FT_Library ft;
    if (FT_Init_FreeType(&ft)) {
        std::cerr << "FreeType init failed" << std::endl;
        return;
    }

    FT_Face face;
    if (FT_New_Face(ft, fontPath.c_str(), 0, &face)) {
        std::cerr << "Failed to load font: " << fontPath << std::endl;
        FT_Done_FreeType(ft);
        return;
    }

    FT_Set_Pixel_Sizes(face, 0, fontSize);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // Keep face alive for lazy CJK loading
    m_ftLibrary = ft;
    m_ftFace = face;

    // Load ASCII printable characters (32-126)
    for (uint32_t c = 32; c < 127; c++) {
        if (FT_Load_Char(face, c, FT_LOAD_RENDER)) continue;

        GLuint texture;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(
            GL_TEXTURE_2D, 0, GL_RED,
            face->glyph->bitmap.width, face->glyph->bitmap.rows,
            0, GL_RED, GL_UNSIGNED_BYTE, face->glyph->bitmap.buffer);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        Character ch = {
            texture,
            glm::ivec2(face->glyph->bitmap.width, face->glyph->bitmap.rows),
            glm::ivec2(face->glyph->bitmap_left, face->glyph->bitmap_top),
            (GLuint)face->glyph->advance.x
        };
        m_characters[c] = ch;
    }

    m_initialized = true;
    std::cout << "TextRenderer: loaded " << m_characters.size()
              << " chars from " << fontPath << std::endl;
}

void TextRenderer::DrawText(const std::string& text, float x, float y,
                             float scale, glm::vec3 color) {
    glUseProgram(m_shaderProgram);
    glUniform3f(glGetUniformLocation(m_shaderProgram, "textColor"),
                color.x, color.y, color.z);
    glm::mat4 projection = glm::ortho(0.0f, (float)m_width, (float)m_height, 0.0f);
    glUniformMatrix4fv(glGetUniformLocation(m_shaderProgram, "projection"),
                       1, GL_FALSE, &projection[0][0]);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(m_VAO);

    // Iterate UTF-8 codepoints
    const uint8_t* str = (const uint8_t*)text.c_str();
    size_t len = text.size();
    size_t i = 0;

    while (i < len) {
        uint32_t cp = 0;
        if (str[i] < 0x80) {
            cp = str[i]; i += 1;
        } else if ((str[i] & 0xE0) == 0xC0) {
            if (i + 1 >= len) break;
            cp = (str[i] & 0x1F) << 6 | (str[i+1] & 0x3F); i += 2;
        } else if ((str[i] & 0xF0) == 0xE0) {
            if (i + 2 >= len) break;
            cp = (str[i] & 0x0F) << 12 | (str[i+1] & 0x3F) << 6 | (str[i+2] & 0x3F); i += 3;
        } else if ((str[i] & 0xF8) == 0xF0) {
            if (i + 3 >= len) break;
            cp = (str[i] & 0x07) << 18 | (str[i+1] & 0x3F) << 12 | (str[i+2] & 0x3F) << 6 | (str[i+3] & 0x3F); i += 4;
        } else {
            i += 1; continue;
        }

        auto it = m_characters.find(cp);
        if (it == m_characters.end()) {
            ensureGlyph(cp);
            it = m_characters.find(cp);
            if (it == m_characters.end()) {
                x += 10.0f * scale;
                continue;
            }
        }

        Character& ch = it->second;
        GLfloat xpos = x + ch.Bearing.x * scale;
        GLfloat ypos = y + ch.Bearing.y * scale;
        GLfloat w = ch.Size.x * scale;
        GLfloat h = ch.Size.y * scale;

        GLfloat vertices[6][4] = {
            { xpos,     ypos + h, 0.0f, 1.0f },
            { xpos,     ypos,     0.0f, 0.0f },
            { xpos + w, ypos,     1.0f, 0.0f },
            { xpos,     ypos + h, 0.0f, 1.0f },
            { xpos + w, ypos,     1.0f, 0.0f },
            { xpos + w, ypos + h, 1.0f, 1.0f }
        };

        glBindTexture(GL_TEXTURE_2D, ch.TextureID);
        glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        x += (ch.Advance >> 6) * scale;
    }

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
}

float TextRenderer::GetTextWidth(const std::string& text, float scale) const {
    float width = 0.0f;
    const uint8_t* str = (const uint8_t*)text.c_str();
    size_t len = text.size();
    size_t i = 0;

    while (i < len) {
        uint32_t cp = 0;
        if (str[i] < 0x80) {
            cp = str[i]; i += 1;
        } else if ((str[i] & 0xE0) == 0xC0) {
            if (i + 1 >= len) break;
            cp = (str[i] & 0x1F) << 6 | (str[i+1] & 0x3F); i += 2;
        } else if ((str[i] & 0xF0) == 0xE0) {
            if (i + 2 >= len) break;
            cp = (str[i] & 0x0F) << 12 | (str[i+1] & 0x3F) << 6 | (str[i+2] & 0x3F); i += 3;
        } else if ((str[i] & 0xF8) == 0xF0) {
            if (i + 3 >= len) break;
            cp = (str[i] & 0x07) << 18 | (str[i+1] & 0x3F) << 12 | (str[i+2] & 0x3F) << 6 | (str[i+3] & 0x3F); i += 4;
        } else {
            i += 1; continue;
        }

        auto it = m_characters.find(cp);
        if (it != m_characters.end()) {
            width += (it->second.Advance >> 6) * scale;
        } else {
            width += 10.0f * scale;
        }
    }
    return width;
}

void TextRenderer::ensureGlyph(uint32_t codepoint) {
    if (!m_ftFace) return;

    FT_Face face = static_cast<FT_Face>(m_ftFace);
    if (FT_Load_Char(face, codepoint, FT_LOAD_RENDER)) return;

    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(
        GL_TEXTURE_2D, 0, GL_RED,
        face->glyph->bitmap.width, face->glyph->bitmap.rows,
        0, GL_RED, GL_UNSIGNED_BYTE, face->glyph->bitmap.buffer);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    Character ch = {
        texture,
        glm::ivec2(face->glyph->bitmap.width, face->glyph->bitmap.rows),
        glm::ivec2(face->glyph->bitmap_left, face->glyph->bitmap_top),
        (GLuint)face->glyph->advance.x
    };
    m_characters[codepoint] = ch;
}
