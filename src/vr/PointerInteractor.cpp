#include "PointerInteractor.h"

#include "OpenVRRuntime.h"
#include "OverlayManager.h"
#include "PointerProjection.h"
#include "VrMath.h"

#include <algorithm>
#include <cmath>

namespace flickvr::vr {

namespace {

void writeError(const std::string& message, std::string* output) {
    if (output != nullptr) {
        *output = message;
    }
}

} // namespace

PointerInteractor::PointerInteractor(
    OpenVRRuntime& runtime,
    OverlayManager& overlays)
    : runtime_(runtime), overlays_(overlays) {
    left_.hand = Hand::Left;
    right_.hand = Hand::Right;
}

bool PointerInteractor::update(
    bool refreshTracking,
    std::string* error) {
    if (!runtime_.isInitialized()) {
        lastError_ = "OpenVR runtime is not initialized";
        writeError(lastError_, error);
        return false;
    }
    if (!overlays_.isInitialized()) {
        lastError_ = "OverlayManager is not initialized";
        writeError(lastError_, error);
        return false;
    }
    if (refreshTracking && !runtime_.updateTrackedPoses(
            runtime_.predictionSecondsToPhotons(), error)) {
        lastError_ = runtime_.lastError();
        return false;
    }

    if (!overlays_.isKeyboardVisible()) {
        left_ = computeForHand(Hand::Left, {});
        right_ = computeForHand(Hand::Right, {});
        lastError_.clear();
        writeError({}, error);
        return true;
    }

    const std::optional<RigidTransform> overlayTransform =
        overlays_.keyboardTransform(error);
    if (!overlayTransform.has_value()) {
        lastError_ = overlays_.lastError();
        return false;
    }
    left_ = computeForHand(Hand::Left, *overlayTransform);
    right_ = computeForHand(Hand::Right, *overlayTransform);
    lastError_.clear();
    writeError({}, error);
    return true;
}

const PointerState& PointerInteractor::state(Hand hand) const noexcept {
    return hand == Hand::Left ? left_ : right_;
}

const PointerState& PointerInteractor::left() const noexcept {
    return left_;
}

const PointerState& PointerInteractor::right() const noexcept {
    return right_;
}

HandMask PointerInteractor::intersectingHands() const noexcept {
    return {left_.intersects, right_.intersects};
}

HandMask PointerInteractor::keyboardActionHands(
    HandMask capturedHands) const noexcept {
    HandMask result = intersectingHands();
    result.left = result.left || (capturedHands.left && left_.tracked);
    result.right = result.right || (capturedHands.right && right_.tracked);
    return result;
}

std::optional<Hand> PointerInteractor::closestIntersectingHand() const noexcept {
    if (!left_.intersects && !right_.intersects) {
        return std::nullopt;
    }
    if (!right_.intersects) {
        return Hand::Left;
    }
    if (!left_.intersects) {
        return Hand::Right;
    }
    return left_.distanceMeters <= right_.distanceMeters
        ? Hand::Left
        : Hand::Right;
}

Vec2 PointerInteractor::toTexturePixels(
    const PointerState& pointer,
    float textureWidth,
    float textureHeight,
    bool flipY,
    bool clampToTexture) noexcept {
    float u = pointer.uv.x;
    float v = pointer.uv.y;
    if (clampToTexture) {
        u = std::clamp(u, 0.0F, 1.0F);
        v = std::clamp(v, 0.0F, 1.0F);
    }
    if (flipY) {
        v = 1.0F - v;
    }
    return {
        u * std::max(textureWidth, 0.0F),
        v * std::max(textureHeight, 0.0F),
    };
}

void PointerInteractor::setVerticalGain(float gain) noexcept {
    if (std::isfinite(gain)) {
        verticalGain_ = std::clamp(gain, 0.5F, 3.0F);
    }
}

float PointerInteractor::verticalGain() const noexcept {
    return verticalGain_;
}

const std::string& PointerInteractor::lastError() const noexcept {
    return lastError_;
}

PointerState PointerInteractor::computeForHand(
    Hand hand,
    const RigidTransform& overlayTransform) const noexcept {
    PointerState pointer{};
    pointer.hand = hand;

    const TrackedPose controllerPose = runtime_.controllerPose(hand);
    const TrackedPose aimPose = runtime_.controllerAimPose(hand);
    pointer.tracked = controllerPose.valid && aimPose.valid;
    pointer.deviceIndex = controllerPose.deviceIndex;
    // Preserve the physical controller pose separately. The laser and
    // laser-grab placement share the render-model's aim component.
    pointer.controllerTransform = controllerPose.transform;
    pointer.aimTransform = aimPose.transform;
    if (!pointer.tracked) {
        return pointer;
    }

    pointer.ray = math::forwardRay(aimPose.transform);
    if (!overlays_.isKeyboardVisible()) {
        return pointer;
    }

    const OverlayConfiguration& overlayConfig = overlays_.configuration();
    const float width = overlayConfig.frontPlacement.overlayWidthMeters;
    const float height = width *
        static_cast<float>(overlayConfig.keyboardPixelHeight) /
        static_cast<float>(overlayConfig.keyboardPixelWidth);
    const PointerProjection projection = projectPointerToOverlay(
        pointer.ray,
        overlayTransform,
        {width, height, verticalGain_});
    pointer.projectsToPlane = projection.projectsToPlane;
    pointer.intersects = projection.intersects;
    pointer.hitPoint = projection.hitPoint;
    pointer.hitNormal = projection.hitNormal;
    pointer.rawUv = projection.rawUv;
    pointer.uv = projection.uv;
    pointer.distanceMeters = projection.distanceMeters;
    return pointer;
}

} // namespace flickvr::vr
