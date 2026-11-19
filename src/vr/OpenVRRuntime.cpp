#include "OpenVRRuntime.h"

#include "ControllerAim.h"
#include "OpenVRConversion.h"
#include "PosePrediction.h"

#include <openvr.h>

#include <array>

namespace flickvr::vr {

namespace {

constexpr std::uint32_t kInvalidDeviceIndex = 0xFFFFFFFFU;

struct AimTransformCache {
    std::uint32_t deviceIndex{kInvalidDeviceIndex};
    std::string renderModelName{};
    ::vr::VRInputValueHandle_t devicePath{
        ::vr::k_ulInvalidInputValueHandle};
    std::optional<RigidTransform> controllerToAim{};
    ControllerAimSource source{ControllerAimSource::RawPose};
};

void writeError(const std::string& message, std::string* output) {
    if (output != nullptr) {
        *output = message;
    }
}

TrackedPose convertPose(
    const std::array<::vr::TrackedDevicePose_t, ::vr::k_unMaxTrackedDeviceCount>& poses,
    ::vr::TrackedDeviceIndex_t index) noexcept {
    if (index == ::vr::k_unTrackedDeviceIndexInvalid ||
        index >= poses.size()) {
        return {};
    }

    const ::vr::TrackedDevicePose_t& source = poses[index];
    TrackedPose result{};
    result.connected = source.bDeviceIsConnected;
    result.valid = source.bPoseIsValid;
    result.deviceIndex = index;
    if (result.valid) {
        result.transform = detail::fromOpenVR(source.mDeviceToAbsoluteTracking);
    }
    return result;
}

std::optional<std::string> renderModelName(
    ::vr::IVRSystem* system,
    ::vr::TrackedDeviceIndex_t deviceIndex) {
    if (system == nullptr || deviceIndex == ::vr::k_unTrackedDeviceIndexInvalid) {
        return std::nullopt;
    }

    ::vr::ETrackedPropertyError propertyError = ::vr::TrackedProp_Success;
    const std::uint32_t requiredBytes = system->GetStringTrackedDeviceProperty(
        deviceIndex,
        ::vr::Prop_RenderModelName_String,
        nullptr,
        0,
        &propertyError);
    if (requiredBytes <= 1U ||
        (propertyError != ::vr::TrackedProp_Success &&
         propertyError != ::vr::TrackedProp_BufferTooSmall)) {
        return std::nullopt;
    }

    std::string name(requiredBytes, '\0');
    propertyError = ::vr::TrackedProp_Success;
    const std::uint32_t writtenBytes = system->GetStringTrackedDeviceProperty(
        deviceIndex,
        ::vr::Prop_RenderModelName_String,
        name.data(),
        requiredBytes,
        &propertyError);
    if (propertyError != ::vr::TrackedProp_Success || writtenBytes <= 1U ||
        writtenBytes > requiredBytes) {
        return std::nullopt;
    }
    name.resize(writtenBytes - 1U);
    return name;
}

std::optional<RigidTransform> resolveAimTransform(
    ::vr::IVRSystem* system,
    ::vr::IVRInput* input,
    ::vr::IVRRenderModels* renderModels,
    Hand hand,
    AimTransformCache& cache) {
    // Component transforms are fixed metadata for a tracked device. SteamVR
    // may take its shared property-container lock while resolving them, so do
    // not repeat that work at render-frame frequency. Device and property
    // events clear this cache in pollEvents().
    if (cache.controllerToAim.has_value()) {
        return cache.controllerToAim;
    }
    cache.source = ControllerAimSource::RawPose;
    if (system == nullptr || input == nullptr || renderModels == nullptr ||
        cache.deviceIndex == kInvalidDeviceIndex) {
        return std::nullopt;
    }

    if (cache.renderModelName.empty()) {
        const std::optional<std::string> modelName = renderModelName(
            system,
            static_cast<::vr::TrackedDeviceIndex_t>(cache.deviceIndex));
        if (!modelName.has_value()) {
            return std::nullopt;
        }
        cache.renderModelName = *modelName;
    }

    if (cache.devicePath == ::vr::k_ulInvalidInputValueHandle) {
        const char* sourcePath = hand == Hand::Left
            ? "/user/hand/left"
            : "/user/hand/right";
        if (input->GetInputSourceHandle(sourcePath, &cache.devicePath) !=
                ::vr::VRInputError_None ||
            cache.devicePath == ::vr::k_ulInvalidInputValueHandle) {
            cache.devicePath = ::vr::k_ulInvalidInputValueHandle;
            return std::nullopt;
        }
    }

    struct ComponentCandidate {
        const char* name;
        ControllerAimSource source;
    };
    constexpr std::array<ComponentCandidate, 2> candidates{{
        {::vr::k_pch_Controller_Component_Tip,
         ControllerAimSource::TipComponent},
        {::vr::k_pch_Controller_Component_OpenXR_Aim,
         ControllerAimSource::OpenXrAimComponent},
    }};

    const auto readComponent = [&](const char* componentName) {
        ::vr::RenderModel_ControllerMode_State_t mode{};
        ::vr::RenderModel_ComponentState_t component{};
        if (!renderModels->GetComponentStateForDevicePath(
                cache.renderModelName.c_str(),
                componentName,
                cache.devicePath,
                &mode,
                &component)) {
            return std::optional<RigidTransform>{};
        }
        return std::optional<RigidTransform>{detail::fromOpenVR(
            component.mTrackingToComponentLocal)};
    };

    for (const ComponentCandidate& candidate : candidates) {
        const std::optional<RigidTransform> component = readComponent(
            candidate.name);
        const std::optional<controlleraim::ComponentSelection> selected =
            candidate.source == ControllerAimSource::TipComponent
            ? controlleraim::selectComponent(component, std::nullopt)
            : controlleraim::selectComponent(std::nullopt, component);
        if (!selected.has_value()) {
            continue;
        }
        cache.source = selected->source;
        cache.controllerToAim = selected->controllerToAim;
        return cache.controllerToAim;
    }
    return std::nullopt;
}

} // namespace

struct OpenVRRuntime::Impl {
    ::vr::IVRSystem* system{};
    ::vr::IVROverlay* overlay{};
    ::vr::IVRInput* input{};
    ::vr::IVRCompositor* compositor{};
    ::vr::IVRRenderModels* renderModels{};
    TrackingOrigin origin{TrackingOrigin::Standing};
    TrackedPose hmd{};
    TrackedPose left{};
    TrackedPose right{};
    TrackedPose leftAim{};
    TrackedPose rightAim{};
    AimTransformCache leftAimCache{};
    AimTransformCache rightAimCache{};
    std::string lastError{};
    bool ownsRuntime{};
    bool actionManifestConfigured{};
    bool quitRequested{};
    std::optional<std::uint64_t> closedKeyboardOverlay;
    bool displayTimingCached{};
    float displayFrequency{};
    float vsyncToPhotons{};
};

OpenVRRuntime::OpenVRRuntime(TrackingOrigin origin)
    : impl_(std::make_unique<Impl>()) {
    impl_->origin = origin;
}

OpenVRRuntime::~OpenVRRuntime() {
    shutdown();
}

bool OpenVRRuntime::initialize(std::string* error) {
    if (!impl_) {
        impl_ = std::make_unique<Impl>();
    }
    if (impl_->ownsRuntime) {
        writeError({}, error);
        return true;
    }

    impl_->lastError.clear();
    ::vr::EVRInitError initError = ::vr::VRInitError_None;
    impl_->system = ::vr::VR_Init(&initError, ::vr::VRApplication_Overlay);
    if (initError != ::vr::VRInitError_None || impl_->system == nullptr) {
        const char* description =
            ::vr::VR_GetVRInitErrorAsEnglishDescription(initError);
        impl_->lastError = "OpenVR initialization failed";
        if (description != nullptr && description[0] != '\0') {
            impl_->lastError += ": ";
            impl_->lastError += description;
        }
        impl_->system = nullptr;
        writeError(impl_->lastError, error);
        return false;
    }

    impl_->ownsRuntime = true;
    impl_->overlay = ::vr::VROverlay();
    impl_->input = ::vr::VRInput();
    impl_->compositor = ::vr::VRCompositor();
    impl_->renderModels = ::vr::VRRenderModels();

    if (impl_->overlay == nullptr || impl_->input == nullptr) {
        impl_->lastError = impl_->overlay == nullptr
            ? "OpenVR overlay interface is unavailable"
            : "SteamVR Input interface is unavailable";
        writeError(impl_->lastError, error);
        shutdown();
        return false;
    }

    writeError({}, error);
    return true;
}

void OpenVRRuntime::shutdown() noexcept {
    if (!impl_) {
        return;
    }

    impl_->hmd = {};
    impl_->left = {};
    impl_->right = {};
    impl_->leftAim = {};
    impl_->rightAim = {};
    impl_->leftAimCache = {};
    impl_->rightAimCache = {};
    impl_->system = nullptr;
    impl_->overlay = nullptr;
    impl_->input = nullptr;
    impl_->compositor = nullptr;
    impl_->renderModels = nullptr;
    impl_->actionManifestConfigured = false;
    impl_->quitRequested = false;
    impl_->closedKeyboardOverlay.reset();

    if (impl_->ownsRuntime) {
        impl_->ownsRuntime = false;
        ::vr::VR_Shutdown();
    }
}

bool OpenVRRuntime::isInitialized() const noexcept {
    return impl_ && impl_->ownsRuntime && impl_->system != nullptr;
}

bool OpenVRRuntime::compositorAvailable() const noexcept {
    return impl_ && impl_->compositor != nullptr;
}

bool OpenVRRuntime::runtimeInstalled() noexcept {
    return ::vr::VR_IsRuntimeInstalled();
}

bool OpenVRRuntime::hmdPresent() noexcept {
    return ::vr::VR_IsHmdPresent();
}

std::optional<std::int32_t> OpenVRRuntime::graphicsAdapterIndex(
    std::string* error) {
    if (!isInitialized()) {
        if (impl_) {
            impl_->lastError = "OpenVR runtime is not initialized";
            writeError(impl_->lastError, error);
        }
        return std::nullopt;
    }

    std::int32_t adapterIndex = -1;
    impl_->system->GetDXGIOutputInfo(&adapterIndex);
    if (adapterIndex < 0) {
        impl_->lastError = "OpenVR did not provide a valid DXGI adapter index";
        writeError(impl_->lastError, error);
        return std::nullopt;
    }

    impl_->lastError.clear();
    writeError({}, error);
    return adapterIndex;
}

bool OpenVRRuntime::pollEvents(std::string* error) {
    if (!isInitialized()) {
        impl_->lastError = "OpenVR runtime is not initialized";
        writeError(impl_->lastError, error);
        return false;
    }
    if (!impl_->actionManifestConfigured) {
        impl_->lastError =
            "Set the SteamVR action manifest before polling OpenVR events";
        writeError(impl_->lastError, error);
        return false;
    }

    impl_->closedKeyboardOverlay.reset();
    ::vr::VREvent_t event{};
    while (impl_->system->PollNextEvent(&event, sizeof(event))) {
        switch (event.eventType) {
        case ::vr::VREvent_KeyboardClosed_Global:
            impl_->closedKeyboardOverlay = event.data.keyboard.overlayHandle;
            break;
        case ::vr::VREvent_Quit:
            impl_->quitRequested = true;
            impl_->system->AcknowledgeQuit_Exiting();
            break;
        case ::vr::VREvent_DriverRequestedQuit:
            impl_->quitRequested = true;
            break;
        case ::vr::VREvent_ProcessQuit:
            // This reports that some OpenVR client process exited; it is not
            // a request for this overlay to quit. In particular, VRChat and
            // streaming bridge shutdowns are broadcast here.
            break;
        case ::vr::VREvent_TrackedDeviceActivated:
        case ::vr::VREvent_TrackedDeviceDeactivated:
        case ::vr::VREvent_TrackedDeviceUpdated:
            if (event.trackedDeviceIndex == ::vr::k_unTrackedDeviceIndex_Hmd) {
                impl_->displayTimingCached = false;
            }
            if (impl_->leftAimCache.deviceIndex == event.trackedDeviceIndex) {
                impl_->leftAimCache = {};
            }
            if (impl_->rightAimCache.deviceIndex == event.trackedDeviceIndex) {
                impl_->rightAimCache = {};
            }
            break;
        case ::vr::VREvent_PropertyChanged:
            // Property events identify their source through a property
            // container, not reliably through trackedDeviceIndex. They are
            // infrequent, so invalidate both small metadata caches.
            impl_->leftAimCache = {};
            impl_->rightAimCache = {};
            impl_->displayTimingCached = false;
            break;
        default:
            break;
        }
    }

    impl_->lastError.clear();
    writeError({}, error);
    return true;
}

bool OpenVRRuntime::quitRequested() const noexcept {
    return impl_ && impl_->quitRequested;
}

bool OpenVRRuntime::keyboardClosedForOverlay(std::uint64_t overlay) const noexcept {
    return impl_ && impl_->closedKeyboardOverlay == overlay;
}

bool OpenVRRuntime::actionManifestConfigured() const noexcept {
    return impl_ && impl_->actionManifestConfigured;
}

bool OpenVRRuntime::waitFrameSync(
    std::uint32_t timeoutMilliseconds,
    std::string* error) {
    if (!isInitialized() || impl_->overlay == nullptr) {
        impl_->lastError = "OpenVR overlay interface is not initialized";
        writeError(impl_->lastError, error);
        return false;
    }

    const ::vr::EVROverlayError overlayError =
        impl_->overlay->WaitFrameSync(timeoutMilliseconds);
    if (overlayError != ::vr::VROverlayError_None) {
        impl_->lastError = "Wait for SteamVR overlay frame failed: ";
        impl_->lastError += impl_->overlay->GetOverlayErrorNameFromEnum(overlayError);
        writeError(impl_->lastError, error);
        return false;
    }

    impl_->lastError.clear();
    writeError({}, error);
    return true;
}

void OpenVRRuntime::setTrackingOrigin(TrackingOrigin origin) noexcept {
    if (!impl_) {
        return;
    }
    impl_->origin = origin;
    impl_->hmd = {};
    impl_->left = {};
    impl_->right = {};
    impl_->leftAim = {};
    impl_->rightAim = {};
    impl_->leftAimCache = {};
    impl_->rightAimCache = {};
}

TrackingOrigin OpenVRRuntime::trackingOrigin() const noexcept {
    return impl_ ? impl_->origin : TrackingOrigin::Standing;
}

bool OpenVRRuntime::updateTrackedPoses(
    float predictedSecondsFromNow,
    std::string* error) {
    if (!isInitialized()) {
        if (impl_) {
            impl_->lastError = "OpenVR runtime is not initialized";
            writeError(impl_->lastError, error);
        }
        return false;
    }

    std::array<::vr::TrackedDevicePose_t, ::vr::k_unMaxTrackedDeviceCount> poses{};
    impl_->system->GetDeviceToAbsoluteTrackingPose(
        detail::toOpenVROrigin(impl_->origin),
        predictedSecondsFromNow,
        poses.data(),
        static_cast<std::uint32_t>(poses.size()));

    impl_->hmd = convertPose(poses, ::vr::k_unTrackedDeviceIndex_Hmd);
    const ::vr::TrackedDeviceIndex_t leftIndex =
        impl_->system->GetTrackedDeviceIndexForControllerRole(
            ::vr::TrackedControllerRole_LeftHand);
    const ::vr::TrackedDeviceIndex_t rightIndex =
        impl_->system->GetTrackedDeviceIndexForControllerRole(
            ::vr::TrackedControllerRole_RightHand);
    impl_->left = convertPose(poses, leftIndex);
    impl_->right = convertPose(poses, rightIndex);

    // Keep invalid role indices explicit at the public boundary.
    if (leftIndex == ::vr::k_unTrackedDeviceIndexInvalid) {
        impl_->left.deviceIndex = kInvalidDeviceIndex;
    }
    if (rightIndex == ::vr::k_unTrackedDeviceIndexInvalid) {
        impl_->right.deviceIndex = kInvalidDeviceIndex;
    }

    const auto updateAim = [&](
        Hand hand,
        const TrackedPose& controller,
        AimTransformCache& cache,
        TrackedPose& aim) {
        if (cache.deviceIndex != controller.deviceIndex) {
            cache = {};
            cache.deviceIndex = controller.deviceIndex;
        }
        const std::optional<RigidTransform> controllerToAim =
            resolveAimTransform(
                impl_->system,
                impl_->input,
                impl_->renderModels,
                hand,
                cache);
        aim = controlleraim::compose(controller, controllerToAim);
    };
    updateAim(Hand::Left, impl_->left, impl_->leftAimCache, impl_->leftAim);
    updateAim(Hand::Right, impl_->right, impl_->rightAimCache, impl_->rightAim);

    impl_->lastError.clear();
    writeError({}, error);
    return true;
}

float OpenVRRuntime::predictionSecondsToPhotons() const noexcept {
    if (!isInitialized()) {
        return 0.0F;
    }

    float secondsSinceLastVsync = 0.0F;
    std::uint64_t frameCounter = 0U;
    if (!impl_->system->GetTimeSinceLastVsync(
            &secondsSinceLastVsync, &frameCounter)) {
        return 0.0F;
    }

    // These two HMD properties are stable between OpenVR property/device
    // events. Reading them every frame causes hundreds of thousands of shared
    // property-container lock acquisitions during an idle session.
    if (!impl_->displayTimingCached) {
        ::vr::ETrackedPropertyError frequencyError = ::vr::TrackedProp_Success;
        const float displayFrequency = impl_->system->GetFloatTrackedDeviceProperty(
            ::vr::k_unTrackedDeviceIndex_Hmd,
            ::vr::Prop_DisplayFrequency_Float,
            &frequencyError);
        ::vr::ETrackedPropertyError photonsError = ::vr::TrackedProp_Success;
        const float vsyncToPhotons = impl_->system->GetFloatTrackedDeviceProperty(
            ::vr::k_unTrackedDeviceIndex_Hmd,
            ::vr::Prop_SecondsFromVsyncToPhotons_Float,
            &photonsError);
        if (frequencyError != ::vr::TrackedProp_Success ||
            photonsError != ::vr::TrackedProp_Success) {
            return 0.0F;
        }
        impl_->displayFrequency = displayFrequency;
        impl_->vsyncToPhotons = vsyncToPhotons;
        impl_->displayTimingCached = true;
    }

    return poseprediction::secondsToPhotons(
        secondsSinceLastVsync,
        impl_->displayFrequency,
        impl_->vsyncToPhotons);
}

TrackedPose OpenVRRuntime::hmdPose() const noexcept {
    return impl_ ? impl_->hmd : TrackedPose{};
}

TrackedPose OpenVRRuntime::controllerPose(Hand hand) const noexcept {
    if (!impl_) {
        return {};
    }
    return hand == Hand::Left ? impl_->left : impl_->right;
}

TrackedPose OpenVRRuntime::controllerAimPose(Hand hand) const noexcept {
    if (!impl_) {
        return {};
    }
    return hand == Hand::Left ? impl_->leftAim : impl_->rightAim;
}

ControllerAimSource OpenVRRuntime::controllerAimSource(
    Hand hand) const noexcept {
    if (!impl_) {
        return ControllerAimSource::RawPose;
    }
    return hand == Hand::Left
        ? impl_->leftAimCache.source
        : impl_->rightAimCache.source;
}

const std::string& OpenVRRuntime::lastError() const noexcept {
    static const std::string empty{};
    return impl_ ? impl_->lastError : empty;
}

void* OpenVRRuntime::nativeSystem() const noexcept {
    return impl_ ? impl_->system : nullptr;
}

void* OpenVRRuntime::nativeOverlay() const noexcept {
    return impl_ ? impl_->overlay : nullptr;
}

void* OpenVRRuntime::nativeInput() const noexcept {
    return impl_ ? impl_->input : nullptr;
}

void OpenVRRuntime::markActionManifestConfigured() noexcept {
    if (impl_) {
        impl_->actionManifestConfigured = true;
    }
}

} // namespace flickvr::vr
