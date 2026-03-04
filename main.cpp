#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <iostream>
#include "Shader.h"

#define STB_IMAGE_IMPLEMENTATION
#include "Camera.h"
#include "Model.h"
#include "stb_image.h"

#include "DrawObjects.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_opengl3.h"

#include <Physics3D/math/linalg/vec.h>
#include <Physics3D/math/linalg/mat.h>
#include <Physics3D/part.h>
#include <Physics3D/world.h>
#include <Physics3D/worldIteration.h>
#include <Physics3D/geometry/shapeCreation.h>
#include <Physics3D/geometry/shapeLibrary.h>
#include <Physics3D/CollisionCast/collisionCast.h>
#include <Physics3D/threading/physicsThread.h>
#include <Physics3D/externalforces/directionalGravity.h>

#include "Physics3D/math/ray.h"
#include "utils.h"

using namespace P3D;

struct Bullet
{
    glm::vec3 position;
    glm::vec3 velocity;
    float life;
};

std::vector<Bullet> g_bullets;

// Custom Part to hold rendering info
class CustomPart : public Part
{
public:
    enum Type { SPHERE, CUBE, PLANE, WALL, MODEL };

    Type type;

    // For textured parts, we might want to store which texture set to use.
    // simpler for this demo: just an index
    int materialIndex;

    CustomPart(const Shape &shape, const GlobalCFrame &position, const PartProperties &properties, Type type,
               int materialIndex = 0)
        : Part(shape, position, properties), type(type), materialIndex(materialIndex)
    {
    }

    void applyImpulse(Vec3 relativeOrigin, Vec3 impulse)
    {
        constexpr double dt = 1.0 / 100.0; // 与 world 步长一致
        applyForce(relativeOrigin, impulse / dt);
    }
};

void mouse_callback(GLFWwindow *window, double xpos, double ypos);

void scroll_callback(GLFWwindow *window, double xoffset, double yoffset);

void updatePlayerController(GLFWwindow *window, World<CustomPart> &world, UpgradeableMutex &worldMutex);

bool isPlayerGrounded(World<CustomPart> &world, UpgradeableMutex &worldMutex);

void framebuffer_size_callback(GLFWwindow *window, int width, int height);

void processInput(GLFWwindow *window);

void shootRay(World<CustomPart> &world, UpgradeableMutex &worldMutex);

const unsigned int SCR_WIDTH = 1920;
const unsigned int SCR_HEIGHT = 1080;

// physics world
World<CustomPart> *g_world = nullptr;
UpgradeableMutex *g_worldMutex = nullptr;
CustomPart *g_playerPart = nullptr;

// Camera
Camera camera(glm::vec3(-22, 5.0f, 35.0f));
float lastX = SCR_WIDTH / 2.0f;
float lastY = SCR_HEIGHT / 2.0f;
bool firstMouse = true;

float deltaTime = 0.0f; // 当前帧与上一帧的时间差
float lastFrame = 0.0f; // 上一帧的时间

// Floor
float floorSize = 100.0f;
float floorUVScale = 25.0f; // Tiling factor for floor texture

// Shooting
bool g_firePressedLast = false;
float g_fireCooldown = 0.0f;
float g_fireRate = 10.0f; // bullets per second
float g_fireRange = 100.0f; // ray length
float g_fireImpulse = 150.0f; // push strength

// FPS controller params
float g_playerHeight = 5.0f;
float g_playerRadius = 1.0f;
float g_playerMoveSpeed = 14.0f;
float g_playerJumpSpeed = 5.0f;
float g_playerEyeHeight = 0.75f;
bool g_jumpPressedLast = false;

