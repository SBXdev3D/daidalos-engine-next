// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <daidalos/audio/Audio.hpp>
#include <daidalos/ecs/Entity.hpp>
#include <daidalos/physics/PhysicsWorld.hpp>
#include <daidalos/renderer/SceneView.hpp>

#include <glm/mat4x4.hpp>
#include <glm/trigonometric.hpp>

#include <memory>
#include <optional>
#include <string>
#include <vector>

// The components that the engine itself reads. Game components live in the game.
namespace daidalos::scene {

/// ECS component: the entity's WorldTransform places the camera, which looks down its local -Z.
struct Camera {
    /// Radians from the bottom of the view to the top. Without one, the engine's view.fov applies.
    std::optional<float> verticalFov;
    float nearPlane = 0.1f;
};

/// ECS component: draws a mesh at the entity's WorldTransform.
struct MeshRenderer {
    renderer::MeshHandle mesh;
    /// Null for the default material: plain white.
    renderer::MaterialHandle material;
    /// False for what must not darken the scene, such as a weapon held in front of the camera.
    bool castsShadows = true;
};

/// ECS component: draws a mesh that follows joints, which are other entities (ADR-0020). The entity's
/// own transform does not move it. A mesh without skin data is drawn rigid, at the entity.
struct SkinnedMeshRenderer {
    renderer::MeshHandle mesh;
    renderer::MaterialHandle material;
    /// The entities of the skin's joints, in the order of the skin.
    std::vector<ecs::Entity> joints;
    /// For each joint, from the space of the mesh to the space of the joint in the bind pose. Shared by
    /// every instance of the model.
    std::shared_ptr<const std::vector<glm::mat4>> inverseBindMatrices;
    bool castsShadows = true;
};

/// ECS component: the world matrix of a drawn entity, or of a joint, when the last view was extracted.
/// ExtractScene adds and updates it, and gives it to the renderer as the previous matrix, from which
/// motion vectors come (ADR-0037).
struct DrawnTransform {
    glm::mat4 matrix{1.0f};
};

/// ECS component: the name of the model node an entity comes from, for the code that builds on a level.
struct NodeName {
    std::string value;
};

/// ECS component: a light at the entity's WorldTransform. A spot light shines down the entity's local -Z,
/// like a camera. See renderer::Light for what the values mean (ADR-0022).
struct Light {
    renderer::LightType type = renderer::LightType::Point;
    /// Linear color.
    glm::vec3 color{1.0f};
    float intensity = 1.0f;
    /// Meters.
    float range = 10.0f;
    /// Fraction of the range lit at full strength.
    float falloff = 0.5f;
    /// Spot lights: radians from the axis.
    float innerAngle = glm::radians(20.0f);
    float outerAngle = glm::radians(30.0f);
    /// Meters: the size of the bulb, which softens the shadows (ADR-0039). Zero gives hard shadows.
    float radius = 0.05f;
    bool enabled = true;
};

/// ECS component: emits particles of an effect at the entity's WorldTransform, at a steady rate. The
/// effect's directions are in the entity's frame (ADR-0025).
struct ParticleEmitter {
    renderer::ParticleEffectHandle effect;
    /// Particles per second.
    float rate = 10.0f;
    bool enabled = true;
    /// The fraction of a particle carried over to the next frame.
    float pending = 0.0f;
};

/// ECS component: the entity's LocalTransform moves a kinematic body of the scene's physics. The entity
/// must be at the root of the hierarchy, or under parents that do not move.
struct KinematicBody {
    physics::BodyId id;
};

/// ECS component: a voice of the scene's sounds that follows the entity.
struct SoundEmitter {
    audio::VoiceHandle voice;
};

} // namespace daidalos::scene
