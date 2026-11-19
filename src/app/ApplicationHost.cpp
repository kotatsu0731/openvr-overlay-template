#include "app/ApplicationHost.h"
#include "app/SystemTray.h"

#include "config/Settings.h"
#include "diagnostics/Logger.h"
#include "ui/OverlayRenderer.h"
#include "ui/SmoothScroll.h"
#include "ui/Theme.h"
#include "vr/OpenVRRuntime.h"
#include "vr/OverlayDistanceMotion.h"
#include "vr/OverlayDragger.h"
#include "vr/OverlayManager.h"
#include "vr/OverlayRelativePose.h"
#include "vr/PointerInteractor.h"
#include "vr/SteamVRInput.h"
#include "vr/VrTypes.h"

#include <imgui.h>
#include <openvr.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <shlobj.h>
#include <windows.h>
#include <wrl/client.h>

namespace flickvr::app {
namespace {

using Clock = std::chrono::steady_clock;
using Microsoft::WRL::ComPtr;

constexpr std::uint32_t kWorldOverlayWidth = 1280;
constexpr std::uint32_t kWorldOverlayHeight = 720;
constexpr std::uint32_t kDashboardWidth = 1600;
constexpr std::uint32_t kDashboardHeight = 900;
constexpr float kWorldFontPixels = 36.0F;
constexpr float kDashboardFontPixels = 36.0F;
constexpr float kOverlayDistanceSpeedMetersPerSecond = 1.10F;
constexpr float kScrollPixelsPerNormalizedUnit = 920.0F;
constexpr std::string_view kApplicationKey = "com.example.openvroverlay.template";

ui::ThemePalette DefaultPalette() noexcept {
    ui::ThemePalette palette{};
    palette.backdrop = {0.09F, 0.10F, 0.13F, 0.94F};
    palette.surface = {0.14F, 0.16F, 0.20F, 0.96F};
    palette.surfaceRaised = {0.19F, 0.22F, 0.28F, 0.98F};
    palette.outline = {0.30F, 0.34F, 0.42F, 0.85F};
    palette.controlOutline = {0.38F, 0.43F, 0.53F, 0.90F};
    palette.inputSurface = {0.11F, 0.13F, 0.17F, 1.00F};
    palette.text = {0.94F, 0.95F, 0.98F, 1.00F};
    palette.textMuted = {0.66F, 0.70F, 0.78F, 1.00F};
    palette.textOnAccent = {1.00F, 1.00F, 1.00F, 1.00F};
    palette.accent = {0.33F, 0.56F, 0.96F, 1.00F};
    palette.accentSecondary = {0.42F, 0.65F, 1.00F, 1.00F};
    palette.accentPressed = {0.24F, 0.44F, 0.82F, 1.00F};
    palette.accentSurfaceTint = {0.33F, 0.56F, 0.96F, 0.22F};
    palette.surfaceTint = {1.00F, 1.00F, 1.00F, 0.05F};
    palette.backgroundGlow = {0.33F, 0.56F, 0.96F, 0.16F};
    palette.noiseTint = {1.00F, 1.00F, 1.00F, 0.02F};
    palette.success = {0.26F, 0.78F, 0.49F, 1.00F};
    palette.warning = {0.95F, 0.71F, 0.25F, 1.00F};
    palette.danger = {0.92F, 0.34F, 0.37F, 1.00F};
    return palette;
}

std::filesystem::path ExecutablePath() {
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return std::filesystem::current_path() / "OpenVROverlayTemplate.exe";
    }
    buffer.resize(length);
    return std::filesystem::path(buffer);
}

std::filesystem::path LocalSettingsPath() {
    PWSTR localAppData = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &localAppData))) {
        const std::filesystem::path result =
            std::filesystem::path(localAppData) / "OpenVROverlayTemplate" / "settings.toml";
        CoTaskMemFree(localAppData);
        return result;
    }
    return ExecutablePath().parent_path() / "settings.toml";
}

std::filesystem::path ResourcePath(std::string_view fileName) {
    return ExecutablePath().parent_path() / "resources" / "openvr" / fileName;
}

std::string PathAsUtf8(const std::filesystem::path& path) {
#if defined(__cpp_lib_char8_t)
    const auto encoded = path.u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
#else
    return path.u8string();
#endif
}

