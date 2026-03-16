#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <unordered_map>
#include <vector>

#include "Common/Shader.h"
#include "Common/Camera.h"
#include "Common/Model.h"

#define STB_IMAGE_IMPLEMENTATION
#include "Common/stb_image.h"
#include "Common/DrawObjects.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_opengl3.h"

#include <rc_net.h>

#include "Common/utils.h"
#include "PhysicsServer/PhysicsCommon.h"

struct NetPlayerState
{
    sPlayerDescription desc{};
};

struct NetDynamicObjectState
{
    sDynamicObjectDescription desc{};
};

struct _Bullet
{
    glm::vec3 position;
    glm::vec3 velocity;
    float life;
};

std::vector<_Bullet> g_bullets;

struct TextureSet
{
    unsigned int albedo = 0;
    unsigned int normal = 0;
    unsigned int metallic = 0;
    unsigned int roughness = 0;
    unsigned int ao = 0;
};

class DemoClient : public RCNet::net::client_interface<DemoGameMsg>
{
public:
    void SendRegister()
    {
        RCNet::net::message<DemoGameMsg> msg;
        msg.header.id = DemoGameMsg::Client_RegisterWithServer;
        Send(msg);
    }

    void SendUnregister()
    {
        RCNet::net::message<DemoGameMsg> msg;
        msg.header.id = DemoGameMsg::Client_UnregisterWithServer;
        Send(msg);
    }

    void SendInput(const sPlayerInput &input)
    {
        RCNet::net::message<DemoGameMsg> msg;
        msg.header.id = DemoGameMsg::Client_PlayerInput;
        msg << input;
        Send(msg);
    }
};

static const unsigned int SCR_WIDTH = 1024;
static const unsigned int SCR_HEIGHT = 768;

static Camera camera(glm::vec3(0.0f, 2.0f, 10.0f));
static float lastX = SCR_WIDTH / 2.0f;
static float lastY = SCR_HEIGHT / 2.0f;
static bool firstMouse = true;

static float deltaTime = 0.0f;
static float lastFrame = 0.0f;

static bool g_acceptReceived = false;
static uint32_t g_localPlayerID = 0;

static std::unordered_map<uint32_t, NetPlayerState> g_players;
static std::unordered_map<uint32_t, NetDynamicObjectState> g_dynamicObjects;

static constexpr float g_playerEyeHeight = 3.75f;
static constexpr float g_floorSize = 100.0f;
static constexpr float g_floorUVScale = 25.0f;

static bool g_firePressedLast = false;
static float g_fireCooldown = 0.0f;
static constexpr float g_fireRate = 10.0f;
static constexpr float g_bulletSpeed = 200.0f;
static constexpr float g_bulletLife = 2.0f;

static void SpawnLocal_BulletTrace()
{
    glm::vec3 dir = glm::normalize(camera.Front);

    glm::mat4 invView = glm::inverse(camera.GetViewMatrix());
    glm::vec4 muzzleLocal(0.12f, -0.2f, -0.40f, 1.0f);
    glm::vec3 startPos = glm::vec3(invView * muzzleLocal);

    _Bullet b;
    b.position = startPos;
    b.velocity = dir * g_bulletSpeed;
    b.life = g_bulletLife;
    g_bullets.push_back(b);
}

void framebuffer_size_callback(GLFWwindow *window, int width, int height)
{
    (void) window;
    glViewport(0, 0, width, height);
}

void mouse_callback(GLFWwindow *window, double xposIn, double yposIn)
{
    (void) window;

    float xpos = static_cast<float>(xposIn);
    float ypos = static_cast<float>(yposIn);

    if (firstMouse)
    {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }

    float xoffset = xpos - lastX;
    float yoffset = lastY - ypos;

    lastX = xpos;
    lastY = ypos;

    camera.ProcessMouseMovement(xoffset, yoffset);
}

void scroll_callback(GLFWwindow *window, double xoffset, double yoffset)
{
    (void) window;
    (void) xoffset;
    camera.ProcessMouseScroll(static_cast<float>(yoffset));
}

static GFloat toGFloat(float v)
{
    return GFloat::FromFloat(v);
}

