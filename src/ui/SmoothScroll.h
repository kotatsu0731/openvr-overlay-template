#pragma once

namespace flickvr::ui {

struct SmoothScrollConfig {
    float deadZone{0.18F};
    float maximumSpeed{1.0F};
    float accelerationResponse{13.0F};
    float decelerationResponse{9.0F};
};

// Converts a held analog axis into a frame-rate-independent scroll delta.
// The controller owns only scalar state and performs no per-frame allocation.
class SmoothScrollController final {
public:
    explicit SmoothScrollController(SmoothScrollConfig config = {}) noexcept;

    [[nodiscard]] float Update(float axis, float deltaSeconds) noexcept;
    void Reset() noexcept;
    [[nodiscard]] float velocity() const noexcept { return velocity_; }

private:
    SmoothScrollConfig config_{};
    float velocity_{};
};

}  // namespace flickvr::ui
