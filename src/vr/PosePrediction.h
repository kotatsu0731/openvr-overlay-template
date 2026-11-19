#pragma once

#include <algorithm>
#include <cmath>

namespace flickvr::vr::poseprediction {

inline constexpr float kMaximumPredictionSeconds = 0.05F;

// Estimates the interval from this CPU sample to the next photons. This is
// the prediction interval expected by GetDeviceToAbsoluteTrackingPose().
[[nodiscard]] inline float secondsToPhotons(
    float secondsSinceLastVsync,
    float displayFrequencyHz,
    float secondsFromVsyncToPhotons) noexcept {
    if (!std::isfinite(secondsSinceLastVsync) ||
        !std::isfinite(displayFrequencyHz) ||
        !std::isfinite(secondsFromVsyncToPhotons) ||
        secondsSinceLastVsync < 0.0F ||
        displayFrequencyHz <= 1.0F ||
        secondsFromVsyncToPhotons < 0.0F) {
        return 0.0F;
    }

    const float prediction =
        (1.0F / displayFrequencyHz) - secondsSinceLastVsync +
        secondsFromVsyncToPhotons;
    if (!std::isfinite(prediction)) {
        return 0.0F;
    }
    return std::clamp(prediction, 0.0F, kMaximumPredictionSeconds);
}

} // namespace flickvr::vr::poseprediction
