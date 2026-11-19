#include "OverlayManager.h"

#include "OpenVRAccess.h"
#include "OpenVRConversion.h"
#include "OverlayPlacement.h"
#include "OpenVRRuntime.h"
#include "VrMath.h"

#include <openvr.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace flickvr::vr {

namespace {

void writeError(const std::string& message, std::string* output) {
    if (output != nullptr) {
        *output = message;
    }
}

[[nodiscard]] bool isValidHandle(::vr::VROverlayHandle_t handle) noexcept {
    return handle != ::vr::k_ulOverlayHandleInvalid;
}

[[nodiscard]] bool isValidPlacement(const FrontPlacement& placement) noexcept {
    return std::isfinite(placement.forwardMeters) &&
        placement.forwardMeters > 0.0F &&
        std::isfinite(placement.verticalOffsetMeters) &&
        std::isfinite(placement.overlayWidthMeters) &&
        placement.overlayWidthMeters > 0.0F &&
        std::isfinite(placement.maximumAbsolutePitchDegrees) &&
        std::isfinite(placement.tiltDegrees);
}

} // namespace

struct OverlayManager::Impl {
    explicit Impl(OpenVRRuntime& runtimeRef) : runtime(runtimeRef) {}

    OpenVRRuntime& runtime;
    OverlayConfiguration configuration{};
    ::vr::VROverlayHandle_t keyboard{::vr::k_ulOverlayHandleInvalid};
    ::vr::VROverlayHandle_t dashboard{::vr::k_ulOverlayHandleInvalid};
    ::vr::VROverlayHandle_t dashboardThumbnail{
        ::vr::k_ulOverlayHandleInvalid};
    std::string lastError{};
    float dashboardPointerX{};
    float dashboardPointerY{};
    bool dashboardPointerValid{};
    bool dashboardPrimaryHeld{};
    std::uint64_t textRequestId{};

    [[nodiscard]] ::vr::IVROverlay* overlay() const noexcept {
        return detail::OpenVRAccess::overlay(runtime);
    }

    bool fail(
        const char* operation,
        ::vr::EVROverlayError error,
        std::string* output) {
        lastError = operation;
        lastError += ": ";
        lastError += detail::overlayErrorMessage(overlay(), error);
        writeError(lastError, output);
        return false;
    }

    [[nodiscard]] bool check(
        const char* operation,
        ::vr::EVROverlayError error,
        std::string* output) {
        if (error != ::vr::VROverlayError_None) {
            return fail(operation, error, output);
        }
        lastError.clear();
        writeError({}, output);
        return true;
    }

    [[nodiscard]] bool submitTexture(
        ::vr::VROverlayHandle_t handle,
        void* texture,
        TextureColorSpace colorSpace,
        const char* operation,
        std::string* output) {
        if (!isValidHandle(handle) || overlay() == nullptr) {
            lastError = "OverlayManager is not initialized";
            writeError(lastError, output);
            return false;
        }
        if (texture == nullptr) {
            lastError = "A null Direct3D 11 texture was submitted";
            writeError(lastError, output);
            return false;
        }

        const ::vr::Texture_t openVrTexture{
            texture,
            ::vr::TextureType_DirectX,
            detail::toOpenVRColorSpace(colorSpace),
        };
        return check(
            operation,
            overlay()->SetOverlayTexture(handle, &openVrTexture),
            output);
    }
};

OverlayManager::OverlayManager(OpenVRRuntime& runtime)
    : impl_(std::make_unique<Impl>(runtime)) {}

OverlayManager::~OverlayManager() {
    shutdown();
}