int main()
{
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow *window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "PhysicsEngineDemo", NULL, NULL);
    if (window == NULL)
    {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetScrollCallback(window, scroll_callback);

    if (!gladLoadGLLoader((GLADloadproc) glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetCursorPosCallback(window, mouse_callback);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    (void) io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad; // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // ImGui::GetStyle().WindowMinSize = ImVec2(350, 150);
    // ImGui::GetStyle().FontSizeBase = 30.0f;

    // Setup Platform/Renderer backends
    const char *glsl_version = "#version 330";
    ImGui_ImplGlfw_InitForOpenGL(window, true);
#ifdef __EMSCRIPTEN__
    ImGui_ImplGlfw_InstallEmscriptenCallbacks(window, "#canvas");
#endif
    ImGui_ImplOpenGL3_Init(glsl_version);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL); // set depth function to less than AND equal for skybox depth trick.

    // shaders
    Shader pbrShader("../shaders/PBR/IBL/pbr.vs", "../shaders/PBR/IBL/pbr.fs");
    Shader equirectangularToCubemapShader("../shaders/PBR/IBL/cubemap.vs",
                                          "../shaders/PBR/IBL/equirectangular_to_cubemap.fs");

    // diffuse
    Shader irradianceShader("../shaders/PBR/IBL/cubemap.vs",
                            "../shaders/PBR/IBL/irradiance_convolution.fs");
    // specular
    Shader prefilterShader("../shaders/PBR/IBL/cubemap.vs",
                           "../shaders/PBR/IBL/prefilter.fs");
    Shader brdfShader("../shaders/PBR/IBL/brdf.vs",
                      "../shaders/PBR/IBL/brdf.fs"); // use to precompute the BRDF look-up texture

    Shader backgroundShader("../shaders/PBR/IBL/background.vs", "../shaders/PBR/IBL/background.fs");

    // model
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

    // load PBR material textures
    // --------------------------
    // rusted iron
    unsigned int ironAlbedoMap = loadTexture("../Textures/rusted_iron/albedo.png");
    unsigned int ironNormalMap = loadTexture("../Textures/rusted_iron/normal.png");
    unsigned int ironMetallicMap = loadTexture("../Textures/rusted_iron/metallic.png");
    unsigned int ironRoughnessMap = loadTexture("../Textures/rusted_iron/roughness.png");
    unsigned int ironAOMap = loadTexture("../Textures/rusted_iron/ao.png");

    // gold
    unsigned int goldAlbedoMap = loadTexture("../Textures/gold/albedo.png");
    unsigned int goldNormalMap = loadTexture("../Textures/gold/normal.png");
    unsigned int goldMetallicMap = loadTexture("../Textures/gold/metallic.png");
    unsigned int goldRoughnessMap = loadTexture("../Textures/gold/roughness.png");
    unsigned int goldAOMap = loadTexture("../Textures/gold/ao.png");

    // grass
    unsigned int grassAlbedoMap = loadTexture("../Textures/grass/albedo.png");
    unsigned int grassNormalMap = loadTexture("../Textures/grass/normal.png");
    unsigned int grassMetallicMap = loadTexture("../Textures/grass/metallic.png");
    unsigned int grassRoughnessMap = loadTexture("../Textures/grass/roughness.png");
    unsigned int grassAOMap = loadTexture("../Textures/grass/ao.png");

    // plastic
    unsigned int plasticAlbedoMap = loadTexture("../Textures/plastic/albedo.png");
    unsigned int plasticNormalMap = loadTexture("../Textures/plastic/normal.png");
    unsigned int plasticMetallicMap = loadTexture("../Textures/plastic/metallic.png");
    unsigned int plasticRoughnessMap = loadTexture("../Textures/plastic/roughness.png");
    unsigned int plasticAOMap = loadTexture("../Textures/plastic/ao.png");

    // wall
    unsigned int wallAlbedoMap = loadTexture("../Textures/wall/albedo.png");
    unsigned int wallNormalMap = loadTexture("../Textures/wall/normal.png");
    unsigned int wallMetallicMap = loadTexture("../Textures/wall/metallic.png");
    unsigned int wallRoughnessMap = loadTexture("../Textures/wall/roughness.png");
    unsigned int wallAOMap = loadTexture("../Textures/wall/ao.png");

    // model
    unsigned int modelAlbedoMap = loadTexture(
        "../models/Cerberus_by_Andrew_Maximov/Textures/Cerberus_A.tga");
    unsigned int modelNormalMap = loadTexture(
        "../models/Cerberus_by_Andrew_Maximov/Textures/Cerberus_N.tga");
    unsigned int modelMetallicMap = loadTexture(
        "../models/Cerberus_by_Andrew_Maximov/Textures/Cerberus_M.tga");
    unsigned int modelRoughnessMap = loadTexture(
        "../models/Cerberus_by_Andrew_Maximov/Textures/Cerberus_R.tga");
    unsigned int modelAOMap = loadTexture(
        "../models/Cerberus_by_Andrew_Maximov/Textures/Raw/Cerberus_AO.tga");

    // Colors for bullets
    unsigned int yellowAlbedo = createSolidTexture(255, 255, 0);
    unsigned int defaultNormal = createSolidTexture(128, 128, 255);
    unsigned int defaultMetallic = createSolidTexture(0, 0, 0);
    unsigned int defaultRoughness = createSolidTexture(255, 255, 255);
    unsigned int defaultAO = createSolidTexture(255, 255, 255);

    // Physics World Setup
    // -------------------
    World<CustomPart> world(1.0 / 100.0);
    UpgradeableMutex worldMutex;
    g_world = &world;
    g_worldMutex = &worldMutex;

    PartProperties basicProperties;
    basicProperties.density = 15.0;
    basicProperties.friction = 2.0;
    basicProperties.bouncyness = 0.1;

    // Arena walls
    const double wallHeight = 50.0;
    const double wallThickness = 1.0;

    const double floorTopY = -10.0;
    const double wallCenterY = floorTopY + wallHeight * 0.5;

    const double halfExtent = floorSize;
    const double wallOffset = halfExtent + wallThickness * 0.5;

    // Left wall (x-)
    std::unique_ptr<CustomPart> wallLeft = std::make_unique<CustomPart>(
        boxShape(wallThickness, wallHeight, floorSize * 2.0),
        GlobalCFrame(-wallOffset, wallCenterY, 0.0),
        basicProperties,
        CustomPart::WALL,
        4);
    world.addTerrainPart(wallLeft.get());

    // Right wall (x+)
    std::unique_ptr<CustomPart> wallRight = std::make_unique<CustomPart>(
        boxShape(wallThickness, wallHeight, floorSize * 2.0),
        GlobalCFrame(wallOffset, wallCenterY, 0.0),
        basicProperties,
        CustomPart::WALL,
        4);
    world.addTerrainPart(wallRight.get());

    // Back wall (z-)
    std::unique_ptr<CustomPart> wallBack = std::make_unique<CustomPart>(
        boxShape(floorSize * 2.0, wallHeight, wallThickness),
        GlobalCFrame(0.0, wallCenterY, -wallOffset),
        basicProperties,
        CustomPart::WALL,
        4);
    world.addTerrainPart(wallBack.get());

    // Front wall (z+)
    std::unique_ptr<CustomPart> wallFront = std::make_unique<CustomPart>(
        boxShape(floorSize * 2.0, wallHeight, wallThickness),
        GlobalCFrame(0.0, wallCenterY, wallOffset),
        basicProperties,
        CustomPart::WALL,
        4);
    world.addTerrainPart(wallFront.get());

    // Floor
    std::unique_ptr<CustomPart> floor = std::make_unique<CustomPart>(
        boxShape(floorSize * 2, 1.0, floorSize * 2),
        GlobalCFrame(0.0, -10.0 - 0.5, 0.0),
        // -0.5 to align top surface to -10.0 (renderPlane is at y=0 local, so -10 global)
        basicProperties,
        CustomPart::PLANE,
        4 // Wall texture index
    );
    world.addTerrainPart(floor.get());

    // Spheres
    // Positions from original code: (-5, 0, 2), (-3, 0, 2), (-1, 0, 2), (1, 0, 2), (3, 0, 2)
    struct SphereInit
    {
        double x, y, z;
        int materialIndex;
    };
    std::vector<SphereInit> sphereInits = {
        {-5.0, 10.0, 2.0, 0}, // Iron
        {-3.0, 12.0, 2.0, 1}, // Gold
        {-1.0, 14.0, 2.0, 2}, // Grass
        {1.0, 16.0, 2.0, 3}, // Plastic
        {3.0, 18.0, 2.0, 1} // Gold
    };

    struct BoxInit
    {
        double x, y, z;
        int materialIndex;
    };
    std::vector<BoxInit> boxInits = {
        {-4.0, 6.0, 5.0, 0}, // Iron
        {-2.0, 8.0, 5.0, 1}, // Gold
        {0.0, 10.0, 5.0, 2}, // Grass
        {2.0, 12.0, 5.0, 3}, // Plastic
        {4.0, 14.0, 5.0, 1} // Gold
    };

    std::vector<std::unique_ptr<CustomPart>> parts;

    // add spheres
    for (const auto &s: sphereInits)
    {
        auto part = std::make_unique<CustomPart>(
            sphereShape(1.0),
            GlobalCFrame(s.x, s.y, s.z),
            basicProperties,
            CustomPart::SPHERE,
            s.materialIndex
        );
        world.addPart(part.get());
        parts.push_back(std::move(part));
    }

    // add boxes
    for (const auto &b: boxInits)
    {
        auto part = std::make_unique<CustomPart>(
            boxShape(2.0, 2.0, 2.0),
            GlobalCFrame(b.x, b.y, b.z),
            basicProperties,
            CustomPart::CUBE,
            b.materialIndex
        );
        world.addPart(part.get());
        parts.push_back(std::move(part));
    }

    // Player capsule
    PartProperties playerProperties;
    playerProperties.density = 20.0f;
    playerProperties.friction = 0.5f;
    playerProperties.bouncyness = 0.01f;
    auto playerPart = std::make_unique<CustomPart>(
        cylinderShape(g_playerRadius, g_playerHeight), // radius, height
        GlobalCFrame(camera.Position.x, camera.Position.y, camera.Position.z),
        playerProperties,
        CustomPart::MODEL,
        4
    );
    world.addPart(playerPart.get());
    g_playerPart = playerPart.get();
    parts.push_back(std::move(playerPart));

    // Gravity
    DirectionalGravity gravity(Vec3(0.0, -9.81, 0.0));
    world.addExternalForce(&gravity);

    // Physics Thread
    PhysicsThread physicsThread(&world, &worldMutex);
    physicsThread.start();

    // pbr: setup framebuffer
    // ----------------------
    unsigned int captureFBO;
    unsigned int captureRBO;
    glGenFramebuffers(1, &captureFBO);
    glGenRenderbuffers(1, &captureRBO);

    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 512, 512);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, captureRBO);

    // pbr: load the HDR environment map
    // ---------------------------------
    stbi_set_flip_vertically_on_load(true);
    int width, height, nrComponents;
    float *data = stbi_loadf("../Textures/hdr/newport_loft.hdr",
                             &width, &height, &nrComponents, 0);
    unsigned int hdrTexture;
    if (data)
    {
        glGenTextures(1, &hdrTexture);
        glBindTexture(GL_TEXTURE_2D, hdrTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT,
                     data); // note how we specify the texture's data value to be float

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        stbi_image_free(data);
    } else
    {
        std::cout << "Failed to load HDR image." << std::endl;
    }

    // pbr: setup cubemap to render to and attach to framebuffer
    // ---------------------------------------------------------
    unsigned int envCubemap;
    glGenTextures(1, &envCubemap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap);
    for (unsigned int i = 0; i < 6; ++i)
    {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F, 512, 512, 0, GL_RGB, GL_FLOAT, nullptr);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER,
                    GL_LINEAR_MIPMAP_LINEAR); // enable pre-filter mipmap sampling (combatting visible dots artifact)
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // pbr: set up projection and view matrices for capturing data onto the 6 cubemap face directions
    // ----------------------------------------------------------------------------------------------
    glm::mat4 captureProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
    glm::mat4 captureViews[] =
    {
        glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
        glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(-1.0f, 0.0f, 0.0f),
                    glm::vec3(0.0f, -1.0f, 0.0f)),
        glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
        glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f),
                    glm::vec3(0.0f, 0.0f, -1.0f)),
        glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
        glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, -1.0f, 0.0f))
    };

    // pbr: convert HDR equirectangular environment map to cubemap equivalent
    // ----------------------------------------------------------------------
    equirectangularToCubemapShader.use();
    equirectangularToCubemapShader.setInt("equirectangularMap", 0);
    equirectangularToCubemapShader.setMat4("projection", captureProjection);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, hdrTexture);

    glViewport(0, 0, 512, 512); // don't forget to configure the viewport to the capture dimensions.
    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    for (unsigned int i = 0; i < 6; ++i)
    {
        equirectangularToCubemapShader.setMat4("view", captureViews[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, envCubemap, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        renderCube();
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // then let OpenGL generate mipmaps from first mip face (combatting visible dots artifact)
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    // pbr: create an irradiance cubemap, and re-scale capture FBO to irradiance scale.
    // --------------------------------------------------------------------------------
    unsigned int irradianceMap;
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

    // pbr: solve diffuse integral by convolution to create an irradiance (cube)map.
    // -----------------------------------------------------------------------------
    irradianceShader.use();
    irradianceShader.setInt("environmentMap", 0);
    irradianceShader.setMat4("projection", captureProjection);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap);

    glViewport(0, 0, 32, 32); // don't forget to configure the viewport to the capture dimensions.
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

    // pbr: create a pre-filter cubemap, and re-scale capture FBO to pre-filter scale.
    // --------------------------------------------------------------------------------
    unsigned int prefilterMap;
    glGenTextures(1, &prefilterMap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, prefilterMap);
    for (unsigned int i = 0; i < 6; ++i)
    {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F, 128, 128, 0, GL_RGB, GL_FLOAT, nullptr);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER,
                    GL_LINEAR_MIPMAP_LINEAR); // be sure to set minification filter to mip_linear
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // generate mipmaps for the cubemap so OpenGL automatically allocates the required memory.
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    // pbr: run a quasi monte-carlo simulation on the environment lighting to create a prefilter (cube)map.
    // ----------------------------------------------------------------------------------------------------
    prefilterShader.use();
    prefilterShader.setInt("environmentMap", 0);
    prefilterShader.setMat4("projection", captureProjection);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap);

    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    unsigned int maxMipLevels = 5;
    for (unsigned int mip = 0; mip < maxMipLevels; ++mip)
    {
        // reisze framebuffer according to mip-level size.
        unsigned int mipWidth = static_cast<unsigned int>(128 * std::pow(0.5, mip));
        unsigned int mipHeight = static_cast<unsigned int>(128 * std::pow(0.5, mip));
        glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mipWidth, mipHeight);
        glViewport(0, 0, mipWidth, mipHeight);

        float roughness = (float) mip / (float) (maxMipLevels - 1);
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

    // pbr: generate a 2D LUT from the BRDF equations used.
    // ----------------------------------------------------
    unsigned int brdfLUTTexture;
    glGenTextures(1, &brdfLUTTexture);

    // pre-allocate enough memory for the LUT texture.
    glBindTexture(GL_TEXTURE_2D, brdfLUTTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, 512, 512, 0, GL_RG, GL_FLOAT, 0);
    // be sure to set wrapping mode to GL_CLAMP_TO_EDGE
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // then re-configure capture framebuffer object and render screen-space quad with BRDF shader.
    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 512, 512);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, brdfLUTTexture, 0);

    glViewport(0, 0, 512, 512);
    brdfShader.use();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    renderQuad();

    glBindFramebuffer(GL_FRAMEBUFFER, 0);


    // initialize static shader uniforms before rendering
    // --------------------------------------------------
    glm::mat4 projection = glm::perspective(glm::radians(camera.Zoom), (float) SCR_WIDTH / (float) SCR_HEIGHT, 0.1f,
                                            1000.0f);
    pbrShader.use();
    pbrShader.setMat4("projection", projection);
    backgroundShader.use();
    backgroundShader.setMat4("projection", projection);

    glm::mat4 weaponProjection = glm::perspective(glm::radians(60.0f), (float) SCR_WIDTH / (float) SCR_HEIGHT,
                                                  0.01f, 1000.0f);

    // then before rendering, configure the viewport to the original framebuffer's screen dimensions
    int scrWidth, scrHeight;
    glfwGetFramebufferSize(window, &scrWidth, &scrHeight);
    glViewport(0, 0, scrWidth, scrHeight);

    while (!glfwWindowShouldClose(window))
    {
        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        // ImGui::ShowDemoWindow(); // Show demo window! :)

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

        // ImGui window: Stats
        {
            ImGui::Begin("Stats");

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
        g_fireCooldown = std::max(0.0f, g_fireCooldown - deltaTime);
        lastFrame = currentFrame;

        processInput(window);

        updatePlayerController(window, world, worldMutex);
        if (g_playerPart)
        {
            std::shared_lock<UpgradeableMutex> lock(worldMutex);
            Vec3 p = castPositionToVec3(g_playerPart->getPosition());
            camera.Position = glm::vec3((float) p.x, (float) (p.y + g_playerEyeHeight * g_playerHeight), (float) p.z);
        }

        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // render scene, supplying the convoluted irradiance map to the final shader.
        // ------------------------------------------------------------------------------------------
        pbrShader.use();
        glm::mat4 model = glm::mat4(1.0f);
        glm::mat4 view = camera.GetViewMatrix();
        pbrShader.setMat4("view", view);
        pbrShader.setVec3("camPos", camera.Position);

        // bind pre-computed IBL data
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, irradianceMap);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_CUBE_MAP, prefilterMap);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, brdfLUTTexture);

        // Texture Bindings Array
        struct TextureSet
        {
            unsigned int albedo, normal, metallic, roughness, ao;
        };
        TextureSet textures[] = {
            {ironAlbedoMap, ironNormalMap, ironMetallicMap, ironRoughnessMap, ironAOMap}, // 0
            {goldAlbedoMap, goldNormalMap, goldMetallicMap, goldRoughnessMap, goldAOMap}, // 1
            {grassAlbedoMap, grassNormalMap, grassMetallicMap, grassRoughnessMap, grassAOMap}, // 2
            {plasticAlbedoMap, plasticNormalMap, plasticMetallicMap, plasticRoughnessMap, plasticAOMap}, // 3
            {wallAlbedoMap, wallNormalMap, wallMetallicMap, wallRoughnessMap, wallAOMap} // 4
        };

        // Sync with physics world
        {
            std::shared_lock<UpgradeableMutex> lock(worldMutex);
            world.forEachPart([&](const CustomPart &part)
            {
                // Determine textures
                int matIdx = part.materialIndex;

                if (matIdx >= 0 && matIdx < 5 && part.type != CustomPart::MODEL)
                {
                    glActiveTexture(GL_TEXTURE3);
                    glBindTexture(GL_TEXTURE_2D, textures[matIdx].albedo);
                    glActiveTexture(GL_TEXTURE4);
                    glBindTexture(GL_TEXTURE_2D, textures[matIdx].normal);
                    glActiveTexture(GL_TEXTURE5);
                    glBindTexture(GL_TEXTURE_2D, textures[matIdx].metallic);
                    glActiveTexture(GL_TEXTURE6);
                    glBindTexture(GL_TEXTURE_2D, textures[matIdx].roughness);
                    glActiveTexture(GL_TEXTURE7);
                    glBindTexture(GL_TEXTURE_2D, textures[matIdx].ao);
                }
                // else if (part.type == CustomPart::MODEL)
                // {
                //     // Gun textures
                //     glActiveTexture(GL_TEXTURE3);
                //     glBindTexture(GL_TEXTURE_2D, modelAlbedoMap);
                //     glActiveTexture(GL_TEXTURE4);
                //     glBindTexture(GL_TEXTURE_2D, modelNormalMap);
                //     glActiveTexture(GL_TEXTURE5);
                //     glBindTexture(GL_TEXTURE_2D, modelMetallicMap);
                //     glActiveTexture(GL_TEXTURE6);
                //     glBindTexture(GL_TEXTURE_2D, modelRoughnessMap);
                //     glActiveTexture(GL_TEXTURE7);
                //     glBindTexture(GL_TEXTURE_2D, modelAOMap);
                // }

                // Transform
                glm::mat4 m = toGlm(part.getCFrame().asMat4WithPreScale(part.hitbox.scale));

                // Render
                if (part.type == CustomPart::SPHERE)
                {
                    pbrShader.setMat4("model", m);
                    pbrShader.setMat3("normalMatrix", glm::transpose(glm::inverse(glm::mat3(m))));
                    renderSphere();
                } else if (part.type == CustomPart::PLANE)
                {
                    glm::mat4 mNoScale = toGlm(part.getCFrame().asMat4());
                    pbrShader.setMat4("model", mNoScale);
                    pbrShader.setMat3("normalMatrix", glm::transpose(glm::inverse(glm::mat3(mNoScale))));
                    renderPlane(floorSize, floorUVScale);
                } else if (part.type == CustomPart::WALL)
                {
                    // pbrShader.setMat4("model", m);
                    // pbrShader.setMat3("normalMatrix", glm::transpose(glm::inverse(glm::mat3(m))));
                    // renderCube();
                } else if (part.type == CustomPart::CUBE)
                {
                    pbrShader.setMat4("model", m);
                    pbrShader.setMat3("normalMatrix", glm::transpose(glm::inverse(glm::mat3(m))));
                    renderCube();
                }
                // else if (part.type == CustomPart::MODEL)
                // {
                //
                // }
            });
        }

        // Update and Render Bullets
        pbrShader.use();
        for (auto it = g_bullets.begin(); it != g_bullets.end();)
        {
            it->life -= deltaTime;
            if (it->life <= 0)
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
                glm::mat4 model = glm::mat4(1.0f);
                model = glm::translate(model, b.position);

                if (glm::length(b.velocity) > 0.001f)
                {
                    glm::vec3 direction = glm::normalize(b.velocity);
                    glm::vec3 up = glm::vec3(0, 1, 0);
                    if (abs(glm::dot(direction, up)) > 0.99f) up = glm::vec3(1, 0, 0);

                    // Create a lookAt matrix to orient the bullet in the direction of velocity
                    glm::mat4 look = glm::lookAt(glm::vec3(0), direction, up);
                    model = model * glm::inverse(look);

                    // Scale along Z to make it long
                    model = glm::scale(model, glm::vec3(0.04f, 0.04f, 0.8f));
                }

                pbrShader.setMat4("model", model);
                pbrShader.setMat3("normalMatrix", glm::transpose(glm::inverse(glm::mat3(model))));
                renderCube();
            }
        }

        // render skybox (render as last to prevent overdraw)
        backgroundShader.use();
        backgroundShader.setMat4("view", view);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap);
        //glBindTexture(GL_TEXTURE_CUBE_MAP, irradianceMap); // display irradiance map
        //glBindTexture(GL_TEXTURE_CUBE_MAP, prefilterMap); // display prefilter map
        renderCube();

        // ---------- Weapon Pass ----------
        pbrShader.use();
        pbrShader.setMat4("projection", weaponProjection);
        pbrShader.setMat4("view", view);
        pbrShader.setVec3("camPos", camera.Position);

        glm::mat4 weaponModel = glm::inverse(view); // let gun model always in the camera view space
        weaponModel = glm::translate(weaponModel, glm::vec3(0.08f, -0.07f, -0.29f));
        weaponModel = glm::scale(weaponModel, glm::vec3(0.0013f));
        weaponModel = glm::rotate(weaponModel, glm::radians(-90.0f), glm::vec3(1, 0, 0));
        weaponModel = glm::rotate(weaponModel, glm::radians(6.5f), glm::vec3(0, 0, 1));
        weaponModel = glm::rotate(weaponModel, glm::radians(10.0f), glm::vec3(1, 0, 1));

        // render gun model
        renderModel(pbrShader, gunModel, weaponModel, modelAlbedoMap, modelNormalMap, modelMetallicMap,
                    modelRoughnessMap,
                    modelAOMap);

        // render BRDF map to screen
        //brdfShader.Use();
        //renderQuad();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    return 0;
}