bool SameWindowsPath(
    const std::filesystem::path& first,
    const std::filesystem::path& second) noexcept {
    std::error_code firstError;
    std::error_code secondError;
    const std::wstring firstPath =
        std::filesystem::absolute(first, firstError).lexically_normal().native();
    const std::wstring secondPath =
        std::filesystem::absolute(second, secondError).lexically_normal().native();
    if (firstError || secondError || firstPath.empty() || secondPath.empty()) {
        return false;
    }
    return CompareStringOrdinal(
               firstPath.c_str(), -1, secondPath.c_str(), -1, TRUE) == CSTR_EQUAL;
}

std::optional<std::filesystem::path> RegisteredBinaryPath(
    ::vr::IVRApplications& applications,
    std::string* error) {
    std::array<char, 32768> buffer{};
    ::vr::EVRApplicationError applicationError = ::vr::VRApplicationError_None;
    applications.GetApplicationPropertyString(
        kApplicationKey.data(),
        ::vr::VRApplicationProperty_BinaryPath_String,
        buffer.data(),
        static_cast<std::uint32_t>(buffer.size()),
        &applicationError);
    if (applicationError != ::vr::VRApplicationError_None) {
        if (error != nullptr) {
            *error = applications.GetApplicationsErrorNameFromEnum(applicationError);
        }
        return std::nullopt;
    }
    const int wideLength = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, buffer.data(), -1, nullptr, 0);
    if (wideLength <= 1) {
        if (error != nullptr) *error = "registered binary path is not valid UTF-8";
        return std::nullopt;
    }
    std::wstring widePath(static_cast<std::size_t>(wideLength), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, buffer.data(), -1, widePath.data(), wideLength) != wideLength) {
        if (error != nullptr) *error = "registered binary path conversion failed";
        return std::nullopt;
    }
    widePath.resize(static_cast<std::size_t>(wideLength - 1));
    if (error != nullptr) error->clear();
    return std::filesystem::path(widePath);
}

void ApplySmoothScrollToCurrentWindow(float deltaPixels) noexcept {
    if (std::abs(deltaPixels) <= 1e-4F) return;
    const float nextY = std::clamp(
        ImGui::GetScrollY() + deltaPixels,
        0.0F,
        ImGui::GetScrollMaxY());
    ImGui::SetScrollY(nextY);
}

}  // namespace

struct ApplicationHost::Impl {
    SystemTray systemTray;
    config::Settings settings{};
    diagnostics::Logger logger{256};

    std::unique_ptr<vr::OpenVRRuntime> runtime;
    std::unique_ptr<vr::OverlayManager> overlays;
    std::unique_ptr<vr::SteamVRInput> steamInput;
    std::unique_ptr<vr::PointerInteractor> pointers;
    std::unique_ptr<vr::OverlayDragger> dragger;

    ComPtr<ID3D11Device> d3dDevice;
    ComPtr<ID3D11DeviceContext> d3dContext;
    ui::OverlayRenderer worldRenderer;
    ui::OverlayRenderer dashboardRenderer;
    ui::ThemePalette activePalette{DefaultPalette()};

    ui::SmoothScrollController worldStickScroll{
        ui::SmoothScrollConfig{0.18F, 1.0F, 13.0F, 9.0F}};
    ui::SmoothScrollController dashboardStickScroll{
        ui::SmoothScrollConfig{0.18F, 1.0F, 13.0F, 9.0F}};

    bool steamVrDashboardWasVisible{};
    bool worldHiddenForSteamVrDashboard{};
    std::optional<vr::Hand> primaryPointerHand;

    std::filesystem::path settingsPath;
    std::unique_ptr<config::SettingsSession> settingsSession;
    std::filesystem::path actionsPath;
    std::filesystem::path applicationManifestPath;
    std::string lastError;
    bool settingsDirty{};
    bool distanceAdjustmentActive{};
    bool distanceAdjustmentChanged{};
    float distanceAdjustmentElapsedSeconds{};
    std::optional<vr::RigidTransform> hiddenWorldPoseRelativeToHmd;
    bool recenterWorldOnNextShow{true};
    bool initialized{};
    bool running{};
    Clock::time_point previousFrame{Clock::now()};

    bool Fail(std::string_view context, std::string_view detail, std::string* output = nullptr) {
        std::string message(context);
        if (!detail.empty()) {
            message += ": ";
            message += detail;
        }
        lastError = message;
        logger.error(message);
        if (output != nullptr) *output = message;
        return false;
    }

