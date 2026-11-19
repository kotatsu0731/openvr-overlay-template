#pragma once

#include "VrTypes.h"

#include <openvr.h>

#include <string>

namespace flickvr::vr::detail {

[[nodiscard]] inline ::vr::ETrackingUniverseOrigin toOpenVROrigin(
    TrackingOrigin origin) noexcept {
    return origin == TrackingOrigin::Seated
        ? ::vr::TrackingUniverseSeated
        : ::vr::TrackingUniverseStanding;
}

[[nodiscard]] inline RigidTransform fromOpenVR(
    const ::vr::HmdMatrix34_t& matrix) noexcept {
    RigidTransform result{};
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 4; ++column) {
            result.matrix[row * 4 + column] = matrix.m[row][column];
        }
    }
    return result;
}

[[nodiscard]] inline ::vr::HmdMatrix34_t toOpenVR(
    const RigidTransform& transform) noexcept {
    ::vr::HmdMatrix34_t result{};
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 4; ++column) {
            result.m[row][column] = transform.matrix[row * 4 + column];
        }
    }
    return result;
}

[[nodiscard]] inline ::vr::EColorSpace toOpenVRColorSpace(
    TextureColorSpace colorSpace) noexcept {
    switch (colorSpace) {
    case TextureColorSpace::Gamma:
        return ::vr::ColorSpace_Gamma;
    case TextureColorSpace::Linear:
        return ::vr::ColorSpace_Linear;
    case TextureColorSpace::Auto:
    default:
        return ::vr::ColorSpace_Auto;
    }
}

[[nodiscard]] inline const char* inputErrorName(
    ::vr::EVRInputError error) noexcept {
    switch (error) {
    case ::vr::VRInputError_None:
        return "None";
    case ::vr::VRInputError_NameNotFound:
        return "NameNotFound";
    case ::vr::VRInputError_WrongType:
        return "WrongType";
    case ::vr::VRInputError_InvalidHandle:
        return "InvalidHandle";
    case ::vr::VRInputError_InvalidParam:
        return "InvalidParam";
    case ::vr::VRInputError_NoSteam:
        return "NoSteam";
    case ::vr::VRInputError_MaxCapacityReached:
        return "MaxCapacityReached";
    case ::vr::VRInputError_IPCError:
        return "IPCError";
    case ::vr::VRInputError_NoActiveActionSet:
        return "NoActiveActionSet";
    case ::vr::VRInputError_InvalidDevice:
        return "InvalidDevice";
    case ::vr::VRInputError_NoData:
        return "NoData";
    case ::vr::VRInputError_BufferTooSmall:
        return "BufferTooSmall";
    case ::vr::VRInputError_MismatchedActionManifest:
        return "MismatchedActionManifest";
    case ::vr::VRInputError_InvalidPriority:
        return "InvalidPriority";
    case ::vr::VRInputError_PermissionDenied:
        return "PermissionDenied";
    default:
        return "UnknownInputError";
    }
}

[[nodiscard]] inline std::string overlayErrorMessage(
    ::vr::IVROverlay* overlay,
    ::vr::EVROverlayError error) {
    if (error == ::vr::VROverlayError_None) {
        return {};
    }
    if (overlay != nullptr) {
        if (const char* name = overlay->GetOverlayErrorNameFromEnum(error);
            name != nullptr) {
            return name;
        }
    }
    return "OpenVR overlay error " + std::to_string(static_cast<int>(error));
}

} // namespace flickvr::vr::detail
