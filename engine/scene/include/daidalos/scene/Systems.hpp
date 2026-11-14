// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/audio/Audio.hpp>
#include <daidalos/ecs/World.hpp>
#include <daidalos/physics/PhysicsWorld.hpp>
#include <daidalos/renderer/SceneView.hpp>

#include <vector>

namespace daidalos::scene {

// What ties the entities to the renderer, the physics and the sounds. Scene runs them at the right
// moments; they are public for tests and for code that runs a world without a Scene.

/// Fills the view from the entities with a WorldTransform and a Camera, MeshRenderer,
/// SkinnedMeshRenderer or enabled Light. A joint that is gone counts as the skinned entity itself. Run it after
/// ecs::PropagateTransforms. The view goes through the given camera entity if it is alive and has a
/// Camera. Otherwise, the camera with the lowest entity index is used.
/// The vectors of the view are reused, so that a view kept between frames does not reallocate.
/// Each drawn entity and joint keeps its matrix in a DrawnTransform, which the next extraction gives as
/// the previous one; an entity drawn for the first time has no motion.
void ExtractScene(ecs::World& world, renderer::SceneView& view, ecs::Entity activeCamera = {});

/// Adds the particles that each enabled ParticleEmitter emits over delta seconds, at its world transform.
/// Run it after ecs::PropagateTransforms.
void EmitParticles(ecs::World& world, float delta, std::vector<renderer::ParticleSpawn>& spawns);

/// Moves the body of each entity with a KinematicBody to its LocalTransform.
void SyncKinematicBodies(ecs::World& world, physics::PhysicsWorld& physics, float step);

/// Moves the voices of SoundEmitters to their entities, from their world transforms.
void SyncSoundEmitters(ecs::World& world, audio::SoundWorld& sounds);

} // namespace daidalos::scene