    void NoteError(std::string_view context, std::string_view detail) {
        (void)Fail(context, detail, nullptr);
    }

    bool CreateGraphicsDevice(std::string* error) {
        std::string adapterError;
        const auto adapterIndex = runtime->graphicsAdapterIndex(&adapterError);
        if (!adapterIndex) {
            return Fail("SteamVR graphics adapter lookup failed", adapterError, error);
        }

        ComPtr<IDXGIFactory1> factory;
        HRESULT result = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
        if (FAILED(result)) {
            return Fail("CreateDXGIFactory1 failed", std::to_string(result), error);
        }
        ComPtr<IDXGIAdapter1> adapter;
        result = factory->EnumAdapters1(static_cast<UINT>(*adapterIndex), &adapter);
        if (FAILED(result)) {
            return Fail("SteamVR DXGI adapter is unavailable", std::to_string(result), error);
        }

        constexpr std::array featureLevels{
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
        };
        D3D_FEATURE_LEVEL selected{};
        result = D3D11CreateDevice(
            adapter.Get(),
            D3D_DRIVER_TYPE_UNKNOWN,
            nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            featureLevels.data(),
            static_cast<UINT>(featureLevels.size()),
            D3D11_SDK_VERSION,
            &d3dDevice,
            &selected,
            &d3dContext);
        if (FAILED(result)) {
            return Fail("D3D11CreateDevice failed", std::to_string(result), error);
        }
        return true;
    }

    bool ConfigureSteamVrApplication(bool autoLaunch, std::string* error = nullptr) {
        ::vr::IVRApplications* applications = ::vr::VRApplications();
        if (applications == nullptr || !std::filesystem::exists(applicationManifestPath)) {
            return Fail("SteamVR application manifest is unavailable", PathAsUtf8(applicationManifestPath), error);
        }
        const std::string path = PathAsUtf8(std::filesystem::absolute(applicationManifestPath));
        if (applications->IsApplicationInstalled(kApplicationKey.data())) {
            std::string propertyError;
            const auto registeredBinary = RegisteredBinaryPath(*applications, &propertyError);
            if (registeredBinary && !SameWindowsPath(*registeredBinary, ExecutablePath())) {
                if (registeredBinary->is_absolute()) {
                    const std::filesystem::path oldManifest =
                        registeredBinary->parent_path() /
                        "resources" / "openvr" / "overlay_template.vrmanifest";
                    const std::string oldManifestUtf8 = PathAsUtf8(oldManifest.lexically_normal());
                    const ::vr::EVRApplicationError removeError =
                        applications->RemoveApplicationManifest(oldManifestUtf8.c_str());
                    if (removeError != ::vr::VRApplicationError_None &&
                        removeError != ::vr::VRApplicationError_NoManifest) {
                        return Fail(
                            "Remove stale SteamVR application manifest",
                            applications->GetApplicationsErrorNameFromEnum(removeError),
                            error);
                    }
                    logger.info("Removed stale SteamVR manifest: " + oldManifestUtf8);
                }
            }
        }

        ::vr::EVRApplicationError appError = applications->AddApplicationManifest(path.c_str(), false);
        if (appError != ::vr::VRApplicationError_None &&
            appError != ::vr::VRApplicationError_AppKeyAlreadyExists) {
            return Fail("AddApplicationManifest failed", applications->GetApplicationsErrorNameFromEnum(appError), error);
        }
        if (!applications->IsApplicationInstalled(kApplicationKey.data())) {
            return Fail(
                "SteamVR application registration is pending",
                "restart SteamVR once, then launch again",
                error);
        }

        std::array<char, ::vr::k_unMaxApplicationKeyLength> processKey{};
        appError = applications->GetApplicationKeyByProcessId(
            GetCurrentProcessId(),
            processKey.data(),
            static_cast<std::uint32_t>(processKey.size()));
        const bool alreadyIdentified =
            appError == ::vr::VRApplicationError_None &&
            std::string_view(processKey.data()) == kApplicationKey;
        if (!alreadyIdentified) {
            appError = applications->IdentifyApplication(
                GetCurrentProcessId(), kApplicationKey.data());
            const bool alreadyRunning =
                appError == ::vr::VRApplicationError_ApplicationAlreadyRunning &&
                applications->GetApplicationProcessId(kApplicationKey.data()) == GetCurrentProcessId();
            if (appError != ::vr::VRApplicationError_None && !alreadyRunning) {
                return Fail(
                    "Identify SteamVR application",
                    applications->GetApplicationsErrorNameFromEnum(appError),
                    error);
            }
        }

        appError = applications->SetApplicationAutoLaunch(kApplicationKey.data(), autoLaunch);
        if (appError != ::vr::VRApplicationError_None) {
            return Fail("SetApplicationAutoLaunch failed", applications->GetApplicationsErrorNameFromEnum(appError), error);
        }
        return true;
    }

