// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "Paint.hpp"

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat3x3.hpp>

#include <algorithm>
#include <cmath>

namespace editor {

namespace scene = daidalos::scene;

namespace {

/// How deep a decal's box reaches on each side of the surface, as a fraction of its size: deep enough for
/// bumps and corners, shallow enough not to print through a thin wall.
constexpr float kDecalDepth = 0.4f;
/// A wet patch's box: its height above the surface, and how far below it reaches, in meters.
constexpr float kWetHeight = 0.5f;
constexpr float kWetBelow = 0.1f;
/// Water stands this far above the surface where it pools.
constexpr float kPuddleDepth = 0.02f;

} // namespace

glm::quat SurfaceRotation(const glm::vec3& normal, float spin) {
    const glm::vec3 z = glm::normalize(normal);
    // On a wall, +X is horizontal and +Y climbs it; on a floor or a ceiling there is no up to follow.
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    glm::vec3 x = std::abs(z.y) < 0.98f ? glm::normalize(glm::cross(up, z)) : glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 y = glm::normalize(glm::cross(z, x));
    x = glm::cross(y, z);
    const glm::quat facing = glm::quat_cast(glm::mat3(x, y, z));
    return glm::normalize(facing * glm::angleAxis(spin, glm::vec3(0.0f, 0.0f, 1.0f)));
}

scene::LevelEntity Stamp(const PaintBrush& brush, const glm::vec3& position, const glm::vec3& normal, float spin,
                         float scale) {
    const glm::vec3 n = glm::normalize(normal);
    const float size = std::max(brush.size * scale, 0.01f);
    scene::LevelEntity entity;
    if (brush.kind == PaintBrush::Kind::Decal) {
        entity.transform.translation = position;
        entity.transform.rotation = SurfaceRotation(n, spin);
        entity.transform.scale = glm::vec3(size, size, size * kDecalDepth * 2.0f);
        entity.decal = scene::Decal{
            .texture = brush.texture, .normal = brush.normal, .opacity = brush.opacity, .roughness = brush.roughness};
        return entity;
    }
    // A wet patch's box has its height along the normal: its local +Y.
    const glm::quat surface = SurfaceRotation(n, spin);
    entity.transform.rotation =
        glm::normalize(surface * glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f)));
    entity.transform.translation = position + n * (kWetHeight * 0.5f - kWetBelow);
    entity.transform.scale = glm::vec3(size, kWetHeight, size);
    entity.wet =
        scene::WetVolume{.wetness = brush.wetness, .puddles = brush.puddles, .puddleHeight = kWetBelow + kPuddleDepth};
    return entity;
}

std::optional<glm::vec3> NextStamp(const glm::vec3& last, const glm::vec3& pointer, float spacing) {
    const glm::vec3 step = pointer - last;
    const float distance = glm::length(step);
    if (distance < std::max(spacing, 0.01f)) {
        return std::nullopt;
    }
    return last + step * (std::max(spacing, 0.01f) / distance);
}

} // namespace editor
