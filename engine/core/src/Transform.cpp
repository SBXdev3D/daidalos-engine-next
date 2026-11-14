// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/math/Axes.hpp>
#include <daidalos/math/Transform.hpp>

#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec4.hpp>

namespace daidalos::math {

glm::mat4 ToMatrix(const Transform& transform) {
    // Built directly as T * R * S: the rotation columns are scaled, and the translation fills the last column.
    glm::mat4 matrix = glm::mat4_cast(transform.rotation);
    matrix[0] *= transform.scale.x;
    matrix[1] *= transform.scale.y;
    matrix[2] *= transform.scale.z;
    matrix[3] = glm::vec4(transform.translation, 1.0f);
    return matrix;
}

glm::vec3 TransformPoint(const glm::mat4& matrix, const glm::vec3& point) {
    return glm::vec3(matrix * glm::vec4(point, 1.0f));
}

glm::vec3 TransformDirection(const glm::mat4& matrix, const glm::vec3& direction) {
    return glm::vec3(matrix * glm::vec4(direction, 0.0f));
}

glm::quat FromYawPitchRoll(float yaw, float pitch, float roll) {
    return glm::angleAxis(yaw, kUp) * glm::angleAxis(pitch, kRight) * glm::angleAxis(roll, -kForward);
}

} // namespace daidalos::math
