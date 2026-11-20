// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/resources/ModelData.hpp>
#include <daidalos/scene/Level.hpp>

#include <string>

namespace daidalos::scene {

/// The model of a block (ADR-0115): its shape at its size in meters, standing on y = 0 and centered across X and
/// Z, the stairs and the ramp rising towards -Z, in one plain material of its color. Its node is named "level", so
/// the game makes it a wall or a floor as it does the level's geometry; texture coordinates are meters, one
/// repeat a meter.
[[nodiscard]] resources::ModelData MakeBlockModel(const LevelBlock& block);
/// The key blocks of the same shape, size and color share their model under.
[[nodiscard]] std::string BlockKey(const LevelBlock& block);

} // namespace daidalos::scene
