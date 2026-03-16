//
// Created by 25190 on 2025/11/9.
//

#ifndef LEARNOPENGL_MODEL_H
#define LEARNOPENGL_MODEL_H
#include <vector>

#include "Mesh.h"
#include "Shader.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

class Model
{
public:
    /*  函数   */
    Model(char const *path)
    {
        loadModel(path);
    }

    void Draw(Shader &shader);

    std::vector<Mesh> getMeshes()
    {
        return meshes;
    }

    std::vector<Texture> getTextures()
    {
        return textures_loaded;
    }

private:
    /*  模型数据  */
    std::vector<Mesh> meshes;
    std::string directory;

    // stores all the textures loaded so far, optimization to make sure textures aren't loaded more than once.
    std::vector<Texture> textures_loaded;

    /*  函数   */
    void loadModel(std::string path);

    void processNode(aiNode *node, const aiScene *scene);

    Mesh processMesh(aiMesh *mesh, const aiScene *scene);

    std::vector<Texture> loadMaterialTextures(aiMaterial *mat, aiTextureType type,
                                              std::string typeName);
};


#endif //LEARNOPENGL_MODEL_H