bool OverlayManager::initialize(
    const OverlayConfiguration& configuration,
    std::string* error) {
    if (isInitialized()) {
        writeError({}, error);
        return true;
    }
    if (!impl_->runtime.isInitialized() || impl_->overlay() == nullptr) {
        impl_->lastError = "OpenVR runtime is not initialized";
        writeError(impl_->lastError, error);
        return false;
    }
    if (configuration.keyboardKey.empty() ||
        configuration.dashboardKey.empty()) {
        impl_->lastError = "Overlay keys must not be empty";
        writeError(impl_->lastError, error);
        return false;
    }
    if (!isValidPlacement(configuration.frontPlacement) ||
        !std::isfinite(configuration.dashboardWidthMeters) ||
        configuration.dashboardWidthMeters <= 0.0F ||
        configuration.keyboardPixelWidth == 0U ||
        configuration.keyboardPixelHeight == 0U ||
        configuration.dashboardPixelWidth == 0U ||
        configuration.dashboardPixelHeight == 0U) {
        impl_->lastError = "Overlay dimensions and placement are invalid";
        writeError(impl_->lastError, error);
        return false;
    }

    impl_->configuration = configuration;
    ::vr::EVROverlayError overlayError = impl_->overlay()->CreateOverlay(
        configuration.keyboardKey.c_str(),
        configuration.keyboardName.c_str(),
        &impl_->keyboard);
    if (overlayError != ::vr::VROverlayError_None) {
        return impl_->fail("Create keyboard overlay", overlayError, error);
    }

    overlayError = impl_->overlay()->SetOverlayWidthInMeters(
        impl_->keyboard,
        std::max(configuration.frontPlacement.overlayWidthMeters, 0.05F));
    if (overlayError == ::vr::VROverlayError_None) {
        // PointerInteractor performs explicit ray tests; disabling native mouse
        // input avoids a second SteamVR pointer producing duplicate presses.
        overlayError = impl_->overlay()->SetOverlayInputMethod(
            impl_->keyboard,
            ::vr::VROverlayInputMethod_None);
    }
    if (overlayError != ::vr::VROverlayError_None) {
        impl_->fail("Configure keyboard overlay", overlayError, error);
        shutdown();
        return false;
    }

    overlayError = impl_->overlay()->CreateDashboardOverlay(
        configuration.dashboardKey.c_str(),
        configuration.dashboardName.c_str(),
        &impl_->dashboard,
        &impl_->dashboardThumbnail);
    if (overlayError != ::vr::VROverlayError_None) {
        impl_->fail("Create dashboard overlay", overlayError, error);
        shutdown();
        return false;
    }

    overlayError = impl_->overlay()->SetOverlayWidthInMeters(
        impl_->dashboard,
        std::max(configuration.dashboardWidthMeters, 0.1F));
    if (overlayError == ::vr::VROverlayError_None) {
        overlayError = impl_->overlay()->SetOverlayInputMethod(
            impl_->dashboard,
            ::vr::VROverlayInputMethod_Mouse);
    }
    if (overlayError == ::vr::VROverlayError_None) {
        overlayError = impl_->overlay()->SetOverlayFlag(
            impl_->dashboard,
            ::vr::VROverlayFlags_SendVRDiscreteScrollEvents,
            true);
    }
    if (overlayError == ::vr::VROverlayError_None) {
        const ::vr::HmdVector2_t scale{
            static_cast<float>(configuration.dashboardPixelWidth),
            static_cast<float>(configuration.dashboardPixelHeight),
        };
        overlayError = impl_->overlay()->SetOverlayMouseScale(
            impl_->dashboard,
            &scale);
    }
    if (overlayError != ::vr::VROverlayError_None) {
        impl_->fail("Configure dashboard overlay", overlayError, error);
        shutdown();
        return false;
    }

    // The normal keyboard overlay starts hidden. Dashboard overlays are shown
    // and hidden by SteamVR when their Dashboard tab becomes active.
    impl_->overlay()->HideOverlay(impl_->keyboard);
    impl_->lastError.clear();
    writeError({}, error);
    return true;
}

