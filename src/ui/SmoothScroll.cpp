#include "ui/SmoothScroll.h"

#include <algorithm>
#include <cmath>

namespace flickvr::ui {

SmoothScrollController::SmoothScrollController(
    SmoothScrollConfig config) noexcept
    : config_(config) {
    config_.deadZone = std::clamp(config_.deadZone, 0.0F, 0.95F);
    config_.maximumSpeed = std::max(0.0F, config_.maximumSpeed);
    config_.accelerationResponse = std::max(0.0F, config_.accelerationResponse);
    config_.decelerationResponse = std::max(0.0F, config_.decelerationResponse);
}

float SmoothScrollController::Update(
    float axis,
    float deltaSeconds) noexcept {
    if (!std::isfinite(axis) || !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0F) {
        return 0.0F;
    }
    const float dt = std::min(deltaSeconds, 0.05F);
    const float magnitude = std::abs(std::clamp(axis, -1.0F, 1.0F));
    float shapedAxis = 0.0F;
    if (magnitude > config_.deadZone) {
        const float normalized = (magnitude - config_.deadZone) /
            (1.0F - config_.deadZone);
        shapedAxis = std::copysign(normalized * normalized, axis);
    }

    const float targetVelocity = shapedAxis * config_.maximumSpeed;
    const float response = shapedAxis == 0.0F
        ? config_.decelerationResponse
        : config_.accelerationResponse;
    const float blend = response > 0.0F
        ? 1.0F - std::exp(-response * dt)
        : 1.0F;
    velocity_ += (targetVelocity - velocity_) * blend;
    if (shapedAxis == 0.0F && std::abs(velocity_) < 0.0005F) velocity_ = 0.0F;
    return velocity_ * dt;
}

void SmoothScrollController::Reset() noexcept {
    velocity_ = 0.0F;
}

}  // namespace flickvr::ui
