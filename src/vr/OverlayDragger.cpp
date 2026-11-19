#include "OverlayDragger.h"

#include "OverlayDragMotion.h"
#include "OverlayManager.h"

#include <cmath>

namespace flickvr::vr {

namespace {

void writeError(const std::string& message, std::string* output) {
    if (output != nullptr) {
        *output = message;
    }
}

} // namespace

OverlayDragger::OverlayDragger(OverlayManager& overlays)
    : overlays_(overlays) {}

bool OverlayDragger::begin(
    const PointerState& pointer,
    std::string* error) {
    if (capturedHand_.has_value()) {
        lastError_ = "An overlay drag is already active";
        writeError(lastError_, error);
        return false;
    }
    if (!pointer.tracked || !pointer.intersects) {
        lastError_ = "A drag can only start from a tracked overlay intersection";
        writeError(lastError_, error);
        return false;
    }

    const std::optional<RigidTransform> overlayTransform =
        overlays_.keyboardTransform(error);
    if (!overlayTransform.has_value()) {
        lastError_ = overlays_.lastError();
        return false;
    }

    capturedHand_ = pointer.hand;
    overlayAtGrab_ = *overlayTransform;
    overlayCenterInAimSpace_ = dragmotion::captureOverlayCenterInAimSpace(
        pointer.aimTransform,
        overlayAtGrab_);
    currentOverlay_ = *overlayTransform;
    lastError_.clear();
    writeError({}, error);
    return true;
}

bool OverlayDragger::update(
    const PointerState& pointer,
    std::string* error) {
    // Compatibility path for existing hosts. It advances aim-relative motion
    // by one nominal VR frame while preserving orientation without an HMD pose.
    return update(
        pointer,
        TrackedPose{},
        dragmotion::kCompatibilityFrameSeconds,
        error);
}

bool OverlayDragger::update(
    const PointerState& pointer,
    const TrackedPose& hmdPose,
    float deltaSeconds,
    std::string* error) {
    if (!capturedHand_.has_value()) {
        lastError_ = "No overlay drag is active";
        writeError(lastError_, error);
        return false;
    }
    if (pointer.hand != *capturedHand_) {
        lastError_ = "The non-captured hand cannot update this overlay drag";
        writeError(lastError_, error);
        return false;
    }
    if (!pointer.tracked) {
        lastError_ = "The captured controller pose is not currently valid";
        writeError(lastError_, error);
        return false;
    }
    if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0F) {
        lastError_ = "Overlay drag delta time must be finite and non-negative";
        writeError(lastError_, error);
        return false;
    }

    const RigidTransform nextOverlay = dragmotion::nextOverlayTransform(
        overlayCenterInAimSpace_,
        pointer.aimTransform,
        currentOverlay_,
        hmdPose,
        deltaSeconds);
    if (!overlays_.setKeyboardTransform(nextOverlay, error)) {
        lastError_ = overlays_.lastError();
        return false;
    }

    currentOverlay_ = nextOverlay;
    lastError_.clear();
    writeError({}, error);
    return true;
}

bool OverlayDragger::adoptTransform(
    const RigidTransform& transform,
    const PointerState& pointer,
    std::string* error) {
    if (!capturedHand_.has_value()) {
        lastError_ = "No overlay drag is active";
        writeError(lastError_, error);
        return false;
    }
    if (pointer.hand != *capturedHand_ || !pointer.tracked) {
        lastError_ = "The captured controller pose is not currently valid";
        writeError(lastError_, error);
        return false;
    }
    if (!overlays_.setKeyboardTransform(transform, error)) {
        lastError_ = overlays_.lastError();
        return false;
    }

    currentOverlay_ = transform;
    overlayCenterInAimSpace_ = dragmotion::captureOverlayCenterInAimSpace(
        pointer.aimTransform,
        currentOverlay_);
    lastError_.clear();
    writeError({}, error);
    return true;
}

void OverlayDragger::end() noexcept {
    capturedHand_.reset();
    overlayCenterInAimSpace_ = {};
    overlayAtGrab_ = currentOverlay_;
}

bool OverlayDragger::cancel(std::string* error) {
    if (!capturedHand_.has_value()) {
        lastError_.clear();
        writeError({}, error);
        return true;
    }

    const bool restored = overlays_.setKeyboardTransform(overlayAtGrab_, error);
    if (!restored) {
        lastError_ = overlays_.lastError();
    } else {
        currentOverlay_ = overlayAtGrab_;
        lastError_.clear();
    }
    capturedHand_.reset();
    overlayCenterInAimSpace_ = {};
    return restored;
}

bool OverlayDragger::isDragging() const noexcept {
    return capturedHand_.has_value();
}

std::optional<Hand> OverlayDragger::capturedHand() const noexcept {
    return capturedHand_;
}

const RigidTransform& OverlayDragger::initialTransform() const noexcept {
    return overlayAtGrab_;
}

const RigidTransform& OverlayDragger::currentTransform() const noexcept {
    return currentOverlay_;
}

const std::string& OverlayDragger::lastError() const noexcept {
    return lastError_;
}

} // namespace flickvr::vr