void OverlayManager::shutdown() noexcept {
    if (!impl_) {
        return;
    }

    ::vr::IVROverlay* overlay = impl_->overlay();
    if (overlay != nullptr) {
        if (impl_->textRequestId != 0) overlay->HideKeyboard();
        if (isValidHandle(impl_->keyboard)) {
            overlay->ClearOverlayTexture(impl_->keyboard);
            overlay->DestroyOverlay(impl_->keyboard);
        }
        if (isValidHandle(impl_->dashboardThumbnail)) {
            overlay->ClearOverlayTexture(impl_->dashboardThumbnail);
            overlay->DestroyOverlay(impl_->dashboardThumbnail);
        }
        if (isValidHandle(impl_->dashboard)) {
            overlay->ClearOverlayTexture(impl_->dashboard);
            overlay->DestroyOverlay(impl_->dashboard);
        }
    }

    impl_->keyboard = ::vr::k_ulOverlayHandleInvalid;
    impl_->dashboard = ::vr::k_ulOverlayHandleInvalid;
    impl_->dashboardThumbnail = ::vr::k_ulOverlayHandleInvalid;
    impl_->dashboardPointerX = 0.0F;
    impl_->dashboardPointerY = 0.0F;
    impl_->dashboardPointerValid = false;
    impl_->dashboardPrimaryHeld = false;
    impl_->textRequestId = 0;
}

bool OverlayManager::isInitialized() const noexcept {
    return impl_ && isValidHandle(impl_->keyboard) &&
        isValidHandle(impl_->dashboard) &&
        isValidHandle(impl_->dashboardThumbnail);
}

const OverlayConfiguration& OverlayManager::configuration() const noexcept {
    return impl_->configuration;
}

bool OverlayManager::submitKeyboardTexture(
    void* texture,
    TextureColorSpace colorSpace,
    std::string* error) {
    return impl_->submitTexture(
        impl_->keyboard,
        texture,
        colorSpace,
        "Submit keyboard Direct3D 11 texture",
        error);
}

bool OverlayManager::submitDashboardTexture(
    void* texture,
    TextureColorSpace colorSpace,
    std::string* error) {
    return impl_->submitTexture(
        impl_->dashboard,
        texture,
        colorSpace,
        "Submit dashboard Direct3D 11 texture",
        error);
}

bool OverlayManager::submitDashboardThumbnailTexture(
    void* texture,
    TextureColorSpace colorSpace,
    std::string* error) {
    return impl_->submitTexture(
        impl_->dashboardThumbnail,
        texture,
        colorSpace,
        "Submit dashboard thumbnail Direct3D 11 texture",
        error);
}

void OverlayManager::clearTextures() noexcept {
    if (!isInitialized() || impl_->overlay() == nullptr) {
        return;
    }
    impl_->overlay()->ClearOverlayTexture(impl_->keyboard);
    impl_->overlay()->ClearOverlayTexture(impl_->dashboard);
    impl_->overlay()->ClearOverlayTexture(impl_->dashboardThumbnail);
}

bool OverlayManager::setKeyboardVisible(
    bool visible,
    bool recenterWhenShowing,
    std::string* error) {
    if (!isInitialized() || impl_->overlay() == nullptr) {
        impl_->lastError = "OverlayManager is not initialized";
        writeError(impl_->lastError, error);
        return false;
    }

    if (visible && recenterWhenShowing && !placeKeyboardInFrontOfHmd(error)) {
        return false;
    }

    const ::vr::EVROverlayError overlayError = visible
        ? impl_->overlay()->ShowOverlay(impl_->keyboard)
        : impl_->overlay()->HideOverlay(impl_->keyboard);
    return impl_->check(
        visible ? "Show keyboard overlay" : "Hide keyboard overlay",
        overlayError,
        error);
}

bool OverlayManager::toggleKeyboard(
    bool recenterWhenShowing,
    std::string* error) {
    return setKeyboardVisible(
        !isKeyboardVisible(),
        recenterWhenShowing,
        error);
}

bool OverlayManager::isKeyboardVisible() const noexcept {
    return isInitialized() && impl_->overlay() != nullptr &&
        impl_->overlay()->IsOverlayVisible(impl_->keyboard);
}

