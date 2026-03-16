#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <thread>
#include <unordered_map>
#include <vector>

#include <rc_net.h>

#include <Physics3D/world.h>
#include <Physics3D/worldIteration.h>
#include <Physics3D/geometry/shapeCreation.h>
#include <Physics3D/externalforces/directionalGravity.h>

#include "PhysicsCommon.h"
#include "Physics3D/CollisionCast/collisionCast.h"

using namespace P3D;

class ServerEntityPart : public Part
{
public:
    enum Type { SPHERE, CUBE, PLANE, WALL, PLAYER };

    Type entityType;

    uint32_t playerID = 0;
    bool isPlayer = false;

    // Metadata for Game_UpdateDynamicObject snapshots
    uint32_t dynamicObjectID = 0;
    uint8_t dynamicShapeType = static_cast<uint8_t>(DynamicObjectShape::Sphere);
    int32_t materialIndex = 0;

    explicit ServerEntityPart(const Shape &shape,
                              const GlobalCFrame &position,
                              const PartProperties &properties,
                              Type type)
        : Part(shape, position, properties), entityType(type)
    {
    }

    void applyImpulse(Vec3 relativeOrigin, Vec3 impulse)
    {
        constexpr double dt = 1.0 / 100.0; // 与 world 步长一致
        applyForce(relativeOrigin, impulse / dt);
    }
};

class PhysicsServer : public RCNet::net::server_interface<DemoGameMsg>
{
public:
    explicit PhysicsServer(uint16_t nPort)
        : RCNet::net::server_interface<DemoGameMsg>(nPort),
          m_world(1.0 / 100.0),
          m_gravity(Vec3(0.0, -9.81, 0.0))
    {
        setupWorld();
    }

    void Tick(double frameDt)
    {
        m_simulationAccumulator += frameDt;
        while (m_simulationAccumulator >= m_world.deltaT)
        {
            applyInputToPlayers();
            m_world.tick();
            m_simulationAccumulator -= m_world.deltaT;
        }

        m_snapshotAccumulator += frameDt;
        if (m_snapshotAccumulator >= m_snapshotInterval)
        {
            broadcastPlayerSnapshots();
            broadcastDynamicObjectSnapshots();
            m_snapshotAccumulator = 0.0;
        }
    }

protected:
    bool OnClientConnect(std::shared_ptr<RCNet::net::connection<DemoGameMsg>> client) override
    {
        RCNet::net::message<DemoGameMsg> msg;
        msg.header.id = DemoGameMsg::Client_Accepted;
        client->Send(msg);
        return true;
    }

    void OnClientDisconnect(std::shared_ptr<RCNet::net::connection<DemoGameMsg>> client) override
    {
        if (!client)
        {
            return;
        }

        std::cout << "Removing client [" << client->GetID() << "]\n";
        removeClientPlayer(client->GetID(), true);
    }

    void OnMessage(std::shared_ptr<RCNet::net::connection<DemoGameMsg>> client,
                   RCNet::net::message<DemoGameMsg> &msg) override
    {
        if (!client)
        {
            return;
        }

        switch (msg.header.id)
        {
            case DemoGameMsg::Server_GetPing:
            {
                client->Send(msg);
                break;
            }

            case DemoGameMsg::Client_RegisterWithServer:
            {
                registerClientPlayer(client);
                break;
            }

            case DemoGameMsg::Client_UnregisterWithServer:
            {
                removeClientPlayer(client->GetID(), true);
                break;
            }

            case DemoGameMsg::Client_PlayerInput:
            case DemoGameMsg::Game_UpdatePlayer:
            {
                handleClientInput(client, msg);
                break;
            }

            default:
                break;
        }
    }

private:
    struct PlayerControlState
    {
        bool jumpPressedLast = false;
        bool shootPressedLast = false;
        double shootCooldown = 0.0;
    };

    static GFloat toGFloat(double v)
    {
        return GFloat::FromFloat(static_cast<float>(v));
    }

    static double toDouble(const GFloat &v)
    {
        return v.toDouble();
    }