    vr::FrontPlacement FrontPlacementFromSettings() const noexcept {
        vr::FrontPlacement placement{};
        placement.forwardMeters = settings.overlay_distance_m;
        placement.verticalOffsetMeters = settings.overlay_height_offset_m;
        placement.overlayWidthMeters = settings.overlay_width_m;
        placement.maximumAbsolutePitchDegrees = 30.0F;
        placement.tiltDegrees = settings.overlay_tilt_degrees;
        return placement;
    }

    void ApplyLiveSettings(bool recenter = false) {
        if (!overlays || !steamInput) return;
        std::string error;
        if (pointers) {
            pointers->setVerticalGain(settings.pointer_vertical_gain);
        }
        steamInput->setSceneInputSuppressionEnabled(settings.block_vr_input_while_pointing);
        if (!overlays->setFrontPlacement(FrontPlacementFromSettings(), false, &error)) {
            NoteError("Apply front placement", error);
        }
        if (!overlays->setKeyboardWidth(settings.overlay_width_m, &error)) {
            NoteError("Apply overlay width", error);
        }
        if (!overlays->setKeyboardOpacity(settings.overlay_opacity, &error)) {
            NoteError("Apply world overlay opacity", error);
        }
        if (!overlays->setDashboardOpacity(settings.overlay_opacity, &error)) {
            NoteError("Apply dashboard opacity", error);
        }
        worldRenderer.SetTheme(activePalette);
        dashboardRenderer.SetTheme(activePalette);
        if (recenter && !overlays->placeKeyboardInFrontOfHmd(&error)) {
            NoteError("Recenter world overlay", error);
        }
    }

    void ResetOverlayPosition() {
        hiddenWorldPoseRelativeToHmd.reset();
        recenterWorldOnNextShow = !overlays->isKeyboardVisible();
        std::string error;
        if (overlays->isKeyboardVisible() &&
            !overlays->placeKeyboardInFrontOfHmd(&error)) {
            NoteError("Reset overlay position", error);
        }
    }

    bool SaveSettings() {
        if (!settingsSession) return false;
        std::string error;
        if (!settingsSession->save(settings, &error)) {
            NoteError("Save settings", error);
            return false;
        }
        settingsDirty = false;
        return true;
    }

    vr::HandMask CapturedHands() const noexcept {
        vr::HandMask mask{};
        if (dragger && dragger->isDragging()) {
            if (dragger->capturedHand() == vr::Hand::Left) mask.left = true;
            if (dragger->capturedHand() == vr::Hand::Right) mask.right = true;
        }
        return mask;
    }

    std::optional<vr::Hand> PreferredInteractionHand() const noexcept {
        if (dragger && dragger->isDragging()) return dragger->capturedHand();
        const bool rightHit = pointers && pointers->right().intersects;
        const bool leftHit = pointers && pointers->left().intersects;
        if (primaryPointerHand == vr::Hand::Left && leftHit) return vr::Hand::Left;
        if (primaryPointerHand == vr::Hand::Right && rightHit) return vr::Hand::Right;
        if (rightHit) return vr::Hand::Right;
        if (leftHit) return vr::Hand::Left;
        return std::nullopt;
    }

    void FinishDrag() {
        if (!dragger || !dragger->isDragging()) return;
        dragger->end();
    }

    void FinishDistanceAdjustment() {
        distanceAdjustmentElapsedSeconds = 0.0F;
        if (!distanceAdjustmentActive) return;
        distanceAdjustmentActive = false;
        if (distanceAdjustmentChanged) {
            distanceAdjustmentChanged = false;
            SaveSettings();
        }
    }

