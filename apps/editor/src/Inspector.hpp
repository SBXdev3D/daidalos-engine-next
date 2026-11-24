// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/scene/AssetLibrary.hpp>
#include <daidalos/scene/Level.hpp>

#include <glm/vec3.hpp>

#include <string>

namespace editor {

/// A text field over a std::string. Returns true when it changed.
bool InputString(const char* label, std::string& value);

/// The fields of each component the entity has, a button to remove each, and a menu to add one. Returns true
/// when anything changed. Angles show in degrees. The model the entity places, if any, sizes its body.
bool InspectComponents(daidalos::scene::LevelEntity& entity, const daidalos::scene::Model* model = nullptr);

/// The sky, fog, exposure and wind of the level. Returns true when anything changed.
bool InspectEnvironment(daidalos::scene::LevelEnvironment& environment);

/// The label a component's icon shows in the hierarchy and the viewport: "L" for a light, and so on.
[[nodiscard]] const char* KindLabel(const daidalos::scene::LevelEntity& entity);
/// The color of an entity's marker in the viewport, as ImGui's packed RGBA.
[[nodiscard]] unsigned MarkerColor(const daidalos::scene::LevelEntity& entity);

} // namespace editor