static float toFloat(const GFloat &v)
{
    return static_cast<float>(v.toDouble());
}

static void HandleIncoming(DemoClient &client)
{
    while (!client.Incoming().empty())
    {
        auto owned = client.Incoming().pop_front();
        auto &msg = owned.msg;

        switch (msg.header.id)
        {
            case DemoGameMsg::Client_Accepted:
            {
                g_acceptReceived = true;
                client.SendRegister();
                break;
            }
            case DemoGameMsg::Client_AssignID:
            {
                if (msg.body.size() < sizeof(uint32_t))
                {
                    break;
                }
                uint32_t id = 0;
                msg >> id;
                g_localPlayerID = id;
                break;
            }
            case DemoGameMsg::Game_AddPlayer:
            case DemoGameMsg::Game_UpdatePlayer:
            {
                if (msg.body.size() < sizeof(sPlayerDescription))
                {
                    break;
                }
                sPlayerDescription desc{};
                msg >> desc;
                g_players[desc.nUniqueID] = NetPlayerState{desc};
                break;
            }
            case DemoGameMsg::Game_RemovePlayer:
            {
                if (msg.body.size() < sizeof(uint32_t))
                {
                    break;
                }
                uint32_t id = 0;
                msg >> id;
                g_players.erase(id);
                break;
            }
            case DemoGameMsg::Game_UpdateDynamicObject:
            {
                if (msg.body.size() < sizeof(sDynamicObjectDescription))
                {
                    break;
                }
                sDynamicObjectDescription desc{};
                msg >> desc;
                g_dynamicObjects[desc.objectID] = NetDynamicObjectState{desc};
                break;
            }
            case DemoGameMsg::Game_SyncOtherPlayersBullets:
            {
                if (msg.body.size() < sizeof(sBulletDescription))
                {
                    break;
                }
                sBulletDescription desc{};
                msg >> desc;
                if (desc.playerID != g_localPlayerID)
                {
                    _Bullet b;
                    b.position = glm::vec3(toFloat(desc.x), toFloat(desc.y), toFloat(desc.z));
                    b.velocity = glm::vec3(toFloat(desc.dirX), toFloat(desc.dirY), toFloat(desc.dirZ)) * g_bulletSpeed;
                    b.life = g_bulletLife;
                    g_bullets.push_back(b);
                }
                break;
            }
            default:
                break;
        }
    }
}

static void BuildInput(GLFWwindow *window, sPlayerInput &input)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
    {
        glfwSetWindowShouldClose(window, true);
    }

    glm::vec3 forward = glm::normalize(glm::vec3(camera.Front.x, 0.0f, camera.Front.z));
    glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));

    glm::vec3 wish(0.0f);
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) wish += forward;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) wish -= forward;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) wish += right;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) wish -= right;

    if (glm::length(wish) > 1e-5f)
    {
        wish = glm::normalize(wish);
    }

    input.moveX = toGFloat(wish.x);
    input.moveZ = toGFloat(wish.z);
    input.yaw = toGFloat(glm::radians(camera.Yaw));
    input.jumpPressed = (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) ? 1u : 0u;

    input.shooting = (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) ? 1u : 0u;
    input.firePosX = toGFloat(camera.Position.x);
    input.firePosY = toGFloat(camera.Position.y);
    input.firePosZ = toGFloat(camera.Position.z);
    input.dirX = toGFloat(camera.Front.x);
    input.dirY = toGFloat(camera.Front.y);
    input.dirZ = toGFloat(camera.Front.z);
}