    void HandleDistanceAdjustment(float deltaSeconds) {
        if (!dragger->isDragging() || !overlays->isKeyboardVisible()) {
            FinishDistanceAdjustment();
            return;
        }

        const vr::AnalogActionState& action =
            steamInput->actionsFor(vr::Hand::Right).adjustDistance;
        const float shapedAxis = vr::distancemotion::shapeAxis(action.y);
        if (!action.active || shapedAxis == 0.0F) {
            FinishDistanceAdjustment();
            return;
        }

        const std::optional<vr::Hand> capturedHand = dragger->capturedHand();
        if (!capturedHand.has_value()) {
            FinishDistanceAdjustment();
            return;
        }
        const vr::PointerState& capturedPointer = pointers->state(*capturedHand);
        if (!capturedPointer.tracked) {
            FinishDistanceAdjustment();
            return;
        }
        if (!distanceAdjustmentActive) {
            distanceAdjustmentElapsedSeconds = 0.0F;
        }
        distanceAdjustmentActive = true;
        std::string error;
        const vr::RigidTransform current = dragger->currentTransform();
        const float acceleration = vr::distancemotion::startupAccelerationMultiplier(
            distanceAdjustmentElapsedSeconds);
        const vr::TrackedPose hmdPose = runtime->hmdPose();
        const vr::distancemotion::DistanceAdjustment adjustment =
            vr::distancemotion::adjust(
                current,
                hmdPose,
                action.y,
                deltaSeconds,
                vr::distancemotion::kDefaultMinimumDistanceMeters,
                vr::distancemotion::kDefaultMaximumDistanceMeters,
                kOverlayDistanceSpeedMetersPerSecond * acceleration);
        if (hmdPose.valid && std::isfinite(deltaSeconds) && deltaSeconds > 0.0F) {
            distanceAdjustmentElapsedSeconds += deltaSeconds;
        }
        if (!adjustment.changed) return;
        if (!dragger->adoptTransform(adjustment.transform, capturedPointer, &error)) {
            NoteError("Adjust overlay distance", error);
            return;
        }

        settings.overlay_distance_m = adjustment.distanceMeters;
        settingsDirty = true;
        distanceAdjustmentChanged = true;
    }

    void HandleHand(vr::Hand hand, float deltaSeconds) {
        const vr::PointerState& pointer = pointers->state(hand);
        const vr::HandActionState action = steamInput->actionsFor(hand);

        if (dragger->isDragging()) {
            if (dragger->capturedHand() == hand) {
                std::string error;
                if (action.grab.held && pointer.tracked &&
                    !dragger->update(pointer, runtime->hmdPose(), deltaSeconds, &error)) {
                    NoteError("Move world overlay", error);
                }
                if (!action.grab.held) {
                    FinishDistanceAdjustment();
                    FinishDrag();
                }
            }
            return;
        }

        if (pointer.intersects && action.primary.pressed) {
            primaryPointerHand = hand;
        }

        if (pointer.intersects && action.grab.pressed) {
            std::string error;
            if (!dragger->begin(pointer, &error)) {
                NoteError("Begin overlay drag", error);
            }
        }
    }

    void ToggleWorldOverlay() {
        const bool wasVisible = overlays->isKeyboardVisible();
        std::string error;
        if (wasVisible) {
            FinishDistanceAdjustment();
            if (dragger->isDragging()) FinishDrag();
            hiddenWorldPoseRelativeToHmd.reset();
            if (const auto current = overlays->keyboardTransform(&error)) {
                if (const auto relative = vr::relativepose::capture(*current, runtime->hmdPose())) {
                    hiddenWorldPoseRelativeToHmd = *relative;
                }
            } else {
                NoteError("Capture overlay pose before hiding", error);
            }
            if (!overlays->setKeyboardVisible(false, false, &error)) {
                NoteError("Hide world overlay", error);
            }
            return;
        }

        if (recenterWorldOnNextShow) {
            if (!overlays->placeKeyboardInFrontOfHmd(&error)) {
                NoteError("Place overlay in front of HMD", error);
                return;
            }
        } else if (hiddenWorldPoseRelativeToHmd) {
            const auto restored = vr::relativepose::restore(
                *hiddenWorldPoseRelativeToHmd, runtime->hmdPose());
            if (!restored) {
                NoteError("Restore overlay pose", "HMD pose is unavailable");
                return;
            }
            if (!overlays->setKeyboardTransform(*restored, &error)) {
                NoteError("Restore overlay pose", error);
                return;
            }
        }
        if (!overlays->setKeyboardVisible(true, false, &error)) {
            NoteError("Show world overlay", error);
            return;
        }
        recenterWorldOnNextShow = false;
        hiddenWorldPoseRelativeToHmd.reset();
    }