bool OverlayManager::isDashboardVisible() const noexcept {
    return isInitialized() && impl_->overlay() != nullptr &&
        impl_->overlay()->IsDashboardVisible();
}

bool OverlayManager::isDashboardActive() const noexcept {
    return isInitialized() && impl_->overlay() != nullptr &&
        impl_->overlay()->IsActiveDashboardOverlay(impl_->dashboard);
}

bool OverlayManager::placeKeyboardInFrontOfHmd(std::string* error) {
    if (!isInitialized()) {
        impl_->lastError = "OverlayManager is not initialized";
        writeError(impl_->lastError, error);
        return false;
    }
    if (!impl_->runtime.updateTrackedPoses(0.0F, error)) {
        impl_->lastError = impl_->runtime.lastError();
        return false;
    }

    const TrackedPose hmd = impl_->runtime.hmdPose();
    if (!hmd.valid) {
        impl_->lastError = "The HMD pose is not currently valid";
        writeError(impl_->lastError, error);
        return false;
    }

    const FrontPlacement& placement = impl_->configuration.frontPlacement;
    const Vec3 rawForward = math::normalized(
        math::multiply(math::basisZ(hmd.transform), -1.0F));
    const float yaw = std::atan2(rawForward.x, -rawForward.z);
    const float rawPitch = std::asin(math::clampUnit(rawForward.y));
    const float pitchLimit = math::degreesToRadians(std::clamp(
        std::abs(placement.maximumAbsolutePitchDegrees),
        0.0F,
        89.0F));
    const float tilt = math::degreesToRadians(std::clamp(
        placement.tiltDegrees,
        -30.0F,
        30.0F));
    const float placementPitch = std::clamp(
        rawPitch,
        -pitchLimit,
        pitchLimit);
    const float overlayPitch = std::clamp(
        placementPitch + tilt,
        -pitchLimit,
        pitchLimit);
    const float cosPitch = std::cos(overlayPitch);

    // Construct a fresh yaw/pitch basis to remove HMD roll completely.
    const Vec3 forward{
        std::sin(yaw) * cosPitch,
        std::sin(overlayPitch),
        -std::cos(yaw) * cosPitch,
    };
    constexpr Vec3 worldUp{0.0F, 1.0F, 0.0F};
    const Vec3 right = math::normalized(
        math::cross(forward, worldUp),
        {1.0F, 0.0F, 0.0F});
    const Vec3 up = math::normalized(
        math::cross(right, forward),
        worldUp);
    const Vec3 backward = math::multiply(forward, -1.0F);

    const float cosPlacementPitch = std::cos(placementPitch);
    const Vec3 placementForward{
        std::sin(yaw) * cosPlacementPitch,
        std::sin(placementPitch),
        -std::cos(yaw) * cosPlacementPitch,
    };
    Vec3 position = math::add(
        math::translation(hmd.transform),
        math::multiply(
            placementForward,
            std::max(placement.forwardMeters, 0.05F)));
    position = placement::withVerticalOffset(
        position, placement.verticalOffsetMeters);

    if (!setKeyboardWidth(
            std::max(placement.overlayWidthMeters, 0.05F),
            error)) {
        return false;
    }
    return setKeyboardTransform(
        math::fromBasis(right, up, backward, position),
        error);
}

bool OverlayManager::setFrontPlacement(
    const FrontPlacement& placement,
    bool recenterIfVisible,
    std::string* error) {
    if (!isValidPlacement(placement)) {
        impl_->lastError = "Front placement values are invalid";
        writeError(impl_->lastError, error);
        return false;
    }

    impl_->configuration.frontPlacement = placement;
    if (recenterIfVisible && isKeyboardVisible()) {
        return placeKeyboardInFrontOfHmd(error);
    }

    impl_->lastError.clear();
    writeError({}, error);
    return true;
}

