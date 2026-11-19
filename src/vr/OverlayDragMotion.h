#pragma once

#include "VrMath.h"
#include "VrTypes.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace flickvr::vr::dragmotion {

inline constexpr float kDefaultTranslationLagSeconds = 0.085F;
inline constexpr float kCompatibilityFrameSeconds = 1.0F / 90.0F;
inline constexpr float kHmdRollFollowThresholdRadians =
    1.04719755119659774615F; // 60 degrees

[[nodiscard]] inline RigidTransform withPosition(
    RigidTransform transform,
    Vec3 position) noexcept {
    transform.matrix[3] = position.x;
    transform.matrix[7] = position.y;
    transform.matrix[11] = position.z;
    return transform;
}

// Rebuilds the laser-aim pose from its ray direction and world-up. Controller
// roll must not swing an edge-grab around the ray while the overlay itself is
// kept HMD-facing.
[[nodiscard]] inline RigidTransform rollFreeAimTransform(
    const RigidTransform& aim) noexcept {
    const Vec3 backward = math::normalized(math::basisZ(aim));
    constexpr Vec3 worldUp{0.0F, 1.0F, 0.0F};
    Vec3 rawRight = math::cross(worldUp, backward);
    if (math::lengthSquared(rawRight) <= math::kEpsilon * math::kEpsilon) {
        constexpr Vec3 worldForward{0.0F, 0.0F, 1.0F};
        rawRight = math::cross(worldForward, backward);
    }
    const Vec3 right = math::normalized(rawRight, math::basisX(aim));
    const Vec3 up = math::normalized(
        math::cross(backward, right), math::basisY(aim));
    return math::fromBasis(right, up, backward, math::translation(aim));
}

// Captures the overlay center in the same aim space as the visible laser. This
// gives a distant grab the expected lever arm when the controller rotates.
[[nodiscard]] inline Vec3 translationTarget(
    Vec3 overlayCenterInAimSpace,
    const RigidTransform& aimNow) noexcept {
    return math::transformPoint(
        rollFreeAimTransform(aimNow), overlayCenterInAimSpace);
}

[[nodiscard]] inline Vec3 captureOverlayCenterInAimSpace(
    const RigidTransform& aimAtGrab,
    const RigidTransform& overlayAtGrab) noexcept {
    return math::transformPoint(
        math::inverseRigid(rollFreeAimTransform(aimAtGrab)),
        math::translation(overlayAtGrab));
}

[[nodiscard]] inline float exponentialFollowAlpha(
    float deltaSeconds,
    float lagSeconds = kDefaultTranslationLagSeconds) noexcept {
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0F) {
        return 0.0F;
    }
    if (!std::isfinite(lagSeconds) || lagSeconds <= math::kEpsilon) {
        return 1.0F;
    }
    return std::clamp(
        1.0F - std::exp(-deltaSeconds / lagSeconds),
        0.0F,
        1.0F);
}

[[nodiscard]] inline Vec3 smoothPosition(
    Vec3 current,
    Vec3 target,
    float deltaSeconds,
    float lagSeconds = kDefaultTranslationLagSeconds) noexcept {
    const float alpha = exponentialFollowAlpha(deltaSeconds, lagSeconds);
    return math::add(current, math::multiply(math::subtract(target, current), alpha));
}

// Keeps local +Z pointed toward the HMD and reconstructs an orthonormal basis
// from world-up, removing controller and HMD roll. Invalid/degenerate HMD data
// preserves the fallback orientation while still applying the new position.
[[nodiscard]] inline RigidTransform faceHmdRollFree(
    Vec3 overlayPosition,
    const TrackedPose& hmdPose,
    const RigidTransform& fallbackOrientation) noexcept {
    RigidTransform fallback = withPosition(fallbackOrientation, overlayPosition);
    if (!hmdPose.valid) {
        return fallback;
    }

    const Vec3 toHmd = math::subtract(
        math::translation(hmdPose.transform),
        overlayPosition);
    if (math::lengthSquared(toHmd) <= math::kEpsilon * math::kEpsilon) {
        return fallback;
    }

    const Vec3 backward = math::normalized(toHmd, math::basisZ(fallbackOrientation));
    constexpr Vec3 worldUp{0.0F, 1.0F, 0.0F};
    const Vec3 rawRight = math::cross(worldUp, backward);
    if (math::lengthSquared(rawRight) <= math::kEpsilon * math::kEpsilon) {
        return fallback;
    }
    const Vec3 right = math::normalized(rawRight, math::basisX(fallbackOrientation));
    const Vec3 up = math::normalized(
        math::cross(backward, right),
        math::basisY(fallbackOrientation));
    return math::fromBasis(right, up, backward, overlayPosition);
}

