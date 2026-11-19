#pragma once

#include "VrTypes.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace flickvr::vr {

class OpenVRRuntime;

struct SteamVRActionHandles {
    std::uint64_t globalSet{};
    std::uint64_t keyboardSet{};
    std::uint64_t blockerSet{};
    std::uint64_t toggleOverlay{};
    std::uint64_t primary{};
    std::uint64_t grab{};
    std::uint64_t adjustDistance{};
    std::uint64_t haptic{};
    std::uint64_t leftSource{};
    std::uint64_t rightSource{};
};

// SteamVR Input facade for resources/openvr/actions.json. Call initialize()
// before the first OpenVR PollNextEvent or UpdateActionState call.
class SteamVRInput final {
public:
    explicit SteamVRInput(OpenVRRuntime& runtime);
    ~SteamVRInput();

    SteamVRInput(const SteamVRInput&) = delete;
    SteamVRInput& operator=(const SteamVRInput&) = delete;

    [[nodiscard]] bool initialize(
        const std::filesystem::path& actionManifestPath,
        std::string* error = nullptr);
    void shutdown() noexcept;
    [[nodiscard]] bool isInitialized() const noexcept;

    // keyboardHands should contain every hand whose ray currently intersects
    // the keyboard plus every hand with a captured flick or grab. Each hand is
    // activated with its own device restriction; the action set is never made
    // unrestricted when both hands intersect.
    [[nodiscard]] bool update(
        HandMask keyboardHands,
        std::string* error = nullptr);
    [[nodiscard]] bool update(
        HandMask keyboardHands,
        std::optional<Hand> preferredPointerHand,
        std::string* error = nullptr);
    // observedHands are sampled at normal priority even while their pointers
    // are outside the keyboard. keyboardHands are the intersecting/captured
    // subset promoted to overlay priority. Keeping the action state observed
    // prevents an already-held grab from becoming a false press when its
    // pointer later enters the overlay.
    [[nodiscard]] bool update(
        HandMask keyboardHands,
        HandMask observedHands,
        std::optional<Hand> preferredPointerHand,
        std::string* error = nullptr);
    [[nodiscard]] const FrameActionState& actions() const noexcept;
    [[nodiscard]] HandActionState actionsFor(Hand hand) const noexcept;

    [[nodiscard]] bool triggerHaptic(
        Hand hand,
        float durationSeconds = 0.025F,
        float frequency = 120.0F,
        float amplitude = 0.35F,
        std::string* error = nullptr);

    // Primary/grab are always submitted at OpenVR's overlay-global priority to
    // avoid duplicate scene input. Enabling this additionally activates a
    // full-hand blocker for the preferred pointing hand. SteamVR's
    // "Experimental overlay input overrides" developer setting must be enabled
    // for either kind of scene-application suppression to take effect. The
    // toggle action normally stays at priority zero so its stick/trackpad source
    // does not also suppress locomotion; it is promoted while the left hand is
    // already covered by the full-hand blocker.
    void setSceneInputSuppressionEnabled(bool enabled) noexcept;
    [[nodiscard]] bool sceneInputSuppressionEnabled() const noexcept;
    [[nodiscard]] bool shouldSuppressSceneInput(Hand hand) const noexcept;
    [[nodiscard]] std::optional<Hand> activeSuppressionHand() const noexcept;
    [[nodiscard]] HandMask activeKeyboardHands() const noexcept;

    [[nodiscard]] bool openBindingUI(
        bool showOnDesktop = false,
        std::string* error = nullptr);

    [[nodiscard]] const SteamVRActionHandles& handles() const noexcept;
    [[nodiscard]] const std::filesystem::path& manifestPath() const noexcept;
    [[nodiscard]] const std::string& lastError() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace flickvr::vr
