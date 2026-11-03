// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <daidalos/core/Handle.hpp>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <optional>
#include <vector>

namespace daidalos::renderer {

struct MeshTag;
/// A mesh owned by the Renderer.
using MeshHandle = core::Handle<MeshTag>;

struct MaterialTag;
/// A material owned by the Renderer.
using MaterialHandle = core::Handle<MaterialTag>;

struct CameraView {
    /// World space to camera space.
    glm::mat4 view{1.0f};
    float verticalFov = 0.0f;
    float nearPlane = 0.0f;
};

/// Marks a draw that is not skinned.
inline constexpr std::uint32_t kNoJoints = 0xFFFFFFFF;

struct DrawItem {
    MeshHandle mesh;
    glm::mat4 world{1.0f};
    MaterialHandle material;
    /// Index into SceneView::jointMatrices of the first matrix of a skinned draw, whose world matrix
    /// is then ignored.
    std::uint32_t firstJoint = kNoJoints;
};

/// Distance fog: surfaces fade into the color, which is also what is seen where nothing is drawn.
struct Atmosphere {
    glm::vec3 fogColor{0.02f, 0.02f, 0.03f};
    /// Fraction of the light lost per meter; 0 for clear air.
    float fogDensity = 0.0f;
};

/// A cone of light, drawn over the fog, for a flashlight. It casts no shadow. The lights of Phase 7
/// will replace it.
struct SpotLight {
    glm::vec3 position{0.0f};
    /// Normalized.
    glm::vec3 direction{0.0f, 0.0f, -1.0f};
    glm::vec3 color{1.0f};
    /// Meters: the light reaches zero there.
    float range = 10.0f;
    /// Radians from the axis: full light inside the first angle, none outside the second.
    float innerAngle = 0.0f;
    float outerAngle = 0.0f;
};

/// What the renderer draws in one frame. It holds copies, so that rendering never touches the ECS
/// (OVERVIEW §10.12).
struct SceneView {
    /// Nothing is drawn without a camera.
    std::optional<CameraView> camera;
    std::vector<DrawItem> draws;
    /// For each joint of the skinned draws: its world matrix times its inverse bind matrix.
    std::vector<glm::mat4> jointMatrices;
    Atmosphere atmosphere;
    std::optional<SpotLight> spotLight;
};

} // namespace daidalos::renderer
