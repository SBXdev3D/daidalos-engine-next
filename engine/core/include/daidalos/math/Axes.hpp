// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <glm/vec3.hpp>

namespace daidalos::math {

// World axes (CONVENTIONS.md, section 5): right-handed, +Y up, -Z forward.
inline constexpr glm::vec3 kRight{1.0f, 0.0f, 0.0f};
inline constexpr glm::vec3 kUp{0.0f, 1.0f, 0.0f};
inline constexpr glm::vec3 kForward{0.0f, 0.0f, -1.0f};

} // namespace daidalos::math