    void setupWorld()
    {
        m_playerProperties.density = 20.0;
        m_playerProperties.friction = 0.7;
        m_playerProperties.bouncyness = 0.01;

        m_terrainProperties.density = 15.0;
        m_terrainProperties.friction = 1.8;
        m_terrainProperties.bouncyness = 0.05;

        m_dynamicObjectProperties.density = 15.0;
        m_dynamicObjectProperties.friction = 2.0;
        m_dynamicObjectProperties.bouncyness = 0.1;

        m_world.addExternalForce(&m_gravity);

        // static environment objects
        const double floorHalfSize = 100.0;
        const double floorTopY = -10.0;
        const double wallHeight = 40.0;
        const double wallThickness = 1.0;
        const double wallOffset = floorHalfSize + wallThickness * 0.5;
        const double wallCenterY = floorTopY + wallHeight * 0.5;

        auto floor = std::make_unique<ServerEntityPart>(
            boxShape(floorHalfSize * 2.0, 1.0, floorHalfSize * 2.0),
            GlobalCFrame(0.0, floorTopY - 0.5, 0.0),
            m_terrainProperties, ServerEntityPart::Type::PLANE);
        m_world.addTerrainPart(floor.get());
        m_staticTerrain.push_back(std::move(floor));

        auto wallLeft = std::make_unique<ServerEntityPart>(
            boxShape(wallThickness, wallHeight, floorHalfSize * 2.0),
            GlobalCFrame(-wallOffset, wallCenterY, 0.0),
            m_terrainProperties, ServerEntityPart::Type::WALL);
        m_world.addTerrainPart(wallLeft.get());
        m_staticTerrain.push_back(std::move(wallLeft));

        auto wallRight = std::make_unique<ServerEntityPart>(
            boxShape(wallThickness, wallHeight, floorHalfSize * 2.0),
            GlobalCFrame(wallOffset, wallCenterY, 0.0),
            m_terrainProperties, ServerEntityPart::Type::WALL);
        m_world.addTerrainPart(wallRight.get());
        m_staticTerrain.push_back(std::move(wallRight));

        auto wallBack = std::make_unique<ServerEntityPart>(
            boxShape(floorHalfSize * 2.0, wallHeight, wallThickness),
            GlobalCFrame(0.0, wallCenterY, -wallOffset),
            m_terrainProperties, ServerEntityPart::Type::WALL);
        m_world.addTerrainPart(wallBack.get());
        m_staticTerrain.push_back(std::move(wallBack));

        auto wallFront = std::make_unique<ServerEntityPart>(
            boxShape(floorHalfSize * 2.0, wallHeight, wallThickness),
            GlobalCFrame(0.0, wallCenterY, wallOffset),
            m_terrainProperties, ServerEntityPart::Type::WALL);
        m_world.addTerrainPart(wallFront.get());
        m_staticTerrain.push_back(std::move(wallFront));

        // dynamic objects
        struct SphereInit
        {
            double x, y, z;
            int materialIndex;
        };
        const std::vector<SphereInit> sphereInits = {
            {-5.0, 10.0, 2.0, 0},
            {-3.0, 12.0, 2.0, 1},
            {-1.0, 14.0, 2.0, 2},
            {1.0, 16.0, 2.0, 3},
            {3.0, 18.0, 2.0, 1}
        };

        struct BoxInit
        {
            double x, y, z;
            int materialIndex;
        };
        const std::vector<BoxInit> boxInits = {
            {-4.0, 6.0, 5.0, 0},
            {-2.0, 8.0, 5.0, 1},
            {0.0, 10.0, 5.0, 2},
            {2.0, 12.0, 5.0, 3},
            {4.0, 14.0, 5.0, 1}
        };

        for (const auto &s: sphereInits)
        {
            auto part = std::make_unique<ServerEntityPart>(
                sphereShape(1.0),
                GlobalCFrame(s.x, s.y, s.z),
                m_dynamicObjectProperties, ServerEntityPart::Type::SPHERE);
            part->dynamicObjectID = m_nextDynamicObjectID++;
            part->dynamicShapeType = static_cast<uint8_t>(DynamicObjectShape::Sphere);
            part->materialIndex = s.materialIndex;
            m_world.addPart(part.get());
            m_dynamicObjects.push_back(std::move(part));
        }

        for (const auto &b: boxInits)
        {
            auto part = std::make_unique<ServerEntityPart>(
                boxShape(2.0, 2.0, 2.0),
                GlobalCFrame(b.x, b.y, b.z),
                m_dynamicObjectProperties, ServerEntityPart::Type::CUBE);
            part->dynamicObjectID = m_nextDynamicObjectID++;
            part->dynamicShapeType = static_cast<uint8_t>(DynamicObjectShape::Cube);
            part->materialIndex = b.materialIndex;
            m_world.addPart(part.get());
            m_dynamicObjects.push_back(std::move(part));
        }
    }

