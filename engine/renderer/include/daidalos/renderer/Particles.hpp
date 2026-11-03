// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <daidalos/core/Handle.hpp>
#include <daidalos/rhi/Types.hpp>

#include <glm/ext/quaternion_float.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>

namespace daidalos::renderer {

/// Particles that can live at once (ADR-0025). New ones are dropped while the pool is full.
inline constexpr std::uint32_t kMaxParticles = 16384;

struct ParticleEffectTag;
/// A kind of particle owned by the Renderer, such as smoke or sparks.
using ParticleEffectHandle = core::Handle<ParticleEffectTag>;

/// What the particles of an effect look like and how they move, on the GPU (ADR-0025). Distances are
/// meters and times seconds; directions are in the frame of what emits them.
struct ParticleEffectDesc {
    /// The image, a grid of columns by columns cells; the effect uses one cell, counted row by row.
    rhi::TextureHandle texture;
    std::uint32_t columns = 1;
    std::uint32_t cell = 0;
    /// Linear color and opacity at birth and at death, blended over the particle's life.
    glm::vec4 startColor{1.0f};
    glm::vec4 endColor{1.0f, 1.0f, 1.0f, 0.0f};
    /// Adds light instead of covering what is behind: sparks, flashes.
    bool additive = false;
    /// Takes the light of the lamps around it, like smoke. Otherwise its color is its own.
    bool lit = true;
    /// Bounces off what is drawn, as seen by the camera.
    bool collides = false;
    float bounce = 0.3f;
    /// Seconds, picked between the two.
    glm::vec2 lifetime{1.0f, 1.0f};
    /// Meters across at birth, picked between the two, and how many times larger at death.
    glm::vec2 size{0.1f, 0.1f};
    float growth = 1.0f;
    /// Where particles appear around the emitter: a box of these half extents, and a sphere of this radius.
    glm::vec3 spawnBox{0.0f};
    float spawnRadius = 0.0f;
    /// The direction they leave in, and up to how many radians away from it.
    glm::vec3 direction{0.0f, 1.0f, 0.0f};
    float spread = 0.0f;
    /// Meters per second, picked between the two.
    glm::vec2 speed{0.0f, 0.0f};
    /// Multiplies the Earth's gravity; 0 floats.
    float gravity = 0.0f;
    /// Fraction of the speed lost per second.
    float drag = 0.0f;
    /// Radians per second, either way.
    float rotationSpeed = 0.0f;
    /// Meters over which a particle fades into a surface behind it, so that it does not cut through it.
    float softness = 0.3f;
};

/// New particles of the frame: count particles of an effect, at a place, turned like the emitter.
struct ParticleSpawn {
    ParticleEffectHandle effect;
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    std::uint32_t count = 0;
};

} // namespace daidalos::renderer