    void SyncWorldOverlayWithSteamVrDashboard() {
        const bool dashboardVisible = overlays->isDashboardVisible();
        std::string error;

        if (dashboardVisible && !steamVrDashboardWasVisible &&
            !worldHiddenForSteamVrDashboard) {
            worldHiddenForSteamVrDashboard = overlays->isKeyboardVisible();
            if (worldHiddenForSteamVrDashboard) {
                FinishDistanceAdjustment();
                if (dragger->isDragging()) FinishDrag();
            }
        }

        if (dashboardVisible && worldHiddenForSteamVrDashboard) {
            if (overlays->isKeyboardVisible() &&
                !overlays->setKeyboardVisible(false, false, &error)) {
                NoteError("Hide world overlay while SteamVR Dashboard is open", error);
            }
        } else if (!dashboardVisible && worldHiddenForSteamVrDashboard) {
            if (overlays->isKeyboardVisible() ||
                overlays->setKeyboardVisible(true, false, &error)) {
                worldHiddenForSteamVrDashboard = false;
            } else {
                NoteError("Restore world overlay after SteamVR Dashboard closed", error);
            }
        }

        steamVrDashboardWasVisible = dashboardVisible;
    }

    void HandleVrInput(float deltaSeconds) {
        std::string error;
        if (!pointers->update(true, &error)) {
            NoteError("Update VR pointers", error);
            return;
        }
        const vr::HandMask actionHands = pointers->keyboardActionHands(CapturedHands());
        const vr::HandMask observedHands{
            pointers->left().tracked,
            pointers->right().tracked,
        };
        const std::optional<vr::Hand> suppressionHand = PreferredInteractionHand();
        if (!steamInput->update(actionHands, observedHands, suppressionHand, &error)) {
            NoteError("Update SteamVR input", error);
            return;
        }
        if (overlays->isDashboardVisible()) {
            worldStickScroll.Reset();
            return;
        }
        if (steamInput->actions().toggleOverlay.pressed) {
            ToggleWorldOverlay();
            return;
        }
        HandleHand(vr::Hand::Left, deltaSeconds);
        HandleHand(vr::Hand::Right, deltaSeconds);
        HandleDistanceAdjustment(deltaSeconds);
    }

    // Replace the body of this method with your world-space overlay UI.
    void RenderWorldOverlayUi(float smoothScrollPixels) {
        ImGui::SetNextWindowPos(ImVec2(0.0F, 0.0F));
        ImGui::SetNextWindowSize(ImVec2(
            static_cast<float>(worldRenderer.width()),
            static_cast<float>(worldRenderer.height())));
        constexpr ImGuiWindowFlags kFlags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoSavedSettings;
        if (ImGui::Begin("##WorldOverlayCanvas", nullptr, kFlags)) {
            ApplySmoothScrollToCurrentWindow(smoothScrollPixels);
            // Template placeholder: build your world overlay UI here.
        }
        ImGui::End();
    }

    // Replace the body of this method with your SteamVR Dashboard UI.
    void RenderDashboardUi(float smoothScrollPixels) {
        ImGui::SetNextWindowPos(ImVec2(0.0F, 0.0F));
        ImGui::SetNextWindowSize(ImVec2(
            static_cast<float>(dashboardRenderer.width()),
            static_cast<float>(dashboardRenderer.height())));
        constexpr ImGuiWindowFlags kFlags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoSavedSettings;
        if (ImGui::Begin("##DashboardCanvas", nullptr, kFlags)) {
            ApplySmoothScrollToCurrentWindow(smoothScrollPixels);
            // Template placeholder: build your dashboard UI here.
        }
        ImGui::End();
    }

