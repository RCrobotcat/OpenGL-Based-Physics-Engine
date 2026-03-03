#pragma once

#include <cmath>
#include <limits>
#include <shared_mutex>

#include <Physics3D/world.h>
#include <Physics3D/worldIteration.h>
#include <Physics3D/math/ray.h>
#include <Physics3D/part.h>
#include <Physics3D/threading/upgradeableMutex.h>

namespace P3D
{
    template<typename PartT>
    struct RaycastResult
    {
        bool hit = false;
        Vec3 position = Vec3(0.0, 0.0, 0.0);
        Vec3 normal = Vec3(0.0, 0.0, 0.0);
        double distance = std::numeric_limits<double>::infinity();
        PartT *hitPart = nullptr;
    };

    // Template implementation must remain in header so callers can instantiate it.
    template<typename PartT>
    bool performRaycast(const Ray &ray,
                        World<PartT> &world,
                        UpgradeableMutex &worldMutex,
                        RaycastResult<PartT> &result,
                        double maxDistance = std::numeric_limits<double>::infinity())
    {
        result = RaycastResult<PartT>{};

        double dirLenSq = lengthSquared(ray.direction);
        if (dirLenSq <= Ray::EPSILON * Ray::EPSILON)
        {
            return false;
        }

        Vec3 direction = ray.direction / std::sqrt(dirLenSq);

        std::shared_lock<UpgradeableMutex> lock(worldMutex);
        world.forEachPart([&](PartT &part)
        {
            const GlobalCFrame &frame = part.getCFrame();
            Vec3 localOrigin = frame.globalToLocal(ray.origin);
            Vec3 localDirection = frame.relativeToLocal(direction);

            double t = part.hitbox.getIntersectionDistance(localOrigin, localDirection);
            if (!std::isfinite(t) || t < Ray::EPSILON || t > maxDistance || t >= result.distance)
            {
                return;
            }

            result.hit = true;
            result.distance = t;
            result.position = castPositionToVec3(ray.origin) + direction * t;
            result.hitPart = &part;

            Vec3 normalCandidate = result.position - castPositionToVec3(part.getPosition());
            double normalLenSq = lengthSquared(normalCandidate);
            result.normal = (normalLenSq > Ray::EPSILON * Ray::EPSILON)
                                ? (normalCandidate / std::sqrt(normalLenSq))
                                : (-direction);
        });

        return result.hit;
    }
} // namespace P3D
