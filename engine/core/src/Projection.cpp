// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/core/Assert.hpp>
#include <daidalos/math/Projection.hpp>

#include <cmath>

namespace daidalos::math {

namespace {

// Shared x and y scaling; z and w are set by the caller. Column-major: matrix[column][row].
glm::mat4 PerspectiveBase(float verticalFov, float aspect, float nearPlane) {
    DAI_ASSERT(verticalFov > 0.0f && aspect > 0.0f && nearPlane > 0.0f, "Invalid perspective parameters");
    const float focal = 1.0f / std::tan(verticalFov * 0.5f);
    glm::mat4 matrix(0.0f);
    matrix[0][0] = focal / aspect;
    matrix[1][1] = focal;
    // clip.w = -view.z: the distance in front of the camera.
    matrix[2][3] = -1.0f;
    return matrix;
}

} // namespace

glm::mat4 PerspectiveReverseZ(float verticalFov, float aspect, float nearPlane, float farPlane) {
    DAI_ASSERT(farPlane > nearPlane, "The far plane must be beyond the near plane");
    glm::mat4 matrix = PerspectiveBase(verticalFov, aspect, nearPlane);
    // depth = (a * z + b) / -z, with depth(-near) = 1 and depth(-far) = 0.
    matrix[2][2] = nearPlane / (farPlane - nearPlane);
    matrix[3][2] = nearPlane * farPlane / (farPlane - nearPlane);
    return matrix;
}

glm::mat4 InfinitePerspectiveReverseZ(float verticalFov, float aspect, float nearPlane) {
    glm::mat4 matrix = PerspectiveBase(verticalFov, aspect, nearPlane);
    // The limit of the finite version as far goes to infinity: depth = near / -z.
    matrix[3][2] = nearPlane;
    return matrix;
}

} // namespace daidalos::math