    GlobalCFrame makeSpawnCFrame(uint32_t playerID) const
    {
        const double angle = static_cast<double>(playerID % 360) * (3.14159265358979323846 / 180.0);
        const double radius = 8.0;
        const double x = std::cos(angle) * radius;
        const double z = std::sin(angle) * radius;
        const Rotation facing = Rotation::rotY(angle + 3.14159265358979323846);
        return GlobalCFrame(x, 2.0, z, facing);
    }

    sPlayerDescription makeDescription(const ServerEntityPart &part) const
    {
        const Vec3 pos = castPositionToVec3(part.getPosition());
        const Quaternion<double> q = part.getCFrame().getRotation().asRotationQuaternion();

        sPlayerDescription desc;
        desc.nUniqueID = part.playerID;
        desc.x = toGFloat(pos.x);
        desc.y = toGFloat(pos.y);
        desc.z = toGFloat(pos.z);
        desc.qx = toGFloat(q.i);
        desc.qy = toGFloat(q.j);
        desc.qz = toGFloat(q.k);
        desc.qw = toGFloat(q.w);
        return desc;
    }

    sDynamicObjectDescription makeDynamicDescription(const ServerEntityPart &part) const
    {
        const Vec3 pos = castPositionToVec3(part.getPosition());
        const Quaternion<double> q = part.getCFrame().getRotation().asRotationQuaternion();

        sDynamicObjectDescription desc;
        desc.objectID = part.dynamicObjectID;
        desc.shapeType = part.dynamicShapeType;
        desc.materialIndex = part.materialIndex;
        desc.x = toGFloat(pos.x);
        desc.y = toGFloat(pos.y);
        desc.z = toGFloat(pos.z);
        desc.qx = toGFloat(q.i);
        desc.qy = toGFloat(q.j);
        desc.qz = toGFloat(q.k);
        desc.qw = toGFloat(q.w);
        return desc;
    }

    void sendDynamicSnapshotsToClient(const std::shared_ptr<RCNet::net::connection<DemoGameMsg>> &client)
    {
        for (const auto &dynamicPart: m_dynamicObjects)
        {
            RCNet::net::message<DemoGameMsg> updateMsg;
            updateMsg.header.id = DemoGameMsg::Game_UpdateDynamicObject;
            updateMsg << makeDynamicDescription(*dynamicPart);
            MessageClient(client, updateMsg);
        }
    }

    void registerClientPlayer(const std::shared_ptr<RCNet::net::connection<DemoGameMsg>> &client)
    {
        const uint32_t clientID = client->GetID();
        if (m_playerParts.find(clientID) != m_playerParts.end())
        {
            return;
        }

        auto player = std::make_unique<ServerEntityPart>(
            cylinderShape(0.5, 1.8),
            makeSpawnCFrame(clientID),
            m_playerProperties, ServerEntityPart::Type::PLAYER);

        player->isPlayer = true;
        player->playerID = clientID;

        m_world.addPart(player.get());
        m_playerParts.insert_or_assign(clientID, std::move(player));

        sPlayerInput defaultInput;
        defaultInput.nUniqueID = clientID;
        defaultInput.moveX = GFloat::Zero();
        defaultInput.moveZ = GFloat::Zero();
        defaultInput.yaw = GFloat::Zero();
        defaultInput.jumpPressed = 0;
        defaultInput.shooting = 0;
        m_playerInputs.insert_or_assign(clientID, defaultInput);
        m_playerControlStates.insert_or_assign(clientID, PlayerControlState{});

        RCNet::net::message<DemoGameMsg> assignID;
        assignID.header.id = DemoGameMsg::Client_AssignID;
        assignID << clientID;
        MessageClient(client, assignID);

        for (const auto &entry: m_playerParts)
        {
            RCNet::net::message<DemoGameMsg> addMsg;
            addMsg.header.id = DemoGameMsg::Game_AddPlayer;
            addMsg << makeDescription(*entry.second);
            MessageClient(client, addMsg);
        }

        // Client needs the latest authoritative dynamic props right after register.
        sendDynamicSnapshotsToClient(client);

        auto it = m_playerParts.find(clientID);
        if (it != m_playerParts.end())
        {
            RCNet::net::message<DemoGameMsg> announce;
            announce.header.id = DemoGameMsg::Game_AddPlayer;
            announce << makeDescription(*it->second);
            MessageAllClients(announce, client);
        }

        std::cout << "[" << clientID << "]: Register\n";
    }

