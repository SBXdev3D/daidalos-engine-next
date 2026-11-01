// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <daidalos/core/Handle.hpp>

#include <glm/trigonometric.hpp>

#include <string>

namespace daidalos::renderer {

struct MeshTag;
/// A mesh owned by the Renderer.
using MeshHandle = core::Handle<MeshTag>;

struct MaterialTag;
/// A material owned by the Renderer.
using MaterialHandle = core::Handle<MaterialTag>;

/// ECS component: the entity's WorldTransform places the camera, which looks down its local -Z.
struct Camera {
    float verticalFov = glm::radians(60.0f);
    float nearPlane = 0.1f;
};

/// ECS component: draws a mesh at the entity's WorldTransform.
struct MeshRenderer {
    MeshHandle mesh;
    /// Null for the default material: plain white.
    MaterialHandle material;
};

/// ECS component: the name of the model node an entity comes from, for the code that builds on a level.
struct NodeName {
    std::string value;
};

} // namespace daidalos::renderer