    void RenderWorldOverlay(float deltaSeconds) {
        ui::PointerFrame pointer{};
        if (const auto hand = PreferredInteractionHand()) {
            const vr::PointerState& state = pointers->state(*hand);
            const vr::HandActionState action = steamInput->actionsFor(*hand);
            const vr::Vec2 pixels = vr::PointerInteractor::toTexturePixels(
                state,
                static_cast<float>(worldRenderer.width()),
                static_cast<float>(worldRenderer.height()),
                false,
                false);
            pointer.visible = state.intersects;
            pointer.xPixels = pixels.x;
            pointer.yPixels = pixels.y;
            pointer.primaryHeld = action.primary.held;
            pointer.primaryPressed = action.primary.pressed;
            pointer.primaryReleased = action.primary.released;
        }

        const vr::AnalogActionState& stick =
            steamInput->actionsFor(vr::Hand::Right).adjustDistance;
        const float smoothScrollPixels = (!dragger->isDragging() && overlays->isKeyboardVisible())
            ? worldStickScroll.Update(stick.active ? -stick.y : 0.0F, deltaSeconds) *
                kScrollPixelsPerNormalizedUnit
            : 0.0F;

        worldRenderer.BeginFrame(deltaSeconds, pointer);
        RenderWorldOverlayUi(smoothScrollPixels);

        std::array<ui::VrPointerVisual, 2> visuals{};
        for (std::size_t i = 0; i < 2; ++i) {
            const vr::Hand hand = i == 0 ? vr::Hand::Left : vr::Hand::Right;
            const vr::PointerState& state = pointers->state(hand);
            const vr::HandActionState action = steamInput->actionsFor(hand);
            const vr::Vec2 pixels = vr::PointerInteractor::toTexturePixels(
                state,
                static_cast<float>(worldRenderer.width()),
                static_cast<float>(worldRenderer.height()),
                false,
                false);
            visuals[i].visible = state.intersects;
            visuals[i].pressed = action.primary.held || action.grab.held;
            visuals[i].offSurface = !state.intersects;
            visuals[i].xPixels = pixels.x;
            visuals[i].yPixels = pixels.y;
        }
        worldRenderer.DrawVrPointers(visuals);
        worldRenderer.EndFrame(ui::Color{0.0F, 0.0F, 0.0F, 0.0F});
        d3dContext->Flush();

        std::string error;
        if (!overlays->submitKeyboardTexture(
                worldRenderer.texture(), vr::TextureColorSpace::Gamma, &error)) {
            NoteError("Submit world overlay texture", error);
        }
    }

    void ProcessDashboard(float deltaSeconds, const vr::DashboardInputFrame& input) {
        ui::PointerFrame pointer{};
        pointer.visible = input.pointerValid && input.active;
        pointer.xPixels = input.pixelX;
        pointer.yPixels = input.pixelY;
        pointer.primaryHeld = input.primaryHeld;
        pointer.primaryPressed = input.primaryPressed;
        pointer.primaryReleased = input.primaryReleased;
        pointer.wheelX = input.scrollDeltaX;
        pointer.wheelY = input.scrollDeltaY;

        dashboardRenderer.BeginFrame(deltaSeconds, pointer);
        const vr::AnalogActionState& stick =
            steamInput->actionsFor(vr::Hand::Right).adjustDistance;
        const float smoothScrollPixels = dashboardStickScroll.Update(
            stick.active ? -stick.y : 0.0F,
            deltaSeconds) * kScrollPixelsPerNormalizedUnit;
        RenderDashboardUi(smoothScrollPixels);
        dashboardRenderer.EndFrame(ui::Color{0.0F, 0.0F, 0.0F, 0.0F});
        d3dContext->Flush();

        std::string error;
        if (!overlays->submitDashboardTexture(
                dashboardRenderer.texture(), vr::TextureColorSpace::Gamma, &error)) {
            NoteError("Submit Dashboard overlay texture", error);
        }
    }

