// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/audio/Audio.hpp>
#include <daidalos/ecs/World.hpp>
#include <daidalos/physics/PhysicsWorld.hpp>
#include <daidalos/renderer/SceneView.hpp>
#include <daidalos/scene/Components.hpp>

#include <glm/mat4x4.hpp>

#include <cstdint>
#include <optional>
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

/// Who sees the particles, and how many are worth emitting (ADR-0053).
struct EmissionView {
    /// Multiplies emitters' rates and bursts, and weather's: renderer.particle_density.
    float density = 1.0f;
    /// The camera's world matrix, if any: emitters out of reach or out of view emit nothing.
    std::optional<glm::mat4> camera;
    /// Radians across the view, left to right, as the renderer's field of view; the view is taken as tall as
    /// 4:3.
    float fieldOfView = 1.5f;
};

/// Whether particles within radius of a point may be seen: within distance of the camera, and in its view
/// with that margin. Without a camera, they may.
[[nodiscard]] bool MaySee(const EmissionView& view, const glm::vec3& point, float radius, float distance);

/// Adds the particles that each enabled ParticleEmitter emits over delta seconds, at its world transform:
/// by its rate, by the distance it went since the last frame, spread along the way, and by its bursts
/// (ADR-0053), times the view's density. Spawns carry the emitter's velocity. Emitters with a surface emit on
/// its triangles, one spawn per particle. Emitters that the view may not see emit nothing, but keep time and
/// their light. Run it after ecs::PropagateTransforms.
void EmitParticles(ecs::World& world, float delta, std::vector<renderer::ParticleSpawn>& spawns,
                   const EmissionView& view = {});

/// Adds the particles of the enabled WeatherZones over delta seconds: over the part of each zone within its
/// reach of the camera, at its top, times the view's density. Without a camera, nothing.
void EmitWeather(ecs::World& world, float delta, std::vector<renderer::ParticleSpawn>& spawns,
                 const EmissionView& view);

/// Moves the storms of Lightning entities on by delta seconds: strikes when it is time, draws the bolt while
/// it lasts and makes its light flash in the view, and adds the thunder that reaches the camera to thunder.
/// Run it after ExtractScene, which puts the light in the view.
void UpdateLightning(ecs::World& world, float delta, renderer::SceneView& view, const EmissionView& camera,
                     std::vector<Thunder>& thunder);

/// How bright a strike is at time seconds into it, from 0 to 1: a first flash, then two flickers at the given
/// times, each fading in a few hundredths of a second; nothing after duration.
[[nodiscard]] float StrikeBrightness(float time, const glm::vec2& flickers, float duration);

/// Adds the ribbons of the enabled RibbonEmitters to the view, built at their world transforms, and the lights
/// of those that give off light, after delta seconds (ADR-0053). Run it after ecs::PropagateTransforms.
void ExtractRibbons(ecs::World& world, float delta, renderer::SceneView& view);

/// A bolt from (0, 0, 0) to (1, 0, 0), split generations times, each middle pushed aside in Y and Z by up
/// to displacement, halved at each split; a middle starts a branch with the chance branches, which goes on
/// from it, split as many times as are left. The bolt first, then its branches. The same seed gives the same
/// bolt.
[[nodiscard]] std::vector<std::vector<glm::vec3>> MakeBolt(std::uint32_t generations, float displacement,
                                                           float branches, std::uint32_t seed);

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
