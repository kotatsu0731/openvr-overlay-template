#pragma once

#include "VrMath.h"

#include <cmath>
#include <optional>

namespace flickvr::vr::controlleraim {

struct ComponentSelection {
    RigidTransform controllerToAim{};
    ControllerAimSource source{ControllerAimSource::RawPose};
};

[[nodiscard]] inline bool validTransform(
    const RigidTransform& transform) noexcept {
    for (const float value : transform.matrix) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    return math::lengthSquared(math::basisZ(transform)) >
        math::kEpsilon * math::kEpsilon;
}

// The OpenVR-native tip is preferred. Some OpenXR-backed controllers expose
// only openxr_aim, so it is the explicit fallback before the raw device pose.
[[nodiscard]] inline std::optional<ComponentSelection> selectComponent(
    const std::optional<RigidTransform>& tip,
    const std::optional<RigidTransform>& openXrAim) noexcept {
    if (tip.has_value() && validTransform(*tip)) {
        return ComponentSelection{*tip, ControllerAimSource::TipComponent};
    }
    if (openXrAim.has_value() && validTransform(*openXrAim)) {
        return ComponentSelection{
            *openXrAim,
            ControllerAimSource::OpenXrAimComponent};
    }
    return std::nullopt;
}

// OpenVR component transforms are local to the tracked controller. Preserve
// tracking metadata and replace only the transform used to cast the ray.
[[nodiscard]] inline TrackedPose compose(
    const TrackedPose& controllerPose,
    const std::optional<RigidTransform>& controllerToAim) noexcept {
    if (!controllerPose.valid || !controllerToAim.has_value()) {
        return controllerPose;
    }

    TrackedPose result = controllerPose;
    result.transform = math::multiply(
        controllerPose.transform,
        *controllerToAim);
    return result;
}

} // namespace flickvr::vr::controlleraim