bool OverlayManager::setKeyboardTransform(
    const RigidTransform& transform,
    std::string* error) {
    if (!isInitialized() || impl_->overlay() == nullptr) {
        impl_->lastError = "OverlayManager is not initialized";
        writeError(impl_->lastError, error);
        return false;
    }
    const ::vr::HmdMatrix34_t matrix = detail::toOpenVR(transform);
    return impl_->check(
        "Set keyboard overlay transform",
        impl_->overlay()->SetOverlayTransformAbsolute(
            impl_->keyboard,
            detail::toOpenVROrigin(impl_->runtime.trackingOrigin()),
            &matrix),
        error);
}

std::optional<RigidTransform> OverlayManager::keyboardTransform(
    std::string* error) {
    if (!isInitialized() || impl_->overlay() == nullptr) {
        impl_->lastError = "OverlayManager is not initialized";
        writeError(impl_->lastError, error);
        return std::nullopt;
    }

    ::vr::ETrackingUniverseOrigin origin{};
    ::vr::HmdMatrix34_t matrix{};
    const ::vr::EVROverlayError overlayError =
        impl_->overlay()->GetOverlayTransformAbsolute(
            impl_->keyboard,
            &origin,
            &matrix);
    if (overlayError != ::vr::VROverlayError_None) {
        impl_->fail("Get keyboard overlay transform", overlayError, error);
        return std::nullopt;
    }

    impl_->lastError.clear();
    writeError({}, error);
    return detail::fromOpenVR(matrix);
}

bool OverlayManager::setKeyboardWidth(
    float widthMeters,
    std::string* error) {
    if (!isInitialized() || impl_->overlay() == nullptr) {
        impl_->lastError = "OverlayManager is not initialized";
        writeError(impl_->lastError, error);
        return false;
    }
    if (!std::isfinite(widthMeters) || widthMeters <= 0.0F) {
        impl_->lastError = "Keyboard overlay width must be positive";
        writeError(impl_->lastError, error);
        return false;
    }
    const bool succeeded = impl_->check(
        "Set keyboard overlay width",
        impl_->overlay()->SetOverlayWidthInMeters(
            impl_->keyboard,
            widthMeters),
        error);
    if (succeeded) {
        impl_->configuration.frontPlacement.overlayWidthMeters = widthMeters;
    }
    return succeeded;
}

bool OverlayManager::setKeyboardTextureSize(
    std::uint32_t width,
    std::uint32_t height,
    std::string* error) {
    if (!isInitialized()) {
        impl_->lastError = "OverlayManager is not initialized";
        writeError(impl_->lastError, error);
        return false;
    }
    if (width == 0U || height == 0U) {
        impl_->lastError = "Keyboard overlay texture size must be non-zero";
        writeError(impl_->lastError, error);
        return false;
    }
    impl_->configuration.keyboardPixelWidth = width;
    impl_->configuration.keyboardPixelHeight = height;
    impl_->lastError.clear();
    writeError({}, error);
    return true;
}

bool OverlayManager::setKeyboardOpacity(
    float opacity,
    std::string* error) {
    if (!isInitialized() || impl_->overlay() == nullptr) {
        impl_->lastError = "OverlayManager is not initialized";
        writeError(impl_->lastError, error);
        return false;
    }
    if (!std::isfinite(opacity)) {
        impl_->lastError = "Keyboard overlay opacity must be finite";
        writeError(impl_->lastError, error);
        return false;
    }
    return impl_->check(
        "Set keyboard overlay opacity",
        impl_->overlay()->SetOverlayAlpha(
            impl_->keyboard,
            std::clamp(opacity, 0.0F, 1.0F)),
        error);
}

