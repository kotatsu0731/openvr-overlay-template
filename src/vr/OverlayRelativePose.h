#pragma once

#include "VrMath.h"
#include "VrTypes.h"

#include <optional>

namespace flickvr::vr::relativepose {

// Visibility changes should follow HMD position, yaw and pitch, but not inject
// the user's current head roll into a world-space keyboard. This produces the
// stable reference frame used on both sides of the hide/show transition.
[[nodiscard]] inline RigidTransform rollStableHmdTransform(
    const RigidTransform& hmd) noexcept {
    const Vec3 backward = math::normalized(math::basisZ(hmd));
    constexpr Vec3 worldUp{0.0F, 1.0F, 0.0F};
    Vec3 rawRight = math::cross(worldUp, backward);
    if (math::lengthSquared(rawRight) <= math::kEpsilon * math::kEpsilon) {
        constexpr Vec3 worldForward{0.0F, 0.0F, 1.0F};
        rawRight = math::cross(worldForward, backward);
    }
    const Vec3 right = math::normalized(rawRight, math::basisX(hmd));
    const Vec3 up = math::normalized(
        math::cross(backward, right), math::basisY(hmd));
    return math::fromBasis(right, up, backward, math::translation(hmd));
}

// Capture and restore are intentionally separate from visibility management.
// Calling them only on hide/show transitions preserves an HMD-relative pose
// without turning the keyboard into a continuously head-locked overlay.
[[nodiscard]] inline std::optional<RigidTransform> capture(
    const RigidTransform& overlay,
    const TrackedPose& hmd) noexcept {
    if (!hmd.valid) {
        return std::nullopt;
    }
    return math::multiply(
        math::inverseRigid(rollStableHmdTransform(hmd.transform)),
        overlay);
}

[[nodiscard]] inline std::optional<RigidTransform> restore(
    const RigidTransform& overlayRelativeToHmd,
    const TrackedPose& hmd) noexcept {
    if (!hmd.valid) {
        return std::nullopt;
    }
    return math::multiply(
        rollStableHmdTransform(hmd.transform),
        overlayRelativeToHmd);
}

} // namespace flickvr::vr::relativepose