void processInput(GLFWwindow *window)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    // float cameraSpeed = 5.0f * deltaTime; // adjust accordingly
    // if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) // w: forward
    //     camera.ProcessKeyboard(FORWARD, deltaTime);
    // if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) // s: backward
    //     camera.ProcessKeyboard(BACKWARD, deltaTime);
    // if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) // a: left
    //     camera.ProcessKeyboard(LEFT, deltaTime);
    // if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) // d: right
    //     camera.ProcessKeyboard(RIGHT, deltaTime);

    // Shooting => LMB
    bool fireNow = (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);

    // single shot on press
    bool fireTriggered = fireNow && !g_firePressedLast;
    g_firePressedLast = fireNow;

    if (fireTriggered && g_fireCooldown <= 0.0f)
    {
        shootRay(*g_world, *g_worldMutex);
        g_fireCooldown = 1.0f / g_fireRate;
    }
}

void mouse_callback(GLFWwindow *window, double xposIn, double yposIn)
{
    float xpos = static_cast<float>(xposIn);
    float ypos = static_cast<float>(yposIn);

    if (firstMouse)
    {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }

    float xoffset = xpos - lastX;
    float yoffset = lastY - ypos; // reversed since y-coordinates go from bottom to top

    lastX = xpos;
    lastY = ypos;

    camera.ProcessMouseMovement(xoffset, yoffset);
}