bool OverlayManager::setDashboardOpacity(
    float opacity,
    std::string* error) {
    if (!isInitialized() || impl_->overlay() == nullptr) {
        impl_->lastError = "OverlayManager is not initialized";
        writeError(impl_->lastError, error);
        return false;
    }
    if (!std::isfinite(opacity)) {
        impl_->lastError = "Dashboard overlay opacity must be finite";
        writeError(impl_->lastError, error);
        return false;
    }
    return impl_->check(
        "Set dashboard overlay opacity",
        impl_->overlay()->SetOverlayAlpha(
            impl_->dashboard,
            std::clamp(opacity, 0.0F, 1.0F)),
        error);
}

bool OverlayManager::setDashboardMouseScale(
    std::uint32_t width,
    std::uint32_t height,
    std::string* error) {
    if (!isInitialized() || impl_->overlay() == nullptr) {
        impl_->lastError = "OverlayManager is not initialized";
        writeError(impl_->lastError, error);
        return false;
    }
    if (width == 0U || height == 0U) {
        impl_->lastError = "Dashboard mouse scale must be non-zero";
        writeError(impl_->lastError, error);
        return false;
    }
    const ::vr::HmdVector2_t scale{
        static_cast<float>(width),
        static_cast<float>(height),
    };
    const bool succeeded = impl_->check(
        "Set dashboard mouse scale",
        impl_->overlay()->SetOverlayMouseScale(impl_->dashboard, &scale),
        error);
    if (succeeded) {
        impl_->configuration.dashboardPixelWidth = width;
        impl_->configuration.dashboardPixelHeight = height;
    }
    return succeeded;
}

bool OverlayManager::showDashboardTextInput(
    std::uint64_t requestId, const std::string& text, const std::string& label,
    std::uint32_t maximumCharacters, std::string* error) {
    if (!isInitialized() || impl_->overlay() == nullptr || requestId == 0 ||
        impl_->textRequestId != 0 || maximumCharacters == 0 || maximumCharacters > 253) {
        writeError("Dashboard text input is unavailable", error);
        return false;
    }
    if (!impl_->check("Open Dashboard keyboard", impl_->overlay()->ShowKeyboardForOverlay(
            impl_->dashboard, ::vr::k_EGamepadTextInputModeNormal,
            ::vr::k_EGamepadTextInputLineModeSingleLine, ::vr::KeyboardFlag_Modal,
            label.c_str(), maximumCharacters, text.c_str(), requestId), error)) return false;
    impl_->textRequestId = requestId;
    impl_->dashboardPrimaryHeld = false;
    return true;
}

