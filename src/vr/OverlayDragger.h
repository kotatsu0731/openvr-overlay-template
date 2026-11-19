#pragma once

#include "VrTypes.h"

#include <optional>
#include <string>

namespace flickvr::vr {

class OverlayManager;

// Reconstructs the grabbed center from the controller's laser-aim pose with a
// small lag. Controller rotation affects position through the natural lever arm
// but never controls overlay orientation; the overlay faces the headset and
// follows only deliberate HMD roll of at least 60 degrees.
class OverlayDragger final {
public:
    explicit OverlayDragger(OverlayManager& overlays);

    [[nodiscard]] bool begin(
        const PointerState& pointer,
        std::string* error = nullptr);
    [[nodiscard]] bool update(
        const PointerState& pointer,
        std::string* error = nullptr);
    [[nodiscard]] bool update(
        const PointerState& pointer,
        const TrackedPose& hmdPose,
        float deltaSeconds,
        std::string* error = nullptr);
    // Applies a host-computed transform (for example, right-stick distance
    // adjustment) without breaking the active aim-space grab anchor.
    [[nodiscard]] bool adoptTransform(
        const RigidTransform& transform,
        const PointerState& pointer,
        std::string* error = nullptr);
    void end() noexcept;
    [[nodiscard]] bool cancel(std::string* error = nullptr);

    [[nodiscard]] bool isDragging() const noexcept;
    [[nodiscard]] std::optional<Hand> capturedHand() const noexcept;
    [[nodiscard]] const RigidTransform& initialTransform() const noexcept;
    [[nodiscard]] const RigidTransform& currentTransform() const noexcept;
    [[nodiscard]] const std::string& lastError() const noexcept;

private:
    OverlayManager& overlays_;
    std::optional<Hand> capturedHand_{};
    Vec3 overlayCenterInAimSpace_{};
    RigidTransform overlayAtGrab_{};
    RigidTransform currentOverlay_{};
    std::string lastError_{};
};

} // namespace flickvr::vr
