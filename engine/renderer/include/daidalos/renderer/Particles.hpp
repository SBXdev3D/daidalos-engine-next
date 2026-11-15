// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Handle.hpp>
#include <daidalos/rhi/Types.hpp>

#include <glm/ext/quaternion_float.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <optional>
#include <vector>

namespace daidalos::renderer {

/// Particles that can live at once (ADR-0025, ADR-0053). New ones are dropped while the pool is full.
inline constexpr std::uint32_t kMaxParticles = 65536;

struct ParticleEffectTag;
/// A kind of particle owned by the Renderer, such as smoke or sparks.
using ParticleEffectHandle = core::Handle<ParticleEffectTag>;
struct MaterialTag;
using MaterialHandle = core::Handle<MaterialTag>;

/// What a particle does where it meets a surface, which the frame's acceleration structure holds (ADR-0053).
enum class ParticleCollision : std::uint32_t {
    /// It goes through.
    None,
    /// It bounces off, keeping ParticleEffectDesc::bounce of its speed, slides when too slow to leave, and
    /// rests once slow: casings, shards.
    Bounce,
    /// It stays where it hit: drops of blood on a wall.
    Stick,
    /// It dies there: rain on a roof.
    Die,
};

/// Particles of another effect that the GPU spawns where a particle dies of age or hits a surface
/// (ADR-0053): a splash where a drop lands, smoke where a spark dies. They appear the next frame, turned so
/// that their effect's +Y follows the hit's normal, or the dying particle's axis.
struct ParticleSubEmitter {
    ParticleEffectHandle effect;
    std::uint32_t count = 0;
};

/// A particle's impact on a surface, which the CPU learns of a few frames later (ADR-0053), for decals and
/// sounds. Slides and rests are not impacts.
struct ParticleHit {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f};
    ParticleEffectHandle effect;
    /// The material of the surface hit, or null if it is gone.
    MaterialHandle material;
};

/// A value at a moment of a particle's life, from 0 at its birth to 1 at its death. Between keys, values
/// are blended linearly; before the first and after the last, they hold.
struct ParticleCurveKey {
    float time = 0.0f;
    float value = 1.0f;
};
struct ParticleColorKey {
    float time = 0.0f;
    /// Linear color and opacity.
    glm::vec4 color{1.0f};
};

/// How a particle's quad is turned (ADR-0053).
enum class ParticleOrientation : std::uint32_t {
    /// Facing the camera, turned by the particle's rotation.
    Camera,
    /// Stretched along its velocity, facing the camera as much as it can: rain, sparks, tracers.
    Velocity,
    /// Turning only around the effect's axis to face the camera: flames, a column of smoke.
    Axis,
    /// Lying in the plane across the effect's axis, turned by the particle's rotation: ripples, splashes.
    Plane,
};

/// Where particles appear around their emitter, in its frame (ADR-0053).
enum class ParticleShape : std::uint32_t {
    /// A point in a box of spawnBox's half extents, moved by a point in a sphere of spawnRadius.
    Box,
    /// A point in a cylinder of radius spawnRadius around the effect's direction, spawnBox.y long either way.
    Cylinder,
    /// A point on a disc of radius spawnRadius across the effect's direction; particles leave it opened out,
    /// by up to spread at the rim, as water from a nozzle.
    Cone,
    /// A point on a segment along the emitter's X, spawnBox.x long either way: a cable, a crack.
    Line,
};

/// Samples of each curve per effect, from birth to death: what the GPU reads.
inline constexpr std::uint32_t kParticleCurveSamples = 256;

/// What the particles of an effect look like and how they move, on the GPU (ADR-0025, ADR-0053).
/// Distances are meters and times seconds; directions are in the frame of what emits them.
struct ParticleEffectDesc {
    /// The image, a grid of columns by rows cells, counted row by row (rows = 0: as many as columns).
    rhi::TextureHandle texture;
    std::uint32_t columns = 1;
    std::uint32_t rows = 0;
    /// The flipbook: frames cells from cell on. With a frame rate, it plays at that rate, looped or held on
    /// its last frame; without one, it plays once over the particle's life. Frames are blended.
    std::uint32_t cell = 0;
    std::uint32_t frames = 1;
    float frameRate = 0.0f;
    bool loop = true;
    /// Each particle starts at a frame of its own.
    bool randomStartFrame = false;

