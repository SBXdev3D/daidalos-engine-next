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
#include <string>
#include <vector>

// The components that the engine itself reads. Game components live in the game.
namespace daidalos::scene {

/// ECS component: the entity's WorldTransform places the camera, which looks down its local -Z.
struct Camera {
    float verticalFov = glm::radians(60.0f);
    float nearPlane = 0.1f;
};

/// ECS component: draws a mesh at the entity's WorldTransform.
struct MeshRenderer {
    renderer::MeshHandle mesh;
    /// Null for the default material: plain white.
    renderer::MaterialHandle material;
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
};

/// ECS component: the name of the model node an entity comes from, for the code that builds on a level.
struct NodeName {
    std::string value;
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
