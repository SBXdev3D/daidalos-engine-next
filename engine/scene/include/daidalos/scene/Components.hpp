// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

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

/// ECS component: a light at the entity's WorldTransform, in physical units (ADR-0043). Spot and directional
/// lights shine down the entity's local -Z, like a camera, as in glTF. See renderer::Light.
struct Light {
    renderer::LightType type = renderer::LightType::Point;
    /// Linear color, multiplied by the temperature's.
    glm::vec3 color{1.0f};
    /// Candela for point and spot lights (renderer::PointCandela and SpotCandela convert lumens), lux for
    /// directional lights.
    float intensity = 1.0f;
    /// Kelvin: the color of a black body at this temperature (renderer::ColorTemperature), such as 2700 for
    /// a warm bulb; 0 for none.
    float temperature = 0.0f;
    /// Meters: the light fades to zero there.
    float range = 10.0f;
    /// Spot lights: radians from the axis.
    float innerAngle = glm::radians(20.0f);
    float outerAngle = glm::radians(30.0f);
    /// Meters: the size of the bulb, which softens the shadows (ADR-0039). Zero gives hard shadows.
    float radius = 0.05f;
    /// Directional lights: radians from the center of their disk to its edge, which softens the shadows.
    float angularRadius = 0.0f;
    bool enabled = true;
};

/// ECS component: a box of fog at the entity's WorldTransform (ADR-0051), added to the atmosphere's: the
/// cube from -0.5 to 0.5, scaled by the transform to the box's size. See renderer::FogVolume.
struct FogVolume {
    /// Fraction of the light lost per meter, at the heart of the box.
    float density = 0.1f;
    /// Of the light lost, the fraction scattered rather than absorbed, per color.
    glm::vec3 albedo{0.9f};
    /// The fog thins to nothing towards the faces, over this fraction of the box's half-size.
    float edge = 0.3f;
    /// How much the density varies, from 0 to 1, with a noise that changes every 1 / noiseFrequency meters
    /// and moves at noiseVelocity, in meters per second: steam rises, dust drifts.
    float noise = 0.0f;
    float noiseFrequency = 1.0f;
    glm::vec3 noiseVelocity{0.0f};
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
