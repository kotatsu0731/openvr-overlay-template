#pragma once

#include <array>
#include <cstdint>

namespace flickvr::vr {

enum class Hand : std::uint8_t {
    Left,
    Right,
};

enum class TrackingOrigin : std::uint8_t {
    Seated,
    Standing,
};

enum class TextureColorSpace : std::uint8_t {
    Auto,
    Gamma,
    Linear,
};

enum class ControllerAimSource : std::uint8_t {
    RawPose,
    TipComponent,
    OpenXrAimComponent,
};

struct Vec2 {
    float x{};
    float y{};
};

struct Vec3 {
    float x{};
    float y{};
    float z{};
};

// Row-major rigid transform. Entries 3, 7 and 11 contain the translation.
// The default value is identity, which makes it safe to use as a fallback pose.
struct RigidTransform {
    std::array<float, 12> matrix{
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
    };
};

struct TrackedPose {
    bool connected{};
    bool valid{};
    std::uint32_t deviceIndex{0xFFFFFFFFU};
    RigidTransform transform{};
};

struct Ray {
    Vec3 origin{};
    Vec3 direction{0.0F, 0.0F, -1.0F};
};

struct HandMask {
    bool left{};
    bool right{};

    [[nodiscard]] constexpr bool contains(Hand hand) const noexcept {
        return hand == Hand::Left ? left : right;
    }

    constexpr void set(Hand hand, bool value = true) noexcept {
        if (hand == Hand::Left) {
            left = value;
        } else {
            right = value;
        }
    }

    [[nodiscard]] constexpr bool any() const noexcept {
        return left || right;
    }
};

struct PointerState {
    Hand hand{Hand::Left};
    bool tracked{};
    bool projectsToPlane{};
    bool intersects{};
    std::uint32_t deviceIndex{0xFFFFFFFFU};
    // Physical controller pose remains available for controller-relative
    // operations. aimTransform is the render-model tip/openxr_aim pose used by
    // the visible laser and by laser-grab placement.
    RigidTransform controllerTransform{};
    RigidTransform aimTransform{};
    Ray ray{};
    Vec3 hitPoint{};
    Vec3 hitNormal{};
    Vec2 rawUv{};
    Vec2 uv{};
    float distanceMeters{};
};

struct DigitalActionState {
    bool active{};
    bool held{};
    bool changed{};
    bool pressed{};
    bool released{};
    float updateTimeSeconds{};
    std::uint64_t activeOrigin{};
};

struct AnalogActionState {
    bool active{};
    float x{};
    float y{};
    float deltaX{};
    float deltaY{};
    float updateTimeSeconds{};
    std::uint64_t activeOrigin{};
};

struct HandActionState {
    DigitalActionState primary{};
    DigitalActionState grab{};
    AnalogActionState adjustDistance{};
};

struct FrameActionState {
    DigitalActionState toggleOverlay{};
    HandActionState left{};
    HandActionState right{};
};

[[nodiscard]] constexpr Hand oppositeHand(Hand hand) noexcept {
    return hand == Hand::Left ? Hand::Right : Hand::Left;
}

} // namespace flickvr::vr