std::optional<DashboardInputFrame> OverlayManager::pollDashboardInput(
    std::string* error) {
    if (!isInitialized() || impl_->overlay() == nullptr) {
        impl_->lastError = "OverlayManager is not initialized";
        writeError(impl_->lastError, error);
        return std::nullopt;
    }
    if (!impl_->runtime.actionManifestConfigured()) {
        impl_->lastError =
            "Set the SteamVR action manifest before polling overlay events";
        writeError(impl_->lastError, error);
        return std::nullopt;
    }

    DashboardInputFrame frame{};
    frame.pixelX = impl_->dashboardPointerX;
    frame.pixelY = impl_->dashboardPointerY;
    frame.pointerValid = impl_->dashboardPointerValid;
    frame.primaryHeld = impl_->dashboardPrimaryHeld;
    frame.active = impl_->overlay()->IsActiveDashboardOverlay(impl_->dashboard);

    const float width = static_cast<float>(
        impl_->configuration.dashboardPixelWidth);
    const float height = static_cast<float>(
        impl_->configuration.dashboardPixelHeight);
    const auto updatePointer = [&](const ::vr::VREvent_Mouse_t& mouse) {
        impl_->dashboardPointerX = std::clamp(mouse.x, 0.0F, width);
        impl_->dashboardPointerY = std::clamp(height - mouse.y, 0.0F, height);
        impl_->dashboardPointerValid = true;
        frame.pixelX = impl_->dashboardPointerX;
        frame.pixelY = impl_->dashboardPointerY;
        frame.pointerValid = true;
    };

    ::vr::VREvent_t event{};
    while (impl_->overlay()->PollNextOverlayEvent(
        impl_->dashboard,
        &event,
        sizeof(event))) {
        switch (event.eventType) {
        case ::vr::VREvent_KeyboardDone:
            if (impl_->textRequestId != 0 &&
                event.data.keyboard.uUserValue == impl_->textRequestId) {
                // The request is limited to 253 characters, including four-byte UTF-8.
                std::array<char, 1024> text{};
                impl_->overlay()->GetKeyboardText(text.data(), static_cast<std::uint32_t>(text.size()));
                text.back() = '\0';
                frame.textRequestId = impl_->textRequestId;
                frame.completedText = text.data();
                impl_->textRequestId = 0;
            }
            break;
        case ::vr::VREvent_MouseMove:
            updatePointer(event.data.mouse);
            frame.pointerMoved = true;
            break;
        case ::vr::VREvent_MouseButtonDown:
            updatePointer(event.data.mouse);
            if ((event.data.mouse.button & ::vr::VRMouseButton_Left) != 0U) {
                frame.primaryPressed = !impl_->dashboardPrimaryHeld;
                impl_->dashboardPrimaryHeld = true;
            }
            break;
        case ::vr::VREvent_MouseButtonUp:
            updatePointer(event.data.mouse);
            if ((event.data.mouse.button & ::vr::VRMouseButton_Left) != 0U) {
                frame.primaryReleased = impl_->dashboardPrimaryHeld;
                impl_->dashboardPrimaryHeld = false;
            }
            break;
        case ::vr::VREvent_ScrollDiscrete:
        case ::vr::VREvent_ScrollSmooth:
            frame.scrollDeltaX += event.data.scroll.xdelta;
            frame.scrollDeltaY += event.data.scroll.ydelta;
            break;
        case ::vr::VREvent_DashboardActivated:
            frame.activated = true;
            break;
        case ::vr::VREvent_DashboardDeactivated:
            frame.deactivated = true;
            if (impl_->dashboardPrimaryHeld) {
                frame.primaryReleased = true;
                impl_->dashboardPrimaryHeld = false;
            }
            break;
        case ::vr::VREvent_Quit:
        case ::vr::VREvent_DriverRequestedQuit:
            frame.quitRequested = true;
            break;
        case ::vr::VREvent_ProcessQuit:
            // Notification about another OpenVR client exiting, not an exit
            // command for this dashboard overlay.
            break;
        default:
            break;
        }
    }

    frame.primaryHeld = impl_->dashboardPrimaryHeld;
    frame.active = impl_->overlay()->IsActiveDashboardOverlay(impl_->dashboard);
    if (impl_->textRequestId != 0 &&
        (!frame.active || impl_->runtime.keyboardClosedForOverlay(impl_->dashboard))) {
        frame.textRequestId = impl_->textRequestId;
        impl_->textRequestId = 0;
        if (!frame.active) impl_->overlay()->HideKeyboard();
    }
    if (!frame.active && impl_->dashboardPrimaryHeld) {
        frame.primaryReleased = true;
        frame.primaryHeld = false;
        impl_->dashboardPrimaryHeld = false;
    }

    impl_->lastError.clear();
    writeError({}, error);
    return frame;
}

std::uint64_t OverlayManager::keyboardHandle() const noexcept {
    return impl_ ? impl_->keyboard : ::vr::k_ulOverlayHandleInvalid;
}

std::uint64_t OverlayManager::dashboardHandle() const noexcept {
    return impl_ ? impl_->dashboard : ::vr::k_ulOverlayHandleInvalid;
}

std::uint64_t OverlayManager::dashboardThumbnailHandle() const noexcept {
    return impl_ ? impl_->dashboardThumbnail : ::vr::k_ulOverlayHandleInvalid;
}

const std::string& OverlayManager::lastError() const noexcept {
    static const std::string empty{};
    return impl_ ? impl_->lastError : empty;
}

} // namespace flickvr::vr