void scroll_callback(GLFWwindow *window, double xoffset, double yoffset)
{
    camera.ProcessMouseScroll(static_cast<float>(yoffset));
}

void framebuffer_size_callback(GLFWwindow *window, int width, int height)
{
    glViewport(0, 0, width, height);
}

bool isPlayerGrounded(World<CustomPart> &world, UpgradeableMutex &worldMutex)
{
    if (!g_playerPart) return false;

    Vec3 center = castPositionToVec3(g_playerPart->getPosition());
    const double capsuleHalfHeight = g_playerHeight * 0.5f;
    const double probeDistance = capsuleHalfHeight * 0.5f + 0.1f;

    Position origin(center.x, center.y, center.z);
    Ray downRay;
    downRay.origin = origin;
    downRay.direction = Vec3(0.0, -1.0, 0.0);

    RaycastResult<CustomPart> hit;
    bool ok = performRaycast(downRay, world, worldMutex, hit, probeDistance);
    if (!ok || hit.hitPart == nullptr) return false;

    // filter out self hits
    if (hit.hitPart == g_playerPart) return false;

    return true;
}

void updatePlayerController(GLFWwindow *window, World<CustomPart> &world, UpgradeableMutex &worldMutex)
{
    if (!g_playerPart) return;

    glm::vec3 forward = glm::normalize(glm::vec3(camera.Front.x, 0.0f, camera.Front.z));
    glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));

    glm::vec3 wish(0.0f);
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) wish += forward;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) wish -= forward;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) wish += right;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) wish -= right;

    if (glm::length(wish) > 1e-5f) wish = glm::normalize(wish);

    Vec3 curVelocity; {
        std::shared_lock<UpgradeableMutex> lock(worldMutex);
        curVelocity = g_playerPart->getVelocity();
    }

    Vec3 targetVel(
        (double) (wish.x * g_playerMoveSpeed),
        curVelocity.y,
        (double) (wish.z * g_playerMoveSpeed)
    );

    // jump
    bool jumpNow = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
    bool jumpTriggered = jumpNow && !g_jumpPressedLast;
    g_jumpPressedLast = jumpNow;
    if (jumpTriggered && isPlayerGrounded(world, worldMutex))
    {
        targetVel.y = g_playerJumpSpeed;
    }

    // sync velocity to physics world
    {
        std::unique_lock<UpgradeableMutex> lock(worldMutex);
        g_playerPart->setVelocity(targetVel);
    }
}

