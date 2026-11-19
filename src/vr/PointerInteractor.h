#pragma once

#include "VrTypes.h"

#include <optional>
#include <string>

namespace flickvr::vr {

class OpenVRRuntime;
class OverlayManager;

class PointerInteractor final {
public:
    PointerInteractor(OpenVRRuntime& runtime, OverlayManager& overlays);

    // Refreshes predicted HMD/controller poses by default, then intersects
    // each controller's model-specific laser aim ray against the overlay.
    [[nodiscard]] bool update(
        bool refreshTracking = true,
        std::string* error = nullptr);

    [[nodiscard]] const PointerState& state(Hand hand) const noexcept;
    [[nodiscard]] const PointerState& left() const noexcept;
    [[nodiscard]] const PointerState& right() const noexcept;
    [[nodiscard]] HandMask intersectingHands() const noexcept;

    // capturedHands keep flick/grab actions active after their rays leave the
    // visible overlay rectangle.
    [[nodiscard]] HandMask keyboardActionHands(
        HandMask capturedHands = {}) const noexcept;
    [[nodiscard]] std::optional<Hand> closestIntersectingHand() const noexcept;

    // PointerState UVs are normalized to a top-left origin. Use flipY only for
    // a renderer that explicitly expects V=0 at the lower edge.
    [[nodiscard]] static Vec2 toTexturePixels(
        const PointerState& pointer,
        float textureWidth,
        float textureHeight,
        bool flipY = false,
        bool clampToTexture = true) noexcept;

    void setVerticalGain(float gain) noexcept;
    [[nodiscard]] float verticalGain() const noexcept;

    [[nodiscard]] const std::string& lastError() const noexcept;

private:
    [[nodiscard]] PointerState computeForHand(
        Hand hand,
        const RigidTransform& overlayTransform) const noexcept;

    OpenVRRuntime& runtime_;
    OverlayManager& overlays_;
    PointerState left_{};
    PointerState right_{};
    float verticalGain_{1.0F};
    std::string lastError_{};
};

} // namespace flickvr::vr
