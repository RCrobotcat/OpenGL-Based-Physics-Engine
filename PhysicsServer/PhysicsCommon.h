#pragma once

#include "Networking/Cpp-Game-Server/DeterministicFloat/glacier_float.h"

enum class GameMsg : uint32_t {
    Server_GetStatus,
    Server_GetPing,

    Client_Accepted,
    Client_AssignID,
    Client_RegisterWithServer,
    Client_UnregisterWithServer,

    Game_AddPlayer,
    Game_RemovePlayer,
    Game_UpdatePlayer,
};

struct sPlayerDescription {
    uint32_t nUniqueID = 0;

    GFloat x, y, z; // position
    GFloat qx, qy, qz, qw; // orientation
};