void shootRay(World<CustomPart> &world, UpgradeableMutex &worldMutex)
{
    // Ray in world space from camera center
    glm::vec3 origin = camera.Position;
    glm::vec3 dir = glm::normalize(camera.Front);

    // Convert to Physics3D vectors
    Vec3 rayOrigin(origin.x, origin.y, origin.z);
    Vec3 rayDir(dir.x, dir.y, dir.z);

    // Raycast in Physics World (read lock)
    std::shared_lock<UpgradeableMutex> lock(worldMutex);
    RaycastResult<CustomPart> hit;
    Position rayOriginPos(rayOrigin.x, rayOrigin.y, rayOrigin.z);
    Ray ray(rayOriginPos, rayDir);
    bool ok = performRaycast(ray, world, worldMutex, hit, static_cast<double>(g_fireRange));

    // bullet visual effect
    Vec3 hitPosV3 = rayOrigin + rayDir * (ok ? hit.distance : g_fireRange);
    glm::vec3 hitPoint(hitPosV3.x, hitPosV3.y, hitPosV3.z);

    glm::mat4 view = camera.GetViewMatrix();
    glm::mat4 invView = glm::inverse(view);
    glm::vec4 muzzleLocal(0.12f, -0.2f, -0.40f, 1.0f);
    glm::vec4 muzzleWorld = invView * muzzleLocal;
    glm::vec3 startPos = glm::vec3(muzzleWorld);

    glm::vec3 bulletDir = glm::normalize(hitPoint - startPos);

    Bullet b;
    b.position = startPos;
    b.velocity = bulletDir * 200.0f; // Fast speed
    b.life = 2.0f;
    g_bullets.push_back(b);

    if (!ok)
        return;

    // Apply impulse to the hit part (write lock required)
    lock.unlock();
    std::unique_lock<UpgradeableMutex> wlock(worldMutex);

    CustomPart *bestPart = hit.hitPart;
    double bestT = hit.distance;
    Vec3 hitPos = rayOrigin + rayDir * bestT;
    Vec3 impulse = rayDir * g_fireImpulse;
    try
    {
        if (bestPart->type != CustomPart::PLANE && bestPart->type != CustomPart::WALL)
        {
            Vec3 partCenter = castPositionToVec3(bestPart->getCFrame().position);
            bestPart->applyImpulse(hitPos - partCenter, impulse);
        }
    } catch (...)
    {
        // ignore
    }

    if (bestPart->type != CustomPart::PLANE)
        std::cout << "[Shoot] hit part type=" << (int) bestPart->type
                << " t=" << bestT
                << " pos=(" << (float) hitPos.x << "," << (float) hitPos.y << "," << (float) hitPos.z << ")\n";
}
