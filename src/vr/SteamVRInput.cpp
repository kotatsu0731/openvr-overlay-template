#include "SteamVRInput.h"

#include "OpenVRAccess.h"
#include "OpenVRConversion.h"
#include "OpenVRRuntime.h"
#include "SteamVRInputTransitions.h"

#include <openvr.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <system_error>
#include <utility>

namespace flickvr::vr {

namespace {

void writeError(const std::string& message, std::string* output) {
    if (output != nullptr) {
        *output = message;
    }
}

[[nodiscard]] std::string pathAsUtf8(const std::filesystem::path& path) {
#if defined(__cpp_lib_char8_t)
    const std::u8string encoded = path.u8string();
    return {
        reinterpret_cast<const char*>(encoded.data()),
        encoded.size(),
    };
#else
    return path.u8string();
#endif
}

[[nodiscard]] DigitalActionState convertAction(
    const ::vr::InputDigitalActionData_t& data) noexcept {
    DigitalActionState state{};
    state.active = data.bActive;
    state.held = data.bActive && data.bState;
    state.changed = data.bActive && data.bChanged;
    state.pressed = state.changed && state.held;
    state.released = state.changed && !state.held;
    state.updateTimeSeconds = data.fUpdateTime;
    state.activeOrigin = data.activeOrigin;
    return state;
}

void preserveReleaseTransition(
    const DigitalActionState& previous,
    DigitalActionState& current) noexcept {
    // Removing a hand-restricted action set (for example, after tracking is
    // lost) does not produce action data for that hand. Surface one final
    // release so a held flick or drag cannot remain captured indefinitely.
    if (previous.held && !current.held) {
        current.changed = true;
        current.released = true;
    }
}

void stabilizeHandActionTransition(
    const DigitalActionState& previous,
    bool wasObserved,
    DigitalActionState& current) noexcept {
    inputtransition::suppressSyntheticPress(
        previous,
        wasObserved,
        current);
    preserveReleaseTransition(previous, current);
}

} // namespace

struct SteamVRInput::Impl {
    explicit Impl(OpenVRRuntime& runtimeRef) : runtime(runtimeRef) {}

    OpenVRRuntime& runtime;
    SteamVRActionHandles handles{};
    FrameActionState frame{};
    HandMask activeHands{};
    HandMask observedHands{};
    std::optional<Hand> blockerHand{};
    inputtransition::PressEdgeLatch togglePressLatch{};
    std::filesystem::path manifest{};
    std::string lastError{};
    bool initialized{};
    bool suppressSceneInput{};

    [[nodiscard]] ::vr::IVRInput* input() const noexcept {
        return detail::OpenVRAccess::input(runtime);
    }

    [[nodiscard]] bool fail(
        const char* operation,
        ::vr::EVRInputError inputError,
        std::string* output) {
        lastError = operation;
        lastError += ": ";
        lastError += detail::inputErrorName(inputError);
        lastError += " (";
        lastError += std::to_string(static_cast<int>(inputError));
        lastError += ')';
        writeError(lastError, output);
        return false;
    }

    [[nodiscard]] bool getActionSet(
        const char* path,
        std::uint64_t& handle,
        std::string* output) {
        const ::vr::EVRInputError inputError = input()->GetActionSetHandle(
            path,
            reinterpret_cast<::vr::VRActionSetHandle_t*>(&handle));
        if (inputError != ::vr::VRInputError_None) {
            return fail(path, inputError, output);
        }
        return true;
    }

    [[nodiscard]] bool getAction(
        const char* path,
        std::uint64_t& handle,
        std::string* output) {
        const ::vr::EVRInputError inputError = input()->GetActionHandle(
            path,
            reinterpret_cast<::vr::VRActionHandle_t*>(&handle));
        if (inputError != ::vr::VRInputError_None) {
            return fail(path, inputError, output);
        }
        return true;
    }

    [[nodiscard]] bool getSource(
        const char* path,
        std::uint64_t& handle,
        std::string* output) {
        const ::vr::EVRInputError inputError = input()->GetInputSourceHandle(
            path,
            reinterpret_cast<::vr::VRInputValueHandle_t*>(&handle));
        if (inputError != ::vr::VRInputError_None) {
            return fail(path, inputError, output);
        }
        return true;
    }

