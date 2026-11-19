#pragma once

#include "OpenVRRuntime.h"

#include <openvr.h>

namespace flickvr::vr::detail {

// Keeps OpenVR interface pointers out of the public runtime API while allowing
// implementation helpers (including PIMPL types) to share the same connection.
struct OpenVRAccess final {
    [[nodiscard]] static ::vr::IVRSystem* system(
        const OpenVRRuntime& runtime) noexcept {
        return static_cast<::vr::IVRSystem*>(runtime.nativeSystem());
    }

    [[nodiscard]] static ::vr::IVROverlay* overlay(
        const OpenVRRuntime& runtime) noexcept {
        return static_cast<::vr::IVROverlay*>(runtime.nativeOverlay());
    }

    [[nodiscard]] static ::vr::IVRInput* input(
        const OpenVRRuntime& runtime) noexcept {
        return static_cast<::vr::IVRInput*>(runtime.nativeInput());
    }
};

} // namespace flickvr::vr::detail
