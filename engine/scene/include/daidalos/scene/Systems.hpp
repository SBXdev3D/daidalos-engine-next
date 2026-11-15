// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/audio/Audio.hpp>
#include <daidalos/ecs/World.hpp>
#include <daidalos/physics/PhysicsWorld.hpp>
#include <daidalos/renderer/SceneView.hpp>
#include <daidalos/scene/Components.hpp>

#include <cstdint>
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

/// Adds the particles that each enabled ParticleEmitter emits over delta seconds, at its world transform:
/// by its rate, by the distance it went since the last frame, spread along the way, and by its bursts
/// (ADR-0053). Spawns carry the emitter's velocity. Run it after ecs::PropagateTransforms.
void EmitParticles(ecs::World& world, float delta, std::vector<renderer::ParticleSpawn>& spawns);

/// Adds the lights of the emitters whose effect gives off light (EmitterLight), as bright as they are active.
/// Run it after EmitParticles, which keeps their activity.
void ExtractEmitterLights(ecs::World& world, renderer::SceneView& view);

/// A light's flicker at a time: 1, down to 1 - depth, rate times a second, as a seed makes it.
[[nodiscard]] float Flicker(float rate, float depth, float time, std::uint32_t seed);

/// How many times a burst fires between from and to seconds of its emitter's life, from included, to not.
[[nodiscard]] std::uint32_t BurstsBetween(const ParticleBurst& burst, float from, float to);

/// Moves the body of each entity with a KinematicBody to its LocalTransform.
void SyncKinematicBodies(ecs::World& world, physics::PhysicsWorld& physics, float step);

/// Moves the voices of SoundEmitters to their entities, from their world transforms.
void SyncSoundEmitters(ecs::World& world, audio::SoundWorld& sounds);

} // namespace daidalos::scene