    [[nodiscard]] bool readDigital(
        std::uint64_t action,
        std::uint64_t source,
        DigitalActionState& state,
        const char* operation,
        std::string* output) {
        ::vr::InputDigitalActionData_t data{};
        const ::vr::EVRInputError inputError = input()->GetDigitalActionData(
            action,
            &data,
            sizeof(data),
            source);
        if (inputError != ::vr::VRInputError_None) {
            state = {};
            return fail(operation, inputError, output);
        }
        state = convertAction(data);
        return true;
    }

    [[nodiscard]] bool readAnalog(
        std::uint64_t action,
        std::uint64_t source,
        AnalogActionState& state,
        const char* operation,
        std::string* output) {
        ::vr::InputAnalogActionData_t data{};
        const ::vr::EVRInputError inputError = input()->GetAnalogActionData(
            action,
            &data,
            sizeof(data),
            source);
        if (inputError != ::vr::VRInputError_None) {
            state = {};
            return fail(operation, inputError, output);
        }
        state = {};
        state.active = data.bActive;
        if (data.bActive) {
            state.x = data.x;
            state.y = data.y;
            state.deltaX = data.deltaX;
            state.deltaY = data.deltaY;
            state.updateTimeSeconds = data.fUpdateTime;
            state.activeOrigin = data.activeOrigin;
        }
        return true;
    }
};

SteamVRInput::SteamVRInput(OpenVRRuntime& runtime)
    : impl_(std::make_unique<Impl>(runtime)) {}

SteamVRInput::~SteamVRInput() = default;

bool SteamVRInput::initialize(
    const std::filesystem::path& actionManifestPath,
    std::string* error) {
    if (isInitialized()) {
        writeError({}, error);
        return true;
    }
    if (!impl_->runtime.isInitialized() || impl_->input() == nullptr) {
        impl_->lastError = "OpenVR runtime is not initialized";
        writeError(impl_->lastError, error);
        return false;
    }

    std::error_code pathError;
    std::filesystem::path absoluteManifest =
        std::filesystem::absolute(actionManifestPath, pathError);
    if (pathError ||
        !std::filesystem::is_regular_file(absoluteManifest, pathError)) {
        impl_->lastError = "SteamVR action manifest does not exist: ";
        impl_->lastError += actionManifestPath.string();
        writeError(impl_->lastError, error);
        return false;
    }
    absoluteManifest = absoluteManifest.lexically_normal();

    const std::string manifestUtf8 = pathAsUtf8(absoluteManifest);
    ::vr::EVRInputError inputError =
        impl_->input()->SetActionManifestPath(manifestUtf8.c_str());
    if (inputError != ::vr::VRInputError_None) {
        return impl_->fail("Set SteamVR action manifest", inputError, error);
    }
    impl_->runtime.markActionManifestConfigured();

    impl_->handles = {};
    if (!impl_->getActionSet(
            "/actions/global", impl_->handles.globalSet, error) ||
        !impl_->getActionSet(
            "/actions/overlay", impl_->handles.keyboardSet, error) ||
        !impl_->getActionSet(
            "/actions/blocker", impl_->handles.blockerSet, error) ||
        !impl_->getAction(
            "/actions/global/in/toggle_overlay",
            impl_->handles.toggleOverlay,
            error) ||
        !impl_->getAction(
            "/actions/overlay/in/primary",
            impl_->handles.primary,
            error) ||
        !impl_->getAction(
            "/actions/overlay/in/grab",
            impl_->handles.grab,
            error) ||
        !impl_->getAction(
            "/actions/overlay/in/adjust_distance",
            impl_->handles.adjustDistance,
            error) ||
        !impl_->getAction(
            "/actions/overlay/out/haptic",
            impl_->handles.haptic,
            error) ||
        !impl_->getSource(
            "/user/hand/left",
            impl_->handles.leftSource,
            error) ||
        !impl_->getSource(
            "/user/hand/right",
            impl_->handles.rightSource,
            error)) {
        impl_->handles = {};
        return false;
    }

    impl_->manifest = std::move(absoluteManifest);
    impl_->frame = {};
    impl_->activeHands = {};
    impl_->observedHands = {};
    impl_->blockerHand.reset();
    impl_->togglePressLatch.reset();
    impl_->initialized = true;
    impl_->lastError.clear();
    writeError({}, error);
    return true;
}

void SteamVRInput::shutdown() noexcept {
    if (!impl_) {
        return;
    }
    // OpenVR owns action handles and has no per-manifest unload call. Clearing
    // local state is enough; OpenVRRuntime performs the process-wide shutdown.
    impl_->handles = {};
    impl_->frame = {};
    impl_->activeHands = {};
    impl_->observedHands = {};
    impl_->blockerHand.reset();
    impl_->togglePressLatch.reset();
    impl_->manifest.clear();
    impl_->initialized = false;
}

bool SteamVRInput::isInitialized() const noexcept {
    return impl_ && impl_->initialized && impl_->input() != nullptr;
}

bool SteamVRInput::update(
    HandMask keyboardHands,
    std::string* error) {
    return update(keyboardHands, keyboardHands, std::nullopt, error);
}

bool SteamVRInput::update(
    HandMask keyboardHands,
    std::optional<Hand> preferredPointerHand,
    std::string* error) {
    return update(
        keyboardHands,
        keyboardHands,
        preferredPointerHand,
        error);
}

bool SteamVRInput::update(
    HandMask keyboardHands,
    HandMask observedHands,
    std::optional<Hand> preferredPointerHand,
    std::string* error) {
    if (!isInitialized()) {
        impl_->lastError = "SteamVRInput is not initialized";
        writeError(impl_->lastError, error);
        return false;
    }

    // Keyboard and blocker values are in OpenVR's overlay-global range and
    // below its reserved range. The toggle shares a physical joystick/trackpad
    // source with locomotion in the default bindings. Since priority operates
    // on that whole source, only promote global while the left hand is already
    // intentionally covered by the full-hand blocker.
    constexpr std::int32_t kKeyboardPriority =
        ::vr::k_nActionSetOverlayGlobalPriorityMin + 2;
    constexpr std::int32_t kBlockerPriority =
        ::vr::k_nActionSetOverlayGlobalPriorityMin + 1;

    std::optional<Hand> blockerHand;
    if (impl_->suppressSceneInput && keyboardHands.any()) {
        const auto isHeld = [&](Hand hand) {
            const HandActionState& state = hand == Hand::Left
                ? impl_->frame.left
                : impl_->frame.right;
            return state.primary.held || state.grab.held;
        };

        // Do not switch hands during a flick or drag even if an automatic
        // closest-pointer selection oscillates between controllers.
        if (impl_->blockerHand.has_value() &&
            keyboardHands.contains(*impl_->blockerHand) &&
            isHeld(*impl_->blockerHand)) {
            blockerHand = impl_->blockerHand;
        } else if (preferredPointerHand.has_value() &&
                   keyboardHands.contains(*preferredPointerHand)) {
            blockerHand = preferredPointerHand;
        } else if (keyboardHands.left != keyboardHands.right) {
            blockerHand = keyboardHands.left ? Hand::Left : Hand::Right;
        } else if (impl_->blockerHand.has_value() &&
                   keyboardHands.contains(*impl_->blockerHand)) {
            blockerHand = impl_->blockerHand;
        } else {
            // Backward-compatible update() has no explicit pointer selection;
            // use the application's default pointer hand deterministically.
            blockerHand = Hand::Right;
        }
    }

    std::array<::vr::VRActiveActionSet_t, 4> activeSets{};
    std::uint32_t setCount = 0U;

    ::vr::VRActiveActionSet_t& global = activeSets[setCount++];
    global.ulActionSet = impl_->handles.globalSet;
    global.ulRestrictedToDevice = ::vr::k_ulInvalidInputValueHandle;
    global.ulSecondaryActionSet = ::vr::k_ulInvalidActionSetHandle;
    global.nPriority = blockerHand == Hand::Left
        ? ::vr::k_nActionSetOverlayGlobalPriorityMin + 3
        : 0;

    // An interactive hand receives overlay priority. A merely observed hand
    // stays at priority zero, so reading its physical edge cannot suppress the
    // scene application's input while its pointer is away from the keyboard.
    const auto appendKeyboardSet = [&](
        std::uint64_t source,
        bool interactive) {
        ::vr::VRActiveActionSet_t& keyboard = activeSets[setCount++];
        keyboard.ulActionSet = impl_->handles.keyboardSet;
        keyboard.ulRestrictedToDevice = source;
        keyboard.ulSecondaryActionSet = ::vr::k_ulInvalidActionSetHandle;
        keyboard.nPriority = interactive ? kKeyboardPriority : 0;
    };
    observedHands.left = observedHands.left || keyboardHands.left;
    observedHands.right = observedHands.right || keyboardHands.right;
    if (observedHands.left) {
        appendKeyboardSet(
            impl_->handles.leftSource,
            keyboardHands.left);
    }
    if (observedHands.right) {
        appendKeyboardSet(
            impl_->handles.rightSource,
            keyboardHands.right);
    }

    if (blockerHand.has_value()) {
        ::vr::VRActiveActionSet_t& blocker = activeSets[setCount++];
        blocker.ulActionSet = impl_->handles.blockerSet;
        blocker.ulRestrictedToDevice = *blockerHand == Hand::Left
            ? impl_->handles.leftSource
            : impl_->handles.rightSource;
        blocker.ulSecondaryActionSet = ::vr::k_ulInvalidActionSetHandle;
        blocker.nPriority = kBlockerPriority;
    }

    const ::vr::EVRInputError updateError = impl_->input()->UpdateActionState(
        activeSets.data(),
        sizeof(::vr::VRActiveActionSet_t),
        setCount);
    if (updateError != ::vr::VRInputError_None) {
        impl_->frame = {};
        impl_->activeHands = {};
        impl_->observedHands = {};
        impl_->blockerHand.reset();
        return impl_->fail("Update SteamVR action state", updateError, error);
    }

    FrameActionState frame{};
    if (!impl_->readDigital(
            impl_->handles.toggleOverlay,
            ::vr::k_ulInvalidInputValueHandle,
            frame.toggleOverlay,
            "Read toggle_overlay action",
            error)) {
        return false;
    }

    if (observedHands.left) {
        if (!impl_->readDigital(
                impl_->handles.primary,
                impl_->handles.leftSource,
                frame.left.primary,
                "Read left primary action",
                error) ||
            !impl_->readDigital(
                impl_->handles.grab,
                impl_->handles.leftSource,
                frame.left.grab,
                "Read left grab action",
                error) ||
            !impl_->readAnalog(
                impl_->handles.adjustDistance,
                impl_->handles.leftSource,
                frame.left.adjustDistance,
                "Read left adjust_distance action",
                error)) {
            return false;
        }
    }
    if (observedHands.right) {
        if (!impl_->readDigital(
                impl_->handles.primary,
                impl_->handles.rightSource,
                frame.right.primary,
                "Read right primary action",
                error) ||
            !impl_->readDigital(
                impl_->handles.grab,
                impl_->handles.rightSource,
                frame.right.grab,
                "Read right grab action",
                error) ||
            !impl_->readAnalog(
                impl_->handles.adjustDistance,
                impl_->handles.rightSource,
                frame.right.adjustDistance,
                "Read right adjust_distance action",
                error)) {
            return false;
        }
    }

    // Pointing at the keyboard can promote the global action set together
    // with the left-hand blocker. Hiding it drops that priority again while
    // the toggle button is still held; never interpret that transition as a
    // second press that immediately shows the overlay again.
    inputtransition::suppressSyntheticPress(
        impl_->frame.toggleOverlay,
        true,
        frame.toggleOverlay);
    impl_->togglePressLatch.filter(frame.toggleOverlay);
    stabilizeHandActionTransition(
        impl_->frame.left.primary,
        impl_->observedHands.left,
        frame.left.primary);
    stabilizeHandActionTransition(
        impl_->frame.left.grab,
        impl_->observedHands.left,
        frame.left.grab);
    stabilizeHandActionTransition(
        impl_->frame.right.primary,
        impl_->observedHands.right,
        frame.right.primary);
    stabilizeHandActionTransition(
        impl_->frame.right.grab,
        impl_->observedHands.right,
        frame.right.grab);

    impl_->frame = frame;
    impl_->activeHands = keyboardHands;
    impl_->observedHands = observedHands;
    impl_->blockerHand = blockerHand;
    impl_->lastError.clear();
    writeError({}, error);
    return true;
}

const FrameActionState& SteamVRInput::actions() const noexcept {
    return impl_->frame;
}

HandActionState SteamVRInput::actionsFor(Hand hand) const noexcept {
    return hand == Hand::Left ? impl_->frame.left : impl_->frame.right;
}

bool SteamVRInput::triggerHaptic(
    Hand hand,
    float durationSeconds,
    float frequency,
    float amplitude,
    std::string* error) {
    if (!isInitialized()) {
        impl_->lastError = "SteamVRInput is not initialized";
        writeError(impl_->lastError, error);
        return false;
    }

    if (!std::isfinite(durationSeconds) ||
        !std::isfinite(frequency) ||
        !std::isfinite(amplitude)) {
        impl_->lastError = "Haptic parameters must be finite";
        writeError(impl_->lastError, error);
        return false;
    }

    durationSeconds = std::max(durationSeconds, 0.0F);
    frequency = std::max(frequency, 0.0F);
    amplitude = std::clamp(amplitude, 0.0F, 1.0F);
    if (durationSeconds == 0.0F || amplitude == 0.0F) {
        impl_->lastError.clear();
        writeError({}, error);
        return true;
    }

    const std::uint64_t source = hand == Hand::Left
        ? impl_->handles.leftSource
        : impl_->handles.rightSource;
    const ::vr::EVRInputError inputError =
        impl_->input()->TriggerHapticVibrationAction(
            impl_->handles.haptic,
            0.0F,
            durationSeconds,
            frequency,
            amplitude,
            source);
    if (inputError != ::vr::VRInputError_None) {
        return impl_->fail("Trigger keyboard haptic", inputError, error);
    }

    impl_->lastError.clear();
    writeError({}, error);
    return true;
}

void SteamVRInput::setSceneInputSuppressionEnabled(bool enabled) noexcept {
    impl_->suppressSceneInput = enabled;
}

bool SteamVRInput::sceneInputSuppressionEnabled() const noexcept {
    return impl_->suppressSceneInput;
}

bool SteamVRInput::shouldSuppressSceneInput(Hand hand) const noexcept {
    return impl_->blockerHand.has_value() && *impl_->blockerHand == hand;
}

std::optional<Hand> SteamVRInput::activeSuppressionHand() const noexcept {
    return impl_->blockerHand;
}

HandMask SteamVRInput::activeKeyboardHands() const noexcept {
    return impl_->activeHands;
}

bool SteamVRInput::openBindingUI(
    bool showOnDesktop,
    std::string* error) {
    if (!isInitialized()) {
        impl_->lastError = "SteamVRInput is not initialized";
        writeError(impl_->lastError, error);
        return false;
    }
    const ::vr::EVRInputError inputError = impl_->input()->OpenBindingUI(
        nullptr,
        ::vr::k_ulInvalidActionSetHandle,
        ::vr::k_ulInvalidInputValueHandle,
        showOnDesktop);
    if (inputError != ::vr::VRInputError_None) {
        return impl_->fail("Open SteamVR binding UI", inputError, error);
    }
    impl_->lastError.clear();
    writeError({}, error);
    return true;
}

const SteamVRActionHandles& SteamVRInput::handles() const noexcept {
    return impl_->handles;
}

const std::filesystem::path& SteamVRInput::manifestPath() const noexcept {
    return impl_->manifest;
}

const std::string& SteamVRInput::lastError() const noexcept {
    return impl_->lastError;
}

} // namespace flickvr::vr