    void removeClientPlayer(uint32_t clientID, bool notify)
    {
        auto it = m_playerParts.find(clientID);
        if (it == m_playerParts.end())
        {
            return;
        }

        m_world.removePart(it->second.get());
        m_playerParts.erase(it);
        m_playerInputs.erase(clientID);
        m_playerControlStates.erase(clientID);

        if (notify)
        {
            RCNet::net::message<DemoGameMsg> removeMsg;
            removeMsg.header.id = DemoGameMsg::Game_RemovePlayer;
            removeMsg << clientID;
            MessageAllClients(removeMsg);
        }
    }

    void handleClientInput(const std::shared_ptr<RCNet::net::connection<DemoGameMsg>> &client,
                           RCNet::net::message<DemoGameMsg> &msg)
    {
        if (msg.body.size() < sizeof(sPlayerInput))
        {
            return;
        }

        sPlayerInput input;
        msg >> input;
        input.nUniqueID = client->GetID();

        const double clampedX = std::clamp(toDouble(input.moveX), -1.0, 1.0);
        const double clampedZ = std::clamp(toDouble(input.moveZ), -1.0, 1.0);
        input.moveX = toGFloat(clampedX);
        input.moveZ = toGFloat(clampedZ);

        m_playerInputs.insert_or_assign(input.nUniqueID, input);
    }

    void applyInputToPlayers()
    {
        for (auto &entry: m_playerParts)
        {
            const uint32_t playerID = entry.first;
            ServerEntityPart &part = *entry.second;

            sPlayerInput input{};
            auto inputIt = m_playerInputs.find(playerID);
            if (inputIt != m_playerInputs.end())
            {
                input = inputIt->second;
            }

            const double moveX = std::clamp(toDouble(input.moveX), -1.0, 1.0);
            const double moveZ = std::clamp(toDouble(input.moveZ), -1.0, 1.0);
            const double yaw = toDouble(input.yaw);
            const bool jumpNow = (input.jumpPressed != 0);

            const bool shootNow = (input.shooting != 0);
            const double posX = toDouble(input.firePosX);
            const double posY = toDouble(input.firePosY);
            const double posZ = toDouble(input.firePosZ);
            const double dirX = toDouble(input.dirX);
            const double dirY = toDouble(input.dirY);
            const double dirZ = toDouble(input.dirZ);

            auto &controlState = m_playerControlStates[playerID];

            Vec3 velocity = part.getVelocity();
            velocity.x = moveX * m_playerMoveSpeed;
            velocity.z = moveZ * m_playerMoveSpeed;
            velocity.y = 0;

            // jump
            const bool jumpTriggered = jumpNow && !controlState.jumpPressedLast;
            controlState.jumpPressedLast = jumpNow;
            if (jumpTriggered && isPlayerGrounded(part))
            {
                //velocity.y = m_playerJumpSpeed;
            }

            part.setVelocity(velocity);

            GlobalCFrame frame = part.getCFrame();
            frame.rotation = Rotation::rotY(yaw);
            part.setCFrame(frame);

            // shoot
            const bool shootTriggered = shootNow && !controlState.shootPressedLast;
            controlState.shootPressedLast = shootNow;
            controlState.shootCooldown = std::max(0.0, controlState.shootCooldown - m_world.deltaT);
            if (shootTriggered && controlState.shootCooldown <= 0.0)
            {
                tryShoot(part, posX, posY, posZ, dirX, dirY, dirZ);
                controlState.shootCooldown = 1.0 / m_fireRate;
            }
        }
    }

