#pragma once

#include "VrTypes.h"

#include <algorithm>
#include <cmath>

namespace flickvr::vr::math {

inline constexpr float kEpsilon = 1.0e-6F;

[[nodiscard]] inline Vec3 add(Vec3 lhs, Vec3 rhs) noexcept {
    return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

[[nodiscard]] inline Vec3 subtract(Vec3 lhs, Vec3 rhs) noexcept {
    return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

[[nodiscard]] inline Vec3 multiply(Vec3 vector, float scalar) noexcept {
    return {vector.x * scalar, vector.y * scalar, vector.z * scalar};
}

[[nodiscard]] inline float dot(Vec3 lhs, Vec3 rhs) noexcept {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

[[nodiscard]] inline Vec3 cross(Vec3 lhs, Vec3 rhs) noexcept {
    return {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x,
    };
}

[[nodiscard]] inline float lengthSquared(Vec3 vector) noexcept {
    return dot(vector, vector);
}

[[nodiscard]] inline float length(Vec3 vector) noexcept {
    return std::sqrt(lengthSquared(vector));
}

[[nodiscard]] inline Vec3 normalized(
    Vec3 vector,
    Vec3 fallback = {0.0F, 0.0F, -1.0F}) noexcept {
    const float magnitude = length(vector);
    if (!std::isfinite(magnitude) || magnitude <= kEpsilon) {
        return fallback;
    }
    return multiply(vector, 1.0F / magnitude);
}

[[nodiscard]] inline Vec3 translation(const RigidTransform& transform) noexcept {
    return {
        transform.matrix[3],
        transform.matrix[7],
        transform.matrix[11],
    };
}

[[nodiscard]] inline Vec3 basisX(const RigidTransform& transform) noexcept {
    return {
        transform.matrix[0],
        transform.matrix[4],
        transform.matrix[8],
    };
}

[[nodiscard]] inline Vec3 basisY(const RigidTransform& transform) noexcept {
    return {
        transform.matrix[1],
        transform.matrix[5],
        transform.matrix[9],
    };
}

[[nodiscard]] inline Vec3 basisZ(const RigidTransform& transform) noexcept {
    return {
        transform.matrix[2],
        transform.matrix[6],
        transform.matrix[10],
    };
}

[[nodiscard]] inline Vec3 transformDirection(
    const RigidTransform& transform,
    Vec3 direction) noexcept {
    return {
        transform.matrix[0] * direction.x +
            transform.matrix[1] * direction.y +
            transform.matrix[2] * direction.z,
        transform.matrix[4] * direction.x +
            transform.matrix[5] * direction.y +
            transform.matrix[6] * direction.z,
        transform.matrix[8] * direction.x +
            transform.matrix[9] * direction.y +
            transform.matrix[10] * direction.z,
    };
}

[[nodiscard]] inline Vec3 transformPoint(
    const RigidTransform& transform,
    Vec3 point) noexcept {
    return add(transformDirection(transform, point), translation(transform));
}

[[nodiscard]] inline RigidTransform multiply(
    const RigidTransform& lhs,
    const RigidTransform& rhs) noexcept {
    RigidTransform result{};

    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            float value = 0.0F;
            for (int index = 0; index < 3; ++index) {
                value += lhs.matrix[row * 4 + index] *
                    rhs.matrix[index * 4 + column];
            }
            result.matrix[row * 4 + column] = value;
        }

        result.matrix[row * 4 + 3] =
            lhs.matrix[row * 4] * rhs.matrix[3] +
            lhs.matrix[row * 4 + 1] * rhs.matrix[7] +
            lhs.matrix[row * 4 + 2] * rhs.matrix[11] +
            lhs.matrix[row * 4 + 3];
    }

    return result;
}

// This inverse intentionally assumes a rigid (orthonormal) transform. OpenVR
// tracked poses satisfy that contract and avoiding a general matrix inverse keeps
// controller dragging stable and inexpensive.
[[nodiscard]] inline RigidTransform inverseRigid(
    const RigidTransform& transform) noexcept {
    RigidTransform inverse{};

    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            inverse.matrix[row * 4 + column] =
                transform.matrix[column * 4 + row];
        }
    }

    const Vec3 position = translation(transform);
    inverse.matrix[3] = -dot(basisX(transform), position);
    inverse.matrix[7] = -dot(basisY(transform), position);
    inverse.matrix[11] = -dot(basisZ(transform), position);
    return inverse;
}

[[nodiscard]] inline RigidTransform fromBasis(
    Vec3 right,
    Vec3 up,
    Vec3 backward,
    Vec3 position) noexcept {
    RigidTransform transform{};
    transform.matrix = {
        right.x, up.x, backward.x, position.x,
        right.y, up.y, backward.y, position.y,
        right.z, up.z, backward.z, position.z,
    };
    return transform;
}

[[nodiscard]] inline Ray forwardRay(const RigidTransform& transform) noexcept {
    return {
        translation(transform),
        normalized(multiply(basisZ(transform), -1.0F)),
    };
}

[[nodiscard]] inline float degreesToRadians(float degrees) noexcept {
    return degrees * 0.017453292519943295769F;
}

[[nodiscard]] inline float clampUnit(float value) noexcept {
    return std::clamp(value, -1.0F, 1.0F);
}

} // namespace flickvr::vr::math
