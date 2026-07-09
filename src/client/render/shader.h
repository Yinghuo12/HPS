#ifndef SHADER_H
#define SHADER_H

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string>

class Shader {
public:
    GLuint ID;
    Shader() : ID(0) {}
    Shader(const GLchar* vertexPath, const GLchar* fragmentPath);
    Shader(const GLchar* vSource, const GLchar* fSource, bool fromString);
    void Use() const;
    void SetMatrix4(const std::string& name, const glm::mat4& value) const;
    void SetVector3f(const std::string& name, float x, float y, float z) const;
    void SetVector2f(const std::string& name, float x, float y) const;
    void SetInteger(const std::string& name, int value) const;
    void SetFloat(const std::string& name, float value) const;
private:
    void checkCompileErrors(GLuint object, const std::string& type);
};

#endif
