#pragma once

#include "VrTypes.h"

namespace flickvr::vr::placement {

// Keep the configurable height in world-up coordinates. It is intentionally
// independent of HMD pitch, so looking up or down does not invert the setting.
[[nodiscard]] inline Vec3 withVerticalOffset(
    Vec3 position,
    float verticalOffsetMeters) noexcept {
    position.y += verticalOffsetMeters;
    return position;
}

}  // namespace flickvr::vr::placement
