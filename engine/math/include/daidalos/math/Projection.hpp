// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <glm/mat4x4.hpp>

namespace daidalos::math {

// Perspective projections for a camera looking down -Z (CONVENTIONS.md, section 5).
// Depth is reverse-Z in [0, 1]: the near plane maps to 1 and the far plane to 0, which spreads
// floating-point precision evenly over the depth range. Clip-space Y points up; the Vulkan
// renderer flips it once with a negative viewport height.

/// verticalFov in radians, aspect = width / height, 0 < nearPlane < farPlane.
[[nodiscard]] glm::mat4 PerspectiveReverseZ(float verticalFov, float aspect, float nearPlane, float farPlane);

/// Same as PerspectiveReverseZ with the far plane at infinity: depth tends to 0 with distance.
[[nodiscard]] glm::mat4 InfinitePerspectiveReverseZ(float verticalFov, float aspect, float nearPlane);

} // namespace daidalos::math
