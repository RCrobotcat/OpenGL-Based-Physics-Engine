//
// Created by 25190 on 2026/2/20.
//

#ifndef IF_PHYSICS_DRAWOBJECTS_H
#define IF_PHYSICS_DRAWOBJECTS_H
#include "glm/vec3.hpp"
#include "Shader.h"
#include "Model.h"

#endif //IF_PHYSICS_DRAWOBJECTS_H

void renderSphere();

void renderCube();

void renderQuad();

void renderPlane(float size = 10.0f, float uvScale = 25.0f);

void renderLights(Shader &pbrShader, glm::mat4 &model, unsigned int AlbedoMap, unsigned int NormalMap,
                  unsigned int MetallicMap, unsigned int RoughnessMap, unsigned int AOMap);

void renderModel(Shader &pbrShader, Model objectModel, glm::mat4 &model, unsigned int AlbedoMap, unsigned int NormalMap,
                  unsigned int MetallicMap, unsigned int RoughnessMap, unsigned int AOMap);