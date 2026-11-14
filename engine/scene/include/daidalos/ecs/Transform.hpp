// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/math/Transform.hpp>

#include <glm/mat4x4.hpp>

namespace daidalos::ecs {

class World;

/// Placement relative to the parent entity, or to the world for entities without a parent.
/// This is the authoritative transform: edit it, never WorldTransform (ADR-0006).
struct LocalTransform : math::Transform {};

/// Placement in the world, derived from the LocalTransform of the entity and its ancestors by
/// PropagateTransforms. Read-only for everything else.
struct WorldTransform {
    glm::mat4 matrix{1.0f};
};

/// Recomputes the WorldTransform of every entity that has a LocalTransform: world = parentWorld * local.
/// WorldTransform is added where missing. Every world transform is recomputed on every call, so
/// none can be stale after it, whatever was edited.
///
/// A child follows its parent only when the parent has a LocalTransform. A child whose parent has
/// none is placed relative to the world, and its own children follow it as usual.
void PropagateTransforms(World& world);

} // namespace daidalos::ecs
