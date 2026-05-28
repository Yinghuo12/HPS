#include "texture.h"

Texture2D::Texture2D()
    : ID(0), Width(0), Height(0)
    , InternalFormat(GL_RGBA), ImageFormat(GL_RGBA)
    , WrapS(GL_REPEAT), WrapT(GL_REPEAT)
    , FilterMin(GL_LINEAR), FilterMag(GL_LINEAR) {}

void Texture2D::Generate(GLuint width, GLuint height, unsigned char* data) {
    Width = width;
    Height = height;
    glGenTextures(1, &ID);
    glBindTexture(GL_TEXTURE_2D, ID);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, InternalFormat, width, height, 0, ImageFormat, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, WrapS);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, WrapT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, FilterMin);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, FilterMag);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Texture2D::Bind() const {
    glBindTexture(GL_TEXTURE_2D, ID);
}
