#pragma once

#include "VrTypes.h"

namespace flickvr::vr {

struct PointerProjectionConfig {
    float overlayWidthMeters{0.60F};
    float overlayHeightMeters{0.3375F};
    float verticalGain{1.0F};
};

struct PointerProjection {
    bool projectsToPlane{};
    bool intersects{};
    Vec3 hitPoint{};
    Vec3 hitNormal{};
    Vec2 rawUv{};
    Vec2 uv{};
    float distanceMeters{};
};

// Intersects a world-space ray with the overlay's unbounded local Z=0 plane.
// rawUv and uv deliberately remain unclamped so a gesture can keep receiving
// motion after it leaves the visible overlay rectangle.
[[nodiscard]] PointerProjection projectPointerToOverlay(
    const Ray& ray,
    const RigidTransform& overlayTransform,
    const PointerProjectionConfig& config) noexcept;

} // namespace flickvr::vr
