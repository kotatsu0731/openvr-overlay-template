#pragma once

#include "VrTypes.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace flickvr::vr {

class OpenVRRuntime;

struct FrontPlacement {
    float forwardMeters{0.65F};
    // Signed world-up offset from the HMD. Negative values place the keyboard
    // below the HMD; positive values place it above.
    float verticalOffsetMeters{-0.22F};
    float overlayWidthMeters{0.60F};
    float maximumAbsolutePitchDegrees{30.0F};
    float tiltDegrees{0.0F};
};

struct OverlayConfiguration {
    std::string keyboardKey{"com.example.openvroverlay.world"};
    std::string keyboardName{"OpenVR Overlay Template"};
    std::string dashboardKey{"com.example.openvroverlay.dashboard"};
    std::string dashboardName{"OpenVR Overlay Template"};
    float dashboardWidthMeters{2.0F};
    std::uint32_t keyboardPixelWidth{1600U};
    std::uint32_t keyboardPixelHeight{900U};
    std::uint32_t dashboardPixelWidth{1600U};
    std::uint32_t dashboardPixelHeight{900U};
    FrontPlacement frontPlacement{};
};

struct DashboardInputFrame {
    // Top-left-origin pixel coordinates ready for ImGui::AddMousePosEvent.
    float pixelX{};
    float pixelY{};
    bool pointerValid{};
    bool pointerMoved{};
    bool primaryHeld{};
    bool primaryPressed{};
    bool primaryReleased{};
    float scrollDeltaX{};
    float scrollDeltaY{};
    bool activated{};
    bool deactivated{};
    bool active{};
    bool quitRequested{};
    // A nonzero request identifies either Done (text) or cancellation (nullopt).
    std::uint64_t textRequestId{};
    std::optional<std::string> completedText;
};

// Creates and owns both OpenVR overlays. Texture submission accepts an
// ID3D11Texture2D pointer as void* so the public header does not require D3D11.
class OverlayManager final {
public:
    explicit OverlayManager(OpenVRRuntime& runtime);
    ~OverlayManager();

    OverlayManager(const OverlayManager&) = delete;
    OverlayManager& operator=(const OverlayManager&) = delete;

    [[nodiscard]] bool initialize(
        const OverlayConfiguration& configuration = {},
        std::string* error = nullptr);
    void shutdown() noexcept;

    [[nodiscard]] bool isInitialized() const noexcept;
    [[nodiscard]] const OverlayConfiguration& configuration() const noexcept;

    // texture must be an ID3D11Texture2D created on the compositor adapter. It
    // must stay alive until another texture is submitted or clearTextures() /
    // shutdown() is called.
    [[nodiscard]] bool submitKeyboardTexture(
        void* texture,
        TextureColorSpace colorSpace = TextureColorSpace::Gamma,
        std::string* error = nullptr);
    [[nodiscard]] bool submitDashboardTexture(
        void* texture,
        TextureColorSpace colorSpace = TextureColorSpace::Gamma,
        std::string* error = nullptr);
    [[nodiscard]] bool submitDashboardThumbnailTexture(
        void* texture,
        TextureColorSpace colorSpace = TextureColorSpace::Gamma,
        std::string* error = nullptr);
    void clearTextures() noexcept;

    [[nodiscard]] bool setKeyboardVisible(
        bool visible,
        bool recenterWhenShowing = true,
        std::string* error = nullptr);
    [[nodiscard]] bool toggleKeyboard(
        bool recenterWhenShowing = true,
        std::string* error = nullptr);
    [[nodiscard]] bool isKeyboardVisible() const noexcept;
    [[nodiscard]] bool isDashboardVisible() const noexcept;
    [[nodiscard]] bool isDashboardActive() const noexcept;

    [[nodiscard]] bool placeKeyboardInFrontOfHmd(
        std::string* error = nullptr);
    [[nodiscard]] bool setFrontPlacement(
        const FrontPlacement& placement,
        bool recenterIfVisible = false,
        std::string* error = nullptr);
    [[nodiscard]] bool setKeyboardTransform(
        const RigidTransform& transform,
        std::string* error = nullptr);
    [[nodiscard]] std::optional<RigidTransform> keyboardTransform(
        std::string* error = nullptr);
    [[nodiscard]] bool setKeyboardWidth(
        float widthMeters,
        std::string* error = nullptr);
    // Updates the aspect ratio used by ray projection after the keyboard
    // renderer changes size. OpenVR derives the displayed height from the
    // submitted texture and the configured overlay width.
    [[nodiscard]] bool setKeyboardTextureSize(
        std::uint32_t width,
        std::uint32_t height,
        std::string* error = nullptr);
    [[nodiscard]] bool setKeyboardOpacity(
        float opacity,
        std::string* error = nullptr);
    [[nodiscard]] bool setDashboardOpacity(
        float opacity,
        std::string* error = nullptr);
    [[nodiscard]] bool setDashboardMouseScale(
        std::uint32_t width,
        std::uint32_t height,
        std::string* error = nullptr);

    // Drains this Dashboard overlay's native event queue. SteamVR reports
    // mouse Y from the lower edge; this method converts it to ImGui's top-left
    // pixel convention using the current dashboard mouse scale.
    [[nodiscard]] std::optional<DashboardInputFrame> pollDashboardInput(
        std::string* error = nullptr);
    [[nodiscard]] bool showDashboardTextInput(
        std::uint64_t requestId, const std::string& text, const std::string& label,
        std::uint32_t maximumCharacters, std::string* error = nullptr);

    [[nodiscard]] std::uint64_t keyboardHandle() const noexcept;
    [[nodiscard]] std::uint64_t dashboardHandle() const noexcept;
    [[nodiscard]] std::uint64_t dashboardThumbnailHandle() const noexcept;
    [[nodiscard]] const std::string& lastError() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace flickvr::vr
