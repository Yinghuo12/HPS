#include "resource_manager.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <iostream>

std::map<std::string, Shader> ResourceManager::Shaders;
std::map<std::string, Texture2D> ResourceManager::Textures;

Shader& ResourceManager::LoadShader(const GLchar* vShaderFile, const GLchar* fShaderFile, const std::string& name) {
    Shaders[name] = Shader(vShaderFile, fShaderFile);
    return Shaders[name];
}

Shader& ResourceManager::LoadShaderFromSource(const GLchar* vSource, const GLchar* fSource, const std::string& name) {
    Shaders[name] = Shader(vSource, fSource, true);
    return Shaders[name];
}

Shader& ResourceManager::GetShader(const std::string& name) {
    return Shaders[name];
}

Texture2D& ResourceManager::LoadTexture(const GLchar* file, GLboolean alpha, const std::string& name) {
    Textures[name] = loadTextureFromFile(file, alpha);
    return Textures[name];
}

Texture2D& ResourceManager::GetTexture(const std::string& name) {
    return Textures[name];
}

void ResourceManager::Clear() {
    for (auto& iter : Shaders)
        glDeleteProgram(iter.second.ID);
    for (auto& iter : Textures)
        glDeleteTextures(1, &iter.second.ID);
}

Texture2D ResourceManager::loadTextureFromFile(const GLchar* file, GLboolean alpha) {
    Texture2D texture;
    if (alpha) {
        texture.InternalFormat = GL_RGBA;
        texture.ImageFormat = GL_RGBA;
    }
    int width, height, nrChannels;
    unsigned char* data = stbi_load(file, &width, &height, &nrChannels, 0);
    if (data) {
        std::cout << "  [OK] " << file << " " << width << "x" << height << " ch=" << nrChannels << std::endl;
        texture.Generate(width, height, data);
    } else {
        std::cerr << "Texture failed to load: " << file << std::endl;
    }
    stbi_image_free(data);
    return texture;
}
