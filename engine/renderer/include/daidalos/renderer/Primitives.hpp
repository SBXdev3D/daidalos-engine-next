// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/resources/ModelData.hpp>

namespace daidalos::renderer {

/// A cube from -0.5 to 0.5 on each axis, with counter-clockwise front faces and hard edges.
/// Each face holds the whole texture.
[[nodiscard]] resources::MeshData MakeCube();

/// A sphere of radius 0.5 around the origin, smooth, with counter-clockwise front faces: segments around its
/// axis (+Y) and rings from pole to pole. The texture wraps once around it; the tangent follows u.
[[nodiscard]] resources::MeshData MakeSphere(std::uint32_t segments = 48, std::uint32_t rings = 24);

} // namespace daidalos::renderer