    void tryShoot(ServerEntityPart &shooter, double posx, double posy, double posz, double dirx, double diry, double dirz)
    {
        Position rayOriginPos(posx, posy, posz);
        Vec3 rayOrigin(rayOriginPos.x, rayOriginPos.y, rayOriginPos.z);
        Vec3 rayDir = Vec3(dirx, diry, dirz);
        normalize(rayDir);

        RaycastResult<ServerEntityPart> hit;
        Ray ray(rayOriginPos, rayDir);
        bool ok = performRaycast(ray, m_world, hit, static_cast<double>(m_fireRange));

        ServerEntityPart *bestPart = hit.hitPart;
        double bestT = hit.distance;
        Vec3 hitPos = rayOrigin + rayDir * bestT;
        Vec3 impulse = rayDir * m_fireImpulse;
        try
        {
            if (bestPart->entityType != ServerEntityPart::Type::PLANE && bestPart->entityType !=
                ServerEntityPart::Type::WALL)
            {
                Vec3 partCenter = castPositionToVec3(bestPart->getCFrame().position);
                bestPart->applyImpulse(hitPos - partCenter, impulse);
            }
        } catch (...)
        {
            // ignore
        }

        // if (bestPart->entityType != ServerEntityPart::Type::PLANE && bestPart->entityType !=
        //     ServerEntityPart::Type::WALL)
        //     std::cout << "[Shoot] hit part type=" << (int) bestPart->entityType
        //             << " t=" << bestT
        //             << " pos=(" << (float) hitPos.x << "," << (float) hitPos.y << "," << (float) hitPos.z << ")\n";
    }

    bool isPlayerGrounded(const ServerEntityPart &player) const
    {
        const Position rayOrigin = player.getPosition();
        const Vec3 rayDirection(0.0, -1.0, 0.0);
        const double probeDistance = m_playerHeight * 0.5 + 0.15;

        bool grounded = false;
        m_world.forEachPart([&](const ServerEntityPart &other)
        {
            if (grounded || &other == &player)
            {
                return;
            }

            const GlobalCFrame &frame = other.getCFrame();
            const Vec3 localOrigin = frame.globalToLocal(rayOrigin);
            const Vec3 localDirection = frame.relativeToLocal(rayDirection);

            const double t = other.hitbox.getIntersectionDistance(localOrigin, localDirection);
            if (std::isfinite(t) && t > 1e-6 && t <= probeDistance)
            {
                grounded = true;
            }
        });

        return grounded;
    }

    void broadcastPlayerSnapshots()
    {
        for (const auto &entry: m_playerParts)
        {
            RCNet::net::message<DemoGameMsg> updateMsg;
            updateMsg.header.id = DemoGameMsg::Game_UpdatePlayer;
            updateMsg << makeDescription(*entry.second);
            MessageAllClients(updateMsg);
        }
    }

    void broadcastDynamicObjectSnapshots()
    {
        for (const auto &dynamicPart: m_dynamicObjects)
        {
            RCNet::net::message<DemoGameMsg> updateMsg;
            updateMsg.header.id = DemoGameMsg::Game_UpdateDynamicObject;
            updateMsg << makeDynamicDescription(*dynamicPart);
            MessageAllClients(updateMsg);
        }
    }

private:
    World<ServerEntityPart> m_world;
    DirectionalGravity m_gravity;

    PartProperties m_playerProperties{};
    PartProperties m_terrainProperties{};
    PartProperties m_dynamicObjectProperties{};

    std::unordered_map<uint32_t, std::unique_ptr<ServerEntityPart>> m_playerParts; // playerID -> playerPart
    std::unordered_map<uint32_t, sPlayerInput> m_playerInputs; // playerID -> playerInputs
    std::unordered_map<uint32_t, PlayerControlState> m_playerControlStates; // playerID -> edge-trigger state
    std::vector<std::unique_ptr<ServerEntityPart>> m_staticTerrain;
    std::vector<std::unique_ptr<ServerEntityPart>> m_dynamicObjects;

    uint32_t m_nextDynamicObjectID = 1;

    const double m_playerHeight = 1.8;
    const double m_playerMoveSpeed = 10.0;
    const double m_playerJumpSpeed = 5.0;
    const double m_fireRate = 10.0;
    const double m_fireRange = 100.0;
    const double m_fireImpulse = 170.0;
    const double m_snapshotInterval = 1.0 / 100.0;
    double m_simulationAccumulator = 0.0;
    double m_snapshotAccumulator = 0.0;
};

int main()
{
    PhysicsServer server(60000);
    if (!server.Start())
    {
        return -1;
    }

    using clock = std::chrono::steady_clock;
    auto lastTick = clock::now();

    while (true)
    {
        const auto now = clock::now();
        const double frameDt = std::chrono::duration<double>(now - lastTick).count();
        lastTick = now;

        server.Update(128, false);
        server.Tick(frameDt);

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return 0;
}
