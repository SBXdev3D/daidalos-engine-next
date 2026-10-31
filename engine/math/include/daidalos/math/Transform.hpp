// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <glm/ext/quaternion_float.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace daidalos::math {

/// Translation, rotation and scale. The matrix is M = T * R * S: scale first, then rotate,
/// then translate (CONVENTIONS.md, section 5).
struct Transform {
    glm::vec3 translation{0.0f};
    /// Unit quaternion, (w, x, y, z).
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};

    bool operator==(const Transform&) const = default;
};

[[nodiscard]] glm::mat4 ToMatrix(const Transform& transform);

/// Transforms a point (w = 1): translation applies.
[[nodiscard]] glm::vec3 TransformPoint(const glm::mat4& matrix, const glm::vec3& point);

/// Transforms a direction (w = 0): translation does not apply. The result is not normalized.
[[nodiscard]] glm::vec3 TransformDirection(const glm::mat4& matrix, const glm::vec3& direction);

/// Rotation from Euler angles, in radians, for editors and configuration files only.
/// Applied as yaw around +Y, then pitch around the rotated +X, then roll around the rotated +Z:
/// R = Ry(yaw) * Rx(pitch) * Rz(roll). Positive yaw turns left, positive pitch looks up.
[[nodiscard]] glm::quat FromYawPitchRoll(float yaw, float pitch, float roll);

} // namespace daidalos::math
