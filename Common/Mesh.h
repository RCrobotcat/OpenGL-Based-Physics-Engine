//
// Created by 25190 on 2025/11/9.
//

#ifndef LEARNOPENGL_MESH_H
#define LEARNOPENGL_MESH_H
#include <string>

#include "Shader.h"
#include "assimp/types.h"
#include "glm/vec2.hpp"
#include "glm/vec3.hpp"
#define MAX_BONE_INFLUENCE 4

struct Vertex
{
    // position
    glm::vec3 Position;
    // normal
    glm::vec3 Normal;
    // texCoords
    glm::vec2 TexCoords;
    // tangent
    glm::vec3 Tangent;
    // bitangent
    glm::vec3 Bitangent;
    //bone indexes which will influence this vertex
    int m_BoneIDs[MAX_BONE_INFLUENCE];
    //weights from each bone
    float m_Weights[MAX_BONE_INFLUENCE];
};

struct Texture {
    unsigned int id;
    std::string type;
    aiString path;  // 我们储存纹理的路径用于与其它纹理进行比较
};

class Mesh
{
public:
    /*  网格数据  */
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    std::vector<Texture> textures;
    unsigned int VAO;

    /*  函数  */
    Mesh(std::vector<Vertex> vertices, std::vector<unsigned int> indices, std::vector<Texture> textures);

    void Draw(Shader &shader);

private:
    /*  渲染数据  */
    unsigned int VBO, EBO;

    /*  函数  */
    void setupMesh();
};


#endif //LEARNOPENGL_MESH_H