    /// Linear color and opacity over life. Particles also fade in over the first fadeIn of their life, so
    /// that a dense stream does not start with the hard outline of stacked quads.
    std::vector<ParticleColorKey> color{{0.0f, glm::vec4(1.0f)}, {1.0f, glm::vec4(1.0f, 1.0f, 1.0f, 0.0f)}};
    float fadeIn = 0.1f;
    /// Multiply the size, the color's light (to glow above 1), and the speed, over life.
    std::vector<ParticleCurveKey> sizeOverLife{{0.0f, 1.0f}};
    std::vector<ParticleCurveKey> emissionOverLife{{0.0f, 1.0f}};
    std::vector<ParticleCurveKey> speedOverLife{{0.0f, 1.0f}};

    /// Adds light instead of covering what is behind: sparks, flashes.
    bool additive = false;
    /// Takes the light of the lamps around it, like smoke. Otherwise its color is its own.
    bool lit = true;
    /// Meets the surfaces of the frame's acceleration structure, glass included, along its motion.
    ParticleCollision collision = ParticleCollision::None;
    float bounce = 0.3f;
    /// Its impacts are reported to the CPU (Renderer::ParticleHits).
    bool reportHits = false;
    ParticleSubEmitter onDeath;
    ParticleSubEmitter onHit;
    /// Seconds, picked between the two.
    glm::vec2 lifetime{1.0f, 1.0f};
    /// Meters across at birth, picked between the two.
    glm::vec2 size{0.1f, 0.1f};

    ParticleOrientation orientation = ParticleOrientation::Camera;
    /// The axis of Axis and Plane particles, in the emitter's frame.
    glm::vec3 axis{0.0f, 1.0f, 0.0f};
    /// Velocity particles: seconds of motion the quad stretches over, beyond its size.
    float stretch = 0.0f;

    ParticleShape shape = ParticleShape::Box;
    glm::vec3 spawnBox{0.0f};
    float spawnRadius = 0.0f;
    /// The direction they leave in, and up to how many radians away from it.
    glm::vec3 direction{0.0f, 1.0f, 0.0f};
    float spread = 0.0f;
    /// Meters per second, picked between the two.
    glm::vec2 speed{0.0f, 0.0f};
    /// The fraction of the emitter's velocity that particles take with them.
    float inheritVelocity = 0.0f;
    /// Multiplies the Earth's gravity; 0 floats, below 0 rises.
    float gravity = 0.0f;
    /// Fraction of its speed relative to the air lost per second: still air slows it down, and wind carries
    /// it along (ADR-0053). 0 is too heavy for the wind.
    float drag = 0.0f;
    /// Meters per second squared of swirling air, curl noise whose eddies are turbulenceScale meters across.
    float turbulence = 0.0f;
    float turbulenceScale = 1.0f;
    /// Radians per second, either way.
    float rotationSpeed = 0.0f;
    /// Meters over which a particle fades into a surface behind it, so that it does not cut through it.
    float softness = 0.3f;
};

/// The curves of an effect as the GPU reads them: kParticleCurveSamples colors, then as many of size,
/// emission and speed (the fourth value unused).
[[nodiscard]] std::vector<glm::vec4> BakeParticleCurves(const ParticleEffectDesc& desc);

/// New particles of the frame: count particles of an effect, turned like the emitter, spread along the
/// way from previousPosition to position (where the emitter was last frame and is now), and carrying
/// inheritVelocity of velocity, the emitter's.
struct ParticleSpawn {
    ParticleEffectHandle effect;
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    std::uint32_t count = 0;
    /// Same as position when it did not move.
    std::optional<glm::vec3> previousPosition;
    glm::vec3 velocity{0.0f};
};

} // namespace daidalos::renderer
