#pragma once

#include "VrTypes.h"

namespace flickvr::vr::inputtransition {

// When a hand first becomes observable, or its action-set priority changes,
// OpenVR may report an already-held button as changed. Keep the held state for
// release tracking, but never reinterpret it as a new physical press edge.
inline void suppressSyntheticPress(
    const DigitalActionState& previous,
    bool wasObserved,
    DigitalActionState& current) noexcept {
    if (current.pressed && (!wasObserved || previous.held)) {
        current.changed = false;
        current.pressed = false;
    }
}

// Allows one press per reliably observed press/release cycle. An action can
// become temporarily inactive when OpenVR changes action-set priorities. If it
// becomes active again while the same physical button is still held, OpenVR
// may report another press even though the intervening inactive sample was not
// a release. Keep the latch armed across inactive samples and clear it only
// after an active, non-held sample confirms the physical release.
class PressEdgeLatch final {
public:
    void filter(DigitalActionState& current) noexcept {
        if (!awaitingRelease_) {
            awaitingRelease_ = current.pressed;
            return;
        }

        if (current.active && !current.held) {
            awaitingRelease_ = false;
            return;
        }

        if (current.pressed) {
            current.changed = false;
            current.pressed = false;
        }
    }

    void reset() noexcept {
        awaitingRelease_ = false;
    }

private:
    bool awaitingRelease_{};
};

} // namespace flickvr::vr::inputtransition
