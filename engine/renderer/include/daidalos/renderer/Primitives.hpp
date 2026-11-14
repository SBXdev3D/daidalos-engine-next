// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/resources/ModelData.hpp>

namespace daidalos::renderer {

/// A cube from -0.5 to 0.5 on each axis, with counter-clockwise front faces and hard edges.
/// Each face holds the whole texture.
[[nodiscard]] resources::MeshData MakeCube();

} // namespace daidalos::renderer
