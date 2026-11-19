#pragma once

#include "VrMath.h"

#include <algorithm>
#include <cmath>

namespace flickvr::vr::distancemotion {

inline constexpr float kDefaultDeadZone = 0.20F;
inline constexpr float kDefaultMaximumSpeedMetersPerSecond = 1.10F;
inline constexpr float kDefaultMinimumDistanceMeters = 0.35F;
inline constexpr float kDefaultMaximumDistanceMeters = 1.50F;
inline constexpr float kMaximumDeltaSeconds = 0.05F;
inline constexpr float kDefaultStartupAccelerationDurationSeconds = 0.150F;
inline constexpr float kDefaultStartupSpeedMultiplier = 0.60F;

struct DistanceAdjustment {
    RigidTransform transform{};
    float distanceMeters{};
    float shapedAxis{};
    bool changed{};
};

// Ramps from a controllable initial velocity to the configured full speed over
// the first 150 ms. The faster default speed still makes the first frame quicker
// than the former fixed-speed implementation.
[[nodiscard]] inline float startupAccelerationMultiplier(
    float elapsedSeconds,
    float durationSeconds = kDefaultStartupAccelerationDurationSeconds,
    float initialMultiplier = kDefaultStartupSpeedMultiplier) noexcept {
    if (!std::isfinite(elapsedSeconds) || elapsedSeconds < 0.0F ||
        !std::isfinite(durationSeconds) ||
        !std::isfinite(initialMultiplier) || durationSeconds <= 0.0F) {
        return 1.0F;
    }
    const float initial = std::clamp(initialMultiplier, 0.10F, 1.0F);
    const float progress = std::clamp(elapsedSeconds / durationSeconds, 0.0F, 1.0F);
    const float smoothProgress = progress * progress * (3.0F - 2.0F * progress);
    return initial + (1.0F - initial) * smoothProgress;
}

[[nodiscard]] inline float shapeAxis(
    float rawAxis,
    float deadZone = kDefaultDeadZone) noexcept {
    if (!std::isfinite(rawAxis) || !std::isfinite(deadZone)) {
        return 0.0F;
    }
    const float clampedDeadZone = std::clamp(deadZone, 0.0F, 0.95F);
    const float magnitude = std::abs(std::clamp(rawAxis, -1.0F, 1.0F));
    if (magnitude <= clampedDeadZone) {
        return 0.0F;
    }
    const float normalized =
        (magnitude - clampedDeadZone) / (1.0F - clampedDeadZone);
    const float curved = normalized * normalized;
    return std::copysign(curved, rawAxis);
}

// Moves only along the HMD's current forward axis. Lateral placement,
// perpendicular height and overlay orientation are preserved.
[[nodiscard]] inline DistanceAdjustment adjust(
    const RigidTransform& overlay,
    const TrackedPose& hmd,
    float rawAxis,
    float deltaSeconds,
    float minimumDistanceMeters = kDefaultMinimumDistanceMeters,
    float maximumDistanceMeters = kDefaultMaximumDistanceMeters,
    float maximumSpeedMetersPerSecond =
        kDefaultMaximumSpeedMetersPerSecond) noexcept {
    DistanceAdjustment result{};
    result.transform = overlay;
    result.shapedAxis = shapeAxis(rawAxis);
    if (!hmd.valid || result.shapedAxis == 0.0F ||
        !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0F ||
        !std::isfinite(minimumDistanceMeters) ||
        !std::isfinite(maximumDistanceMeters) ||
        !std::isfinite(maximumSpeedMetersPerSecond) ||
        minimumDistanceMeters <= 0.0F ||
        maximumDistanceMeters < minimumDistanceMeters ||
        maximumSpeedMetersPerSecond <= 0.0F) {
        return result;
    }

    const Vec3 hmdForward = math::multiply(
        math::normalized(math::basisZ(hmd.transform)), -1.0F);
    const Vec3 offset = math::subtract(
        math::translation(overlay), math::translation(hmd.transform));
    const float currentDistance = math::dot(offset, hmdForward);
    if (!std::isfinite(currentDistance)) {
        return result;
    }
    result.distanceMeters = currentDistance;

    const float boundedDeltaSeconds =
        std::min(deltaSeconds, kMaximumDeltaSeconds);
    const float requestedDelta =
        result.shapedAxis * maximumSpeedMetersPerSecond * boundedDeltaSeconds;
    float nextDistance = currentDistance;
    if (currentDistance < minimumDistanceMeters) {
        if (requestedDelta > 0.0F) {
            nextDistance = std::min(
                currentDistance + requestedDelta, minimumDistanceMeters);
        }
    } else if (currentDistance > maximumDistanceMeters) {
        if (requestedDelta < 0.0F) {
            nextDistance = std::max(
                currentDistance + requestedDelta, maximumDistanceMeters);
        }
    } else {
        nextDistance = std::clamp(
            currentDistance + requestedDelta,
            minimumDistanceMeters,
            maximumDistanceMeters);
    }
    const float appliedDelta = nextDistance - currentDistance;
    result.distanceMeters = nextDistance;
    if (std::abs(appliedDelta) <= math::kEpsilon) {
        return result;
    }

    const Vec3 nextPosition = math::add(
        math::translation(overlay), math::multiply(hmdForward, appliedDelta));
    result.transform.matrix[3] = nextPosition.x;
    result.transform.matrix[7] = nextPosition.y;
    result.transform.matrix[11] = nextPosition.z;
    result.changed = true;
    return result;
}

} // namespace flickvr::vr::distancemotion
