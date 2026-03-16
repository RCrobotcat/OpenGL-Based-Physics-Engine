#pragma once

#include <cstdint>

#include "Networking/Cpp-Game-Server/DeterministicFloat/glacier_float.h"

enum class DemoGameMsg : uint32_t {
    Server_GetPing,

    Client_Accepted,
    Client_AssignID,
    Client_RegisterWithServer,
    Client_UnregisterWithServer,

    Game_AddPlayer,
    Game_RemovePlayer,
    Game_UpdatePlayer,

    Game_UpdateDynamicObject,

    // Client sends control intent to server,
    // server remains authority over transforms
    Client_PlayerInput,
};

struct sPlayerDescription {
    uint32_t nUniqueID = 0;

    GFloat x, y, z; // position
    GFloat qx, qy, qz, qw; // orientation
};

// Shape tag used by Game_UpdateDynamicObject snapshots
enum class DynamicObjectShape : uint8_t {
    Sphere = 0,
    Cube = 1
};

struct sDynamicObjectDescription {
    uint32_t objectID = 0;
    uint8_t shapeType = static_cast<uint8_t>(DynamicObjectShape::Sphere);
    int32_t materialIndex = 0;

    GFloat x, y, z; // position
    GFloat qx, qy, qz, qw; // orientation
};

struct sPlayerInput {
    uint32_t nUniqueID = 0;

    // Movement intent in local XZ plane, expected range [-1, 1]
    GFloat moveX;
    GFloat moveZ;

    // Player facing yaw in radians
    GFloat yaw;

    // 1 if jump key is currently pressed, 0 otherwise
    uint8_t jumpPressed = 0;

    // 1 if player shoot, 0 otherwise
    uint8_t shooting = 0;
    GFloat firePosX, firePosY, firePosZ;
    GFloat dirX, dirY, dirZ;
};
