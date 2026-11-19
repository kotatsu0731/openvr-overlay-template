#pragma once

#include "VrTypes.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace flickvr::vr {

class OverlayManager;
class PointerInteractor;
class SteamVRInput;
namespace detail {
struct OpenVRAccess;
}

// Owns the process-wide OpenVR client connection. Create exactly one instance
// and keep it alive longer than the other classes in this directory.
class OpenVRRuntime final {
public:
    explicit OpenVRRuntime(TrackingOrigin origin = TrackingOrigin::Standing);
    ~OpenVRRuntime();

    OpenVRRuntime(const OpenVRRuntime&) = delete;
    OpenVRRuntime& operator=(const OpenVRRuntime&) = delete;
    OpenVRRuntime(OpenVRRuntime&&) = delete;
    OpenVRRuntime& operator=(OpenVRRuntime&&) = delete;

    [[nodiscard]] bool initialize(std::string* error = nullptr);
    void shutdown() noexcept;

    [[nodiscard]] bool isInitialized() const noexcept;
    [[nodiscard]] bool compositorAvailable() const noexcept;
    [[nodiscard]] static bool runtimeInstalled() noexcept;
    [[nodiscard]] static bool hmdPresent() noexcept;

    // Use this index with IDXGIFactory::EnumAdapters before D3D11CreateDevice.
    // OpenVR rejects overlay textures created on a different GPU.
    [[nodiscard]] std::optional<std::int32_t> graphicsAdapterIndex(
        std::string* error = nullptr);

    // Drains process-wide OpenVR events and latches quit events. Polling is
    // rejected until SteamVRInput has successfully set the action manifest.
    [[nodiscard]] bool pollEvents(std::string* error = nullptr);
    [[nodiscard]] bool quitRequested() const noexcept;
    [[nodiscard]] bool keyboardClosedForOverlay(std::uint64_t overlay) const noexcept;
    [[nodiscard]] bool actionManifestConfigured() const noexcept;

    // Synchronizes an overlay application's loop to SteamVR's frame cadence.
    // This avoids busy-rendering while the headset is running at a lower rate.
    [[nodiscard]] bool waitFrameSync(
        std::uint32_t timeoutMilliseconds = 20U,
        std::string* error = nullptr);

    void setTrackingOrigin(TrackingOrigin origin) noexcept;
    [[nodiscard]] TrackingOrigin trackingOrigin() const noexcept;

    // Refreshes HMD and left/right controller poses in one OpenVR call.
    // This does not call PollNextEvent, so an action manifest may still be set
    // after runtime initialization and before SteamVRInput::initialize().
    [[nodiscard]] bool updateTrackedPoses(
        float predictedSecondsFromNow = 0.0F,
        std::string* error = nullptr);

    // Best-effort prediction to the next displayed photons. Returns zero if
    // the runtime does not expose valid display timing properties.
    [[nodiscard]] float predictionSecondsToPhotons() const noexcept;

    [[nodiscard]] TrackedPose hmdPose() const noexcept;
    [[nodiscard]] TrackedPose controllerPose(Hand hand) const noexcept;
    [[nodiscard]] TrackedPose controllerAimPose(Hand hand) const noexcept;
    [[nodiscard]] ControllerAimSource controllerAimSource(
        Hand hand) const noexcept;
    [[nodiscard]] const std::string& lastError() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    [[nodiscard]] void* nativeSystem() const noexcept;
    [[nodiscard]] void* nativeOverlay() const noexcept;
    [[nodiscard]] void* nativeInput() const noexcept;
    void markActionManifestConfigured() noexcept;

    friend class OverlayManager;
    friend class PointerInteractor;
    friend class SteamVRInput;
    friend struct detail::OpenVRAccess;
};

} // namespace flickvr::vr
