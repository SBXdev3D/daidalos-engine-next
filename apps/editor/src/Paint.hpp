// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/scene/Level.hpp>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <optional>
#include <string>
#include <vector>

namespace editor {

/// What the paint tool stamps on the surfaces clicked and dragged over (ADR-0078): decals, or wet patches.
struct PaintBrush {
    enum class Kind { Decal, Wet, Props };
    Kind kind = Kind::Decal;
    /// The decal's image, relative to the level's directory, and its normal map or empty.
    std::string texture = "Decals/grime.png";
    std::string normal;
    /// Meters across a stamp.
    float size = 1.0f;
    /// Meters a stroke goes before the next stamp: below the size, stamps overlap into a smear.
    float spacing = 0.6f;
    /// Each stamp turned at random about the surface's normal, so that a stroke does not repeat.
    bool randomSpin = true;
    /// Each stamp's size times a random factor in [1 - jitter, 1 + jitter].
    float sizeJitter = 0.2f;
    // A decal's look.
    float opacity = 1.0f;
    float roughness = 0.8f;
    // A wet patch's water.
    float wetness = 1.0f;
    float puddles = 0.6f;
    // Scattered props (ADR-0116): models, relative to the level's directory, one picked for each stamp, standing
    // up or turned to the surface.
    std::vector<std::string> models;
    bool alignToSurface = false;
};

/// A rotation whose +Z is normal and whose +Y is as close to up as it can be (on walls the image stands
/// upright), then turned by spin radians about the normal. On floors and ceilings +Y is -Z of the world.
[[nodiscard]] glm::quat SurfaceRotation(const glm::vec3& normal, float spin);

/// The entity one stamp makes, at a point of a surface with that normal: a decal's box straddles the surface and
/// projects along its -Z into it; a wet patch's box stands on it, reaching a little below. scale multiplies the
/// brush's size. Its id, name and parent are left for the caller.
/// The shortest turn of +Y to a normal.
[[nodiscard]] glm::quat UpTo(const glm::vec3& normal);

/// One stamp of the brush; pick chooses among a props brush's models.
[[nodiscard]] daidalos::scene::LevelEntity Stamp(const PaintBrush& brush, const glm::vec3& position,
                                                 const glm::vec3& normal, float spin, float scale = 1.0f,
                                                 std::size_t pick = 0);

/// Where a stroke stamps next along the path from its last stamp to the pointer: spacing meters from the last,
/// or nothing while the pointer is closer than that.
[[nodiscard]] std::optional<glm::vec3> NextStamp(const glm::vec3& last, const glm::vec3& pointer, float spacing);

} // namespace editor