// Returns the HMD roll around its own viewing axis. Pitch and yaw are removed
// by comparing the tracked up vector with a world-up reference basis.
[[nodiscard]] inline std::optional<float> hmdRollRadians(
    const TrackedPose& hmdPose) noexcept {
    if (!hmdPose.valid) {
        return std::nullopt;
    }

    const Vec3 backward = math::normalized(math::basisZ(hmdPose.transform));
    constexpr Vec3 worldUp{0.0F, 1.0F, 0.0F};
    const Vec3 rawRight = math::cross(worldUp, backward);
    if (math::lengthSquared(rawRight) <= math::kEpsilon * math::kEpsilon) {
        return std::nullopt;
    }
    const Vec3 referenceRight = math::normalized(rawRight);
    const Vec3 referenceUp = math::normalized(
        math::cross(backward, referenceRight));
    const Vec3 trackedUp = math::normalized(
        math::basisY(hmdPose.transform), referenceUp);
    float sine = math::dot(trackedUp, referenceRight);
    float cosine = math::dot(trackedUp, referenceUp);
    const float magnitude = std::sqrt(sine * sine + cosine * cosine);
    if (!std::isfinite(magnitude) || magnitude <= math::kEpsilon) {
        return std::nullopt;
    }
    sine /= magnitude;
    cosine /= magnitude;
    return std::atan2(sine, cosine);
}

// While dragging, large head tilts are intentional enough to be useful as an
// orientation gesture. Below the threshold the keyboard stays level to avoid
// ordinary head movement making the UI wobble.
[[nodiscard]] inline RigidTransform faceHmdWithConditionalRoll(
    Vec3 overlayPosition,
    const TrackedPose& hmdPose,
    const RigidTransform& fallbackOrientation,
    float thresholdRadians = kHmdRollFollowThresholdRadians) noexcept {
    const RigidTransform rollFree = faceHmdRollFree(
        overlayPosition, hmdPose, fallbackOrientation);
    const std::optional<float> roll = hmdRollRadians(hmdPose);
    if (!roll.has_value() || !std::isfinite(thresholdRadians) ||
        std::abs(*roll) + math::kEpsilon <
            std::max(thresholdRadians, 0.0F)) {
        return rollFree;
    }

    const float sine = std::sin(*roll);
    const float cosine = std::cos(*roll);
    const Vec3 levelRight = math::basisX(rollFree);
    const Vec3 levelUp = math::basisY(rollFree);
    const Vec3 right = math::add(
        math::multiply(levelRight, cosine),
        math::multiply(levelUp, -sine));
    const Vec3 up = math::add(
        math::multiply(levelRight, sine),
        math::multiply(levelUp, cosine));
    return math::fromBasis(
        right,
        up,
        math::basisZ(rollFree),
        overlayPosition);
}

[[nodiscard]] inline RigidTransform nextOverlayTransform(
    Vec3 overlayCenterInAimSpace,
    const RigidTransform& aimNow,
    const RigidTransform& currentOverlay,
    const TrackedPose& hmdPose,
    float deltaSeconds,
    float lagSeconds = kDefaultTranslationLagSeconds) noexcept {
    const Vec3 target = translationTarget(
        overlayCenterInAimSpace,
        aimNow);
    const Vec3 position = smoothPosition(
        math::translation(currentOverlay),
        target,
        deltaSeconds,
        lagSeconds);
    return faceHmdWithConditionalRoll(
        position, hmdPose, currentOverlay);
}

} // namespace flickvr::vr::dragmotion