int main()
{
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow *window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "PhysicsEngineDemo_Online", nullptr, nullptr);
    if (window == nullptr)
    {
        std::cout << "Failed to create GLFW window\n";
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    if (!gladLoadGLLoader((GLADloadproc) glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD\n";
        return -1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    (void) io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    Shader pbrShader("../shaders/PBR/IBL/pbr.vs", "../shaders/PBR/IBL/pbr.fs");
    Shader equirectangularToCubemapShader("../shaders/PBR/IBL/cubemap.vs",
                                          "../shaders/PBR/IBL/equirectangular_to_cubemap.fs");
    Shader irradianceShader("../shaders/PBR/IBL/cubemap.vs", "../shaders/PBR/IBL/irradiance_convolution.fs");
    Shader prefilterShader("../shaders/PBR/IBL/cubemap.vs", "../shaders/PBR/IBL/prefilter.fs");
    Shader brdfShader("../shaders/PBR/IBL/brdf.vs", "../shaders/PBR/IBL/brdf.fs");
    Shader backgroundShader("../shaders/PBR/IBL/background.vs", "../shaders/PBR/IBL/background.fs");

    Model gunModel("../models/Cerberus_by_Andrew_Maximov/Cerberus_LP.FBX");

    pbrShader.use();
    pbrShader.setInt("irradianceMap", 0);
    pbrShader.setInt("prefilterMap", 1);
    pbrShader.setInt("brdfLUT", 2);
    pbrShader.setInt("albedoMap", 3);
    pbrShader.setInt("normalMap", 4);
    pbrShader.setInt("metallicMap", 5);
    pbrShader.setInt("roughnessMap", 6);
    pbrShader.setInt("aoMap", 7);

    backgroundShader.use();
    backgroundShader.setInt("environmentMap", 0);

    unsigned int ironAlbedoMap = loadTexture("../Textures/rusted_iron/albedo.png");
    unsigned int ironNormalMap = loadTexture("../Textures/rusted_iron/normal.png");
    unsigned int ironMetallicMap = loadTexture("../Textures/rusted_iron/metallic.png");
    unsigned int ironRoughnessMap = loadTexture("../Textures/rusted_iron/roughness.png");
    unsigned int ironAOMap = loadTexture("../Textures/rusted_iron/ao.png");

    unsigned int goldAlbedoMap = loadTexture("../Textures/gold/albedo.png");
    unsigned int goldNormalMap = loadTexture("../Textures/gold/normal.png");
    unsigned int goldMetallicMap = loadTexture("../Textures/gold/metallic.png");
    unsigned int goldRoughnessMap = loadTexture("../Textures/gold/roughness.png");
    unsigned int goldAOMap = loadTexture("../Textures/gold/ao.png");

    unsigned int grassAlbedoMap = loadTexture("../Textures/grass/albedo.png");
    unsigned int grassNormalMap = loadTexture("../Textures/grass/normal.png");
    unsigned int grassMetallicMap = loadTexture("../Textures/grass/metallic.png");
    unsigned int grassRoughnessMap = loadTexture("../Textures/grass/roughness.png");
    unsigned int grassAOMap = loadTexture("../Textures/grass/ao.png");

    unsigned int plasticAlbedoMap = loadTexture("../Textures/plastic/albedo.png");
    unsigned int plasticNormalMap = loadTexture("../Textures/plastic/normal.png");
    unsigned int plasticMetallicMap = loadTexture("../Textures/plastic/metallic.png");
    unsigned int plasticRoughnessMap = loadTexture("../Textures/plastic/roughness.png");
    unsigned int plasticAOMap = loadTexture("../Textures/plastic/ao.png");

    unsigned int wallAlbedoMap = loadTexture("../Textures/wall/albedo.png");
    unsigned int wallNormalMap = loadTexture("../Textures/wall/normal.png");
    unsigned int wallMetallicMap = loadTexture("../Textures/wall/metallic.png");
    unsigned int wallRoughnessMap = loadTexture("../Textures/wall/roughness.png");
    unsigned int wallAOMap = loadTexture("../Textures/wall/ao.png");

    unsigned int modelAlbedoMap = loadTexture("../models/Cerberus_by_Andrew_Maximov/Textures/Cerberus_A.tga");
    unsigned int modelNormalMap = loadTexture("../models/Cerberus_by_Andrew_Maximov/Textures/Cerberus_N.tga");
    unsigned int modelMetallicMap = loadTexture("../models/Cerberus_by_Andrew_Maximov/Textures/Cerberus_M.tga");
    unsigned int modelRoughnessMap = loadTexture("../models/Cerberus_by_Andrew_Maximov/Textures/Cerberus_R.tga");
    unsigned int modelAOMap = loadTexture("../models/Cerberus_by_Andrew_Maximov/Textures/Raw/Cerberus_AO.tga");

    unsigned int yellowAlbedo = createSolidTexture(255, 255, 0);
    unsigned int defaultNormal = createSolidTexture(128, 128, 255);
    unsigned int defaultMetallic = createSolidTexture(0, 0, 0);
    unsigned int defaultRoughness = createSolidTexture(255, 255, 255);
    unsigned int defaultAO = createSolidTexture(255, 255, 255);

    unsigned int captureFBO = 0;
    unsigned int captureRBO = 0;
    glGenFramebuffers(1, &captureFBO);
    glGenRenderbuffers(1, &captureRBO);

    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 512, 512);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, captureRBO);

    stbi_set_flip_vertically_on_load(true);
    int width = 0;
    int height = 0;
    int nrComponents = 0;
    float *hdrData = stbi_loadf("../Textures/hdr/newport_loft.hdr", &width, &height, &nrComponents, 0);

    unsigned int hdrTexture = 0;
    glGenTextures(1, &hdrTexture);
    glBindTexture(GL_TEXTURE_2D, hdrTexture);
    if (hdrData)
    {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, hdrData);
        stbi_image_free(hdrData);
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    unsigned int envCubemap = 0;
    glGenTextures(1, &envCubemap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap);
    for (unsigned int i = 0; i < 6; ++i)
    {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F, 512, 512, 0, GL_RGB, GL_FLOAT, nullptr);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glm::mat4 captureProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
    glm::mat4 captureViews[] = {
        glm::lookAt(glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, -1.0f, 0.0f))
    };

    equirectangularToCubemapShader.use();
    equirectangularToCubemapShader.setInt("equirectangularMap", 0);
    equirectangularToCubemapShader.setMat4("projection", captureProjection);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, hdrTexture);

    glViewport(0, 0, 512, 512);
    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    for (unsigned int i = 0; i < 6; ++i)
    {
        equirectangularToCubemapShader.setMat4("view", captureViews[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, envCubemap, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        renderCube();
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    unsigned int irradianceMap = 0;
    glGenTextures(1, &irradianceMap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, irradianceMap);
    for (unsigned int i = 0; i < 6; ++i)
    {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F, 32, 32, 0, GL_RGB, GL_FLOAT, nullptr);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 32, 32);

    irradianceShader.use();
    irradianceShader.setInt("environmentMap", 0);
    irradianceShader.setMat4("projection", captureProjection);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap);

    glViewport(0, 0, 32, 32);
    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    for (unsigned int i = 0; i < 6; ++i)
    {
        irradianceShader.setMat4("view", captureViews[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, irradianceMap,
                               0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        renderCube();
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    unsigned int prefilterMap = 0;
    glGenTextures(1, &prefilterMap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, prefilterMap);
    for (unsigned int i = 0; i < 6; ++i)
    {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F, 128, 128, 0, GL_RGB, GL_FLOAT, nullptr);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    prefilterShader.use();
    prefilterShader.setInt("environmentMap", 0);
    prefilterShader.setMat4("projection", captureProjection);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap);

    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    const unsigned int maxMipLevels = 5;
    for (unsigned int mip = 0; mip < maxMipLevels; ++mip)
    {
        unsigned int mipWidth = static_cast<unsigned int>(128 * std::pow(0.5, mip));
        unsigned int mipHeight = static_cast<unsigned int>(128 * std::pow(0.5, mip));
        glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mipWidth, mipHeight);
        glViewport(0, 0, mipWidth, mipHeight);

        float roughness = static_cast<float>(mip) / static_cast<float>(maxMipLevels - 1);
        prefilterShader.setFloat("roughness", roughness);

        for (unsigned int i = 0; i < 6; ++i)
        {
            prefilterShader.setMat4("view", captureViews[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i,
                                   prefilterMap, mip);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            renderCube();
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    unsigned int brdfLUTTexture = 0;
    glGenTextures(1, &brdfLUTTexture);
    glBindTexture(GL_TEXTURE_2D, brdfLUTTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, 512, 512, 0, GL_RG, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 512, 512);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, brdfLUTTexture, 0);

    glViewport(0, 0, 512, 512);
    brdfShader.use();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    renderQuad();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    int screenW = 0;
    int screenH = 0;
    glfwGetFramebufferSize(window, &screenW, &screenH);
    glViewport(0, 0, screenW, screenH);

    DemoClient client;
    if (!client.Connect("127.0.0.1", 60000))
    {
        std::cout << "Failed to connect server\n";
    }

    while (!glfwWindowShouldClose(window))
    {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Crosshair
        {
            ImDrawList *draw = ImGui::GetForegroundDrawList();

            ImVec2 center(ImGui::GetIO().DisplaySize.x * 0.5f,
                          ImGui::GetIO().DisplaySize.y * 0.5f);

            float halfLen = 6.0f; // 每条线的一半长度（像素）
            float gap = 3.0f; // 中间空隙（像素）
            float thick = 2.0f; // 线宽

            ImU32 col = IM_COL32(255, 255, 255, 220); // 白色半透明

            // 横线（左右两段）
            draw->AddLine(ImVec2(center.x - gap - halfLen, center.y),
                          ImVec2(center.x - gap, center.y), col, thick);
            draw->AddLine(ImVec2(center.x + gap, center.y),
                          ImVec2(center.x + gap + halfLen, center.y), col, thick);

            // 竖线（上下两段）
            draw->AddLine(ImVec2(center.x, center.y - gap - halfLen),
                          ImVec2(center.x, center.y - gap), col, thick);
            draw->AddLine(ImVec2(center.x, center.y + gap),
                          ImVec2(center.x, center.y + gap + halfLen), col, thick);

            // 中心点
            draw->AddCircleFilled(center, 1.5f, col);
        }

        if (client.IsConnected())
        {
            HandleIncoming(client);
        }

        // ImGui window: Stats
        {
            ImGui::SetNextWindowSize(ImVec2(200, 200), ImGuiCond_FirstUseEver);

            ImGui::Begin("Online Stats");
            ImGui::Text("Connected: %s", client.IsConnected() ? "Yes" : "No");
            ImGui::Text("Accepted: %s", g_acceptReceived ? "Yes" : "No");
            ImGui::Text("LocalID: %u", g_localPlayerID);
            ImGui::Text("Players: %d", static_cast<int>(g_players.size()));
            ImGui::Text("DynamicObjects: %d", static_cast<int>(g_dynamicObjects.size()));

            static float timeAccumulator = 0.0f;
            static int frameCount = 0;
            static float displayedFPS = 0.0f;
            static float displayedFrameTime = 0.0f;

            timeAccumulator += deltaTime;
            frameCount++;

            // 每 0.5 秒更新一次 FPS
            if (timeAccumulator >= 0.5f)
            {
                displayedFPS = frameCount / timeAccumulator;
                displayedFrameTime = (displayedFPS > 0.0f)
                                         ? (1000.0f / displayedFPS)
                                         : 0.0f;

                timeAccumulator = 0.0f;
                frameCount = 0;
            }

            ImGui::Text("FPS: %.1f", displayedFPS);
            ImGui::Text("Frame time: %.3f ms", displayedFrameTime);

            ImGui::End();
        }

        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        sPlayerInput input{};
        input.nUniqueID = g_localPlayerID;
        BuildInput(window, input);

        g_fireCooldown = std::max(0.0f, g_fireCooldown - deltaTime);
        const bool fireNow = (input.shooting != 0u);
        const bool fireTriggered = fireNow && !g_firePressedLast;
        g_firePressedLast = fireNow;
        if (fireTriggered && g_fireCooldown <= 0.0f)
        {
            SpawnLocal_BulletTrace();
            g_fireCooldown = 1.0f / g_fireRate;
        }

        if (client.IsConnected() && g_acceptReceived)
        {
            client.SendInput(input);
        }

        auto localIt = g_players.find(g_localPlayerID);
        if (localIt != g_players.end())
        {
            const sPlayerDescription &self = localIt->second.desc;
            camera.Position = glm::vec3(toFloat(self.x), toFloat(self.y) + g_playerEyeHeight, toFloat(self.z));
        }

        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::mat4 projection = glm::perspective(glm::radians(camera.Zoom),
                                                static_cast<float>(SCR_WIDTH) / static_cast<float>(SCR_HEIGHT), 0.1f,
                                                1000.0f);
        glm::mat4 view = camera.GetViewMatrix();

        pbrShader.use();
        pbrShader.setMat4("projection", projection);
        pbrShader.setMat4("view", view);
        pbrShader.setVec3("camPos", camera.Position);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, irradianceMap);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_CUBE_MAP, prefilterMap);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, brdfLUTTexture);

        // floor + walls
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, wallAlbedoMap);
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, wallNormalMap);
        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_2D, wallMetallicMap);
        glActiveTexture(GL_TEXTURE6);
        glBindTexture(GL_TEXTURE_2D, wallRoughnessMap);
        glActiveTexture(GL_TEXTURE7);
        glBindTexture(GL_TEXTURE_2D, wallAOMap);

        glm::mat4 floorModel(1.0f);
        floorModel = glm::translate(floorModel, glm::vec3(0.0f, -10.0f, 0.0f));
        pbrShader.setMat4("model", floorModel);
        pbrShader.setMat3("normalMatrix", glm::transpose(glm::inverse(glm::mat3(floorModel))));
        renderPlane(g_floorSize, g_floorUVScale);

        TextureSet textures[] = {
            {ironAlbedoMap, ironNormalMap, ironMetallicMap, ironRoughnessMap, ironAOMap},
            {goldAlbedoMap, goldNormalMap, goldMetallicMap, goldRoughnessMap, goldAOMap},
            {grassAlbedoMap, grassNormalMap, grassMetallicMap, grassRoughnessMap, grassAOMap},
            {plasticAlbedoMap, plasticNormalMap, plasticMetallicMap, plasticRoughnessMap, plasticAOMap},
            {wallAlbedoMap, wallNormalMap, wallMetallicMap, wallRoughnessMap, wallAOMap}
        };

        auto bindTextureSet = [](const TextureSet &set)
        {
            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, set.albedo);
            glActiveTexture(GL_TEXTURE4);
            glBindTexture(GL_TEXTURE_2D, set.normal);
            glActiveTexture(GL_TEXTURE5);
            glBindTexture(GL_TEXTURE_2D, set.metallic);
            glActiveTexture(GL_TEXTURE6);
            glBindTexture(GL_TEXTURE_2D, set.roughness);
            glActiveTexture(GL_TEXTURE7);
            glBindTexture(GL_TEXTURE_2D, set.ao);
        };

        // Dynamic objects from server (shape/material logic aligned with demo_offline)
        for (const auto &kv: g_dynamicObjects)
        {
            const sDynamicObjectDescription &desc = kv.second.desc;

            int matIdx = desc.materialIndex;
            if (matIdx < 0 || matIdx >= 5)
            {
                matIdx = 0;
            }
            bindTextureSet(textures[matIdx]);

            glm::vec3 pos(toFloat(desc.x), toFloat(desc.y), toFloat(desc.z));
            glm::quat q(toFloat(desc.qw), toFloat(desc.qx), toFloat(desc.qy), toFloat(desc.qz));

            glm::mat4 model(1.0f);
            model = glm::translate(model, pos);
            model *= glm::mat4_cast(q);

            pbrShader.setMat4("model", model);
            pbrShader.setMat3("normalMatrix", glm::transpose(glm::inverse(glm::mat3(model))));

            if (desc.shapeType == static_cast<uint8_t>(DynamicObjectShape::Sphere))
            {
                renderSphere();
            } else
            {
                renderCube();
            }
        }

        for (const auto &kv: g_players)
        {
            const uint32_t id = kv.first;
            const sPlayerDescription &desc = kv.second.desc;

            // First-person camera: do not draw local player body.
            if (id == g_localPlayerID)
            {
                continue;
            }

            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, ironAlbedoMap);
            glActiveTexture(GL_TEXTURE4);
            glBindTexture(GL_TEXTURE_2D, ironNormalMap);
            glActiveTexture(GL_TEXTURE5);
            glBindTexture(GL_TEXTURE_2D, ironMetallicMap);
            glActiveTexture(GL_TEXTURE6);
            glBindTexture(GL_TEXTURE_2D, ironRoughnessMap);
            glActiveTexture(GL_TEXTURE7);
            glBindTexture(GL_TEXTURE_2D, ironAOMap);

            glm::vec3 pos(toFloat(desc.x), toFloat(desc.y), toFloat(desc.z));
            glm::quat q(toFloat(desc.qw), toFloat(desc.qx), toFloat(desc.qy), toFloat(desc.qz));

            glm::mat4 model(1.0f);
            model = glm::translate(model, pos);
            model *= glm::mat4_cast(q);
            model = glm::scale(model, glm::vec3(2.0f, 2.7f, 2.0f));

            pbrShader.setMat4("model", model);
            pbrShader.setMat3("normalMatrix", glm::transpose(glm::inverse(glm::mat3(model))));
            renderSphere();
        }

        // render bullets
        for (auto it = g_bullets.begin(); it != g_bullets.end();)
        {
            it->life -= deltaTime;
            if (it->life <= 0.0f)
            {
                it = g_bullets.erase(it);
            } else
            {
                it->position += it->velocity * deltaTime;
                ++it;
            }
        }
        if (!g_bullets.empty())
        {
            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, yellowAlbedo);
            glActiveTexture(GL_TEXTURE4);
            glBindTexture(GL_TEXTURE_2D, defaultNormal);
            glActiveTexture(GL_TEXTURE5);
            glBindTexture(GL_TEXTURE_2D, defaultMetallic);
            glActiveTexture(GL_TEXTURE6);
            glBindTexture(GL_TEXTURE_2D, defaultRoughness);
            glActiveTexture(GL_TEXTURE7);
            glBindTexture(GL_TEXTURE_2D, defaultAO);

            for (const auto &b: g_bullets)
            {
                glm::mat4 bulletModel(1.0f);
                bulletModel = glm::translate(bulletModel, b.position);

                if (glm::length(b.velocity) > 0.001f)
                {
                    glm::vec3 direction = glm::normalize(b.velocity);
                    glm::vec3 up(0.0f, 1.0f, 0.0f);
                    if (std::abs(glm::dot(direction, up)) > 0.99f)
                    {
                        up = glm::vec3(1.0f, 0.0f, 0.0f);
                    }

                    glm::mat4 look = glm::lookAt(glm::vec3(0.0f), direction, up);
                    bulletModel = bulletModel * glm::inverse(look);
                    bulletModel = glm::scale(bulletModel, glm::vec3(0.04f, 0.04f, 0.8f));
                }

                pbrShader.setMat4("model", bulletModel);
                pbrShader.setMat3("normalMatrix", glm::transpose(glm::inverse(glm::mat3(bulletModel))));
                renderCube();
            }
        }

        backgroundShader.use();
        backgroundShader.setMat4("projection", projection);
        backgroundShader.setMat4("view", view);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap);
        renderCube();

        // Weapon pass (same placement style as demo_offline)
        {
            pbrShader.use();
            pbrShader.setMat4("projection", projection);
            pbrShader.setMat4("view", view);
            pbrShader.setVec3("camPos", camera.Position);

            glm::mat4 weaponModel = glm::inverse(view);
            weaponModel = glm::translate(weaponModel, glm::vec3(0.08f, -0.07f, -0.37f));
            weaponModel = glm::scale(weaponModel, glm::vec3(0.0013f));
            weaponModel = glm::rotate(weaponModel, glm::radians(-90.0f), glm::vec3(1, 0, 0));
            weaponModel = glm::rotate(weaponModel, glm::radians(6.5f), glm::vec3(0, 0, 1));
            weaponModel = glm::rotate(weaponModel, glm::radians(10.0f), glm::vec3(1, 0, 1));

            renderModel(pbrShader, gunModel, weaponModel,
                        modelAlbedoMap, modelNormalMap, modelMetallicMap,
                        modelRoughnessMap, modelAOMap);
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    if (client.IsConnected())
    {
        client.SendUnregister();
        client.Disconnect();
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
