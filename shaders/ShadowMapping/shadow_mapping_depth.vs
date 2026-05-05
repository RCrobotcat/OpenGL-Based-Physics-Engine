#version 330 core
layout (location = 0) in vec3 aPos;

uniform mat4 lightSpaceMatrix;
uniform mat4 model;

void main()
{
    // 转换顶点位置到光源空间
    gl_Position = lightSpaceMatrix * model * vec4(aPos, 1.0);
}
