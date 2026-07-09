#ifndef TEXTURE_H
#define TEXTURE_H

#include <glad/glad.h>
#include <string>

class Texture2D {
public:
    GLuint ID;
    GLuint Width, Height;
    GLuint InternalFormat;
    GLuint ImageFormat;
    GLuint WrapS, WrapT;
    GLuint FilterMin, FilterMag;

    Texture2D();
    void Generate(GLuint width, GLuint height, unsigned char* data);
    void Bind() const;
};

#endif
