// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <daidalos/core/Handle.hpp>
#include <daidalos/renderer/Particles.hpp>
#include <daidalos/rhi/Types.hpp>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstddef>
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
    /// Radians from the bottom of the view to the top. Without one, the renderer's field of view applies
    /// (RendererConfig::fieldOfView), across the view.
    std::optional<float> verticalFov;
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
    /// Stops the rays of shadows (ADR-0035). The first-person weapons do not.
    bool castsShadows = true;
};

/// Light that reaches every surface, distance fog, and the sky: surfaces fade into the fog color, and
/// where nothing is drawn, the sky shows, or the fog color without one.
struct Atmosphere {
    glm::vec3 fogColor{0.02f, 0.02f, 0.03f};
    /// Fraction of the light lost per meter; 0 for clear air.
    float fogDensity = 0.0f;
    /// Linear color, of what comes from above. White shows every surface with its own colors, which
    /// suits a scene without lights; a dark place keeps a little of it so that shadows are not pure black.
    glm::vec3 ambient{1.0f};
    /// What comes from below. A surface facing sideways gets the average of the two, and its normal map
    /// shows where it leans up or down. The same color as ambient makes the light even.
    glm::vec3 ambientBelow{1.0f};
    /// A cube texture (rhi::TextureDesc::cube) seen where nothing is drawn, and the light that rays which
    /// leave the scene find (ADR-0036). Null for none.
    rhi::TextureHandle sky;
    /// Multiplies the sky's colors, which are linear light.
    float skyIntensity = 1.0f;
};

enum class LightType { Point, Spot };

/// A light of the frame (ADR-0022). Its color times its intensity is what a white surface facing it
/// reflects up close: a light of color 0.4 lights a white wall to 0.4, as in the legacy engine.
struct Light {
    LightType type = LightType::Point;
    glm::vec3 position{0.0f};
    /// Spot lights shine along it. Normalized.
    glm::vec3 direction{0.0f, 0.0f, -1.0f};
    /// Linear color, already multiplied by the intensity.
    glm::vec3 color{1.0f};
    /// Meters: the light reaches zero there.
    float range = 10.0f;
    /// Fraction of the range lit at full strength, after which the light fades linearly to zero.
    float falloff = 0.5f;
    /// Spot lights: radians from the axis, full light inside the inner angle, none outside the outer.
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
    std::vector<Light> lights;
    /// New particles of the frame (ADR-0025).
    std::vector<ParticleSpawn> particleSpawns;
    /// Seconds since the previous frame, by which particles move.
    float timeStep = 0.0f;
};

} // namespace daidalos::renderer