    bool Initialize(std::string* error) {
        settingsPath = LocalSettingsPath();
        actionsPath = ResourcePath("actions.json");
        applicationManifestPath = ResourcePath("overlay_template.vrmanifest");

        std::string detail;
        settingsSession = std::make_unique<config::SettingsSession>(PathAsUtf8(settingsPath));
        if (!settingsSession->load(settings, &detail)) {
            logger.warning("Invalid settings were ignored: " + detail);
            settings = config::Settings::defaults();
        }

        runtime = std::make_unique<vr::OpenVRRuntime>();
        if (!runtime->initialize(&detail)) {
            return Fail("OpenVR initialization failed", detail, error);
        }
        if (!ConfigureSteamVrApplication(settings.autostart_steamvr, &detail)) {
            if (error != nullptr) *error = lastError;
            return false;
        }
        steamInput = std::make_unique<vr::SteamVRInput>(*runtime);
        if (!steamInput->initialize(actionsPath, &detail)) {
            return Fail("SteamVR Input initialization failed", detail, error);
        }

        overlays = std::make_unique<vr::OverlayManager>(*runtime);
        vr::OverlayConfiguration overlayConfiguration{};
        overlayConfiguration.keyboardPixelWidth = kWorldOverlayWidth;
        overlayConfiguration.keyboardPixelHeight = kWorldOverlayHeight;
        overlayConfiguration.dashboardPixelWidth = kDashboardWidth;
        overlayConfiguration.dashboardPixelHeight = kDashboardHeight;
        overlayConfiguration.frontPlacement = FrontPlacementFromSettings();
        if (!overlays->initialize(overlayConfiguration, &detail)) {
            return Fail("OpenVR overlay creation failed", detail, error);
        }
        if (!CreateGraphicsDevice(error)) return false;

        if (!worldRenderer.Initialize(
                d3dDevice.Get(), d3dContext.Get(), kWorldOverlayWidth, kWorldOverlayHeight,
                kWorldFontPixels, false, activePalette, &detail)) {
            return Fail("World overlay renderer initialization failed", detail, error);
        }
        if (!dashboardRenderer.Initialize(
                d3dDevice.Get(), d3dContext.Get(), kDashboardWidth, kDashboardHeight,
                kDashboardFontPixels, false, activePalette, &detail)) {
            return Fail("Dashboard renderer initialization failed", detail, error);
        }

        pointers = std::make_unique<vr::PointerInteractor>(*runtime, *overlays);
        dragger = std::make_unique<vr::OverlayDragger>(*overlays);
        ApplyLiveSettings(false);
        recenterWorldOnNextShow = true;

        RenderWorldOverlay(1.0F / 90.0F);
        ProcessDashboard(1.0F / 90.0F, {});

        if (!systemTray.Initialize(&detail)) {
            return Fail("System tray initialization failed", detail, error);
        }
        initialized = true;
        logger.info("OpenVR Overlay Template initialized");
        if (error != nullptr) error->clear();
        return true;
    }

    int Run() {
        if (!initialized) return EXIT_FAILURE;
        running = true;
        previousFrame = Clock::now();
        while (running && !runtime->quitRequested()) {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                if (message.message == WM_QUIT) running = false;
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }

            const auto trayAction = systemTray.TakeAction();
            if (!running || trayAction == SystemTray::Action::Exit) break;
            if (trayAction == SystemTray::Action::ResetOverlayPosition) {
                ResetOverlayPosition();
            }

            const Clock::time_point now = Clock::now();
            const float deltaSeconds = std::chrono::duration<float>(now - previousFrame).count();
            previousFrame = now;
            std::string error;
            if (!runtime->pollEvents(&error)) {
                NoteError("Poll OpenVR events", error);
            }

            SyncWorldOverlayWithSteamVrDashboard();
            HandleVrInput(deltaSeconds);

            const auto dashboardInput = overlays->pollDashboardInput(&error);
            if (!dashboardInput) {
                NoteError("Poll Dashboard input", error);
            } else {
                if (dashboardInput->quitRequested) running = false;
                if (dashboardInput->active) {
                    ProcessDashboard(deltaSeconds, *dashboardInput);
                } else {
                    dashboardStickScroll.Reset();
                }
            }
            if (overlays->isKeyboardVisible()) {
                RenderWorldOverlay(deltaSeconds);
            }
            if (!runtime->waitFrameSync(20U, &error)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(4));
            }
        }
        return EXIT_SUCCESS;
    }

    void Shutdown() noexcept {
        systemTray.Shutdown();
        if (settingsDirty) SaveSettings();
        if (overlays) overlays->clearTextures();
        worldRenderer.Shutdown();
        dashboardRenderer.Shutdown();
        dragger.reset();
        pointers.reset();
        if (overlays) overlays->shutdown();
        overlays.reset();
        if (steamInput) steamInput->shutdown();
        steamInput.reset();
        if (runtime) runtime->shutdown();
        runtime.reset();
        d3dContext.Reset();
        d3dDevice.Reset();
        initialized = false;
        running = false;
    }
};

ApplicationHost::ApplicationHost() : impl_(std::make_unique<Impl>()) {}

ApplicationHost::~ApplicationHost() {
    Shutdown();
}

bool ApplicationHost::Initialize(std::string* error) {
    return impl_->Initialize(error);
}

int ApplicationHost::Run() {
    return impl_->Run();
}

void ApplicationHost::Shutdown() noexcept {
    if (impl_) impl_->Shutdown();
}

}  // namespace flickvr::app
