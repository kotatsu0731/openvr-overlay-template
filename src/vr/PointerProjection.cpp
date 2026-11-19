#include "PointerProjection.h"

#include "VrMath.h"

#include <cmath>

namespace flickvr::vr {

PointerProjection projectPointerToOverlay(
    const Ray& ray,
    const RigidTransform& overlayTransform,
    const PointerProjectionConfig& config) noexcept {
    PointerProjection result{};
    if (!std::isfinite(config.overlayWidthMeters) ||
        !std::isfinite(config.overlayHeightMeters) ||
        !std::isfinite(config.verticalGain) ||
        config.overlayWidthMeters <= math::kEpsilon ||
        config.overlayHeightMeters <= math::kEpsilon ||
        config.verticalGain <= 0.0F) {
        return result;
    }

    const RigidTransform inverseOverlay = math::inverseRigid(overlayTransform);
    const Vec3 localOrigin = math::transformPoint(inverseOverlay, ray.origin);
    const Vec3 localDirection = math::transformDirection(inverseOverlay, ray.direction);
    if (!std::isfinite(localDirection.z) ||
        std::abs(localDirection.z) <= math::kEpsilon) {
        return result;
    }

    const float distance = -localOrigin.z / localDirection.z;
    if (!std::isfinite(distance) || distance < 0.0F) {
        return result;
    }

    const Vec3 localHit = math::add(
        localOrigin,
        math::multiply(localDirection, distance));
    const Vec3 worldHit = math::add(
        ray.origin,
        math::multiply(ray.direction, distance));
    const Vec2 rawUv{
        0.5F + localHit.x / config.overlayWidthMeters,
        0.5F - localHit.y / config.overlayHeightMeters,
    };
    const Vec2 adjustedUv{
        rawUv.x,
        0.5F + (rawUv.y - 0.5F) * config.verticalGain,
    };
    if (!std::isfinite(rawUv.x) || !std::isfinite(rawUv.y) ||
        !std::isfinite(adjustedUv.x) || !std::isfinite(adjustedUv.y)) {
        return result;
    }

    result.projectsToPlane = true;
    result.intersects = adjustedUv.x >= 0.0F && adjustedUv.x <= 1.0F &&
        adjustedUv.y >= 0.0F && adjustedUv.y <= 1.0F;
    result.hitPoint = worldHit;
    result.hitNormal = math::normalized(math::basisZ(overlayTransform), {0.0F, 0.0F, 1.0F});
    result.rawUv = rawUv;
    result.uv = adjustedUv;
    result.distanceMeters = distance;
    return result;
}

} // namespace flickvr::vr
