// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <daidalos/ecs/World.hpp>
#include <daidalos/renderer/Components.hpp>

#include <glm/mat4x4.hpp>

#include <optional>
#include <vector>

namespace daidalos::renderer {

struct CameraView {
    /// World space to camera space.
    glm::mat4 view{1.0f};
    float verticalFov = 0.0f;
    float nearPlane = 0.0f;
};

struct DrawItem {
    MeshHandle mesh;
    glm::mat4 world{1.0f};
    MaterialHandle material;
};

/// What the renderer draws in one frame. It holds copies, so that rendering never touches the ECS
/// (OVERVIEW §10.12).
struct SceneView {
    /// Nothing is drawn without a camera.
    std::optional<CameraView> camera;
    std::vector<DrawItem> draws;
};

/// Fills the view from the entities with a WorldTransform and a Camera or MeshRenderer. Run it after
/// ecs::PropagateTransforms. With several cameras, the entity with the lowest index is used.
/// The vectors of the view are reused, so that a view kept between frames does not reallocate.
void ExtractScene(ecs::World& world, SceneView& view);

} // namespace daidalos::renderer
