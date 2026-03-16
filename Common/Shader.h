//
// Created by 刘宇杰 on 25-10-30.
//

#ifndef LEARNOPENGL_SHADER_H
#define LEARNOPENGL_SHADER_H

#include <string>

#include "glm/fwd.hpp"

class Shader
{
public:
    unsigned int ID;

    // constructor generates the shader on the fly
    Shader(const char *vertexPath, const char *fragmentPath);

    Shader(const char *vertexPath, const char *geometryPath, const char *fragmentPath);

    // activate the shader
    void use();

    // utility uniform functions
    void setBool(const std::string &name, bool value) const;

    void setInt(const std::string &name, int value) const;

    void setFloat(const std::string &name, float value) const;

    void setVec3(const std::string &name, glm::vec3 vec) const;

    void setVec3(const std::string &name, float x, float y, float z) const;

    void setMat4(const std::string &name, glm::mat4 mat) const;

    void setMat3(const std::string &name, glm::mat3 mat) const;

    // utility function for checking shader compilation/linking errors.
    void checkCompileErrors(unsigned int shader, std::string type);

    ~Shader();
};

#endif //LEARNOPENGL_SHADER_H
