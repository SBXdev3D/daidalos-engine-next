// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/audio/Audio.hpp>
#include <daidalos/ecs/World.hpp>
#include <daidalos/physics/PhysicsWorld.hpp>
#include <daidalos/renderer/Particles.hpp>

#include <glm/vec3.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace daidalos::scene {

struct Model;

/// The name of a body's shape as level files write it: box, sphere, cylinder, capsule or hull.
[[nodiscard]] std::string_view BodyShapeName(physics::ShapeKind shape);
/// The shape of that name, or nothing.
[[nodiscard]] std::optional<physics::ShapeKind> BodyShapeOf(std::string_view name);

/// What bodies can be made of (ADR-0125), as their models' materials and level files name it. Others are heard
/// and felt as wood.
inline constexpr std::array<std::string_view, 9> kBodySurfaces{"metal",  "wood",     "plastic", "cloth", "glass",
                                                               "rubber", "concrete", "tile",    "ice"};

/// How a surface rubs and bounces: steel slides on steel, rubber bounces, a sack lands dead, ice barely holds.
[[nodiscard]] physics::BodyMaterial SurfaceMaterial(std::string_view surface);

/// The surface most of a model's triangles are made of, glass only if nothing else is (a lamp is metal with a
/// glass shade); empty if its materials say nothing.
[[nodiscard]] std::string MainSurface(const Model& model);

/// The points of a model's meshes where its nodes put them, in the model's frame, skinned parts left out: what a
/// hull wraps.
[[nodiscard]] std::vector<glm::vec3> ModelPoints(const Model& model);

/// The sounds of bodies hitting, rolling and scraping, by surface (ADR-0125), and the dust a heavy landing raises.
struct PhysicsSounds {
    std::map<std::string, std::vector<audio::ClipHandle>, std::less<>> lightHits;
    std::map<std::string, std::vector<audio::ClipHandle>, std::less<>> heavyHits;
    std::map<std::string, audio::ClipHandle, std::less<>> rolls;
    std::map<std::string, audio::ClipHandle, std::less<>> scrapes;
    renderer::ParticleEffectHandle dust;
};

/// How loud a hit is, 0 to 1, from how fast the bodies closed: nothing below a gentle touch.
[[nodiscard]] float HitVolume(float approach);
/// A heavy hit: enough momentum, kilogram-meters per second, for the heavy sounds and dust.
[[nodiscard]] bool IsHeavyHit(float approach, float mass);
/// Lighter things sound higher, heavier ones lower.
[[nodiscard]] float HitPitch(float mass);

/// Turns each step's contacts into sounds and dust (ADR-0125): a hit for each body that meets something, at most
/// once in a short while, and a rolling or scraping loop for each body that rolls or slides along something.
/// Its random numbers start from a fixed seed, so that a recording plays the same each time.
class PhysicsFeedback {
public:
    using SpawnDust = std::function<void(const glm::vec3& position, const glm::vec3& direction, std::uint32_t count)>;

    void SetSounds(PhysicsSounds sounds) { m_sounds = std::move(sounds); }
    [[nodiscard]] const PhysicsSounds& Sounds() const { return m_sounds; }

    /// After each physics step.
    void Update(ecs::World& world, const physics::PhysicsWorld& physics, audio::SoundWorld& sounds,
                const SpawnDust& spawnDust, float step);
    /// Stops every loop, as when the scene pauses or the level goes.
    void Stop(audio::SoundWorld& sounds);

    /// Hits played since the start, for tests.
    [[nodiscard]] std::uint32_t HitCount() const { return m_hits; }

private:
    struct Loops {
        audio::VoiceHandle roll;
        audio::VoiceHandle scrape;
        float rollLevel = 0.0f;
        float scrapeLevel = 0.0f;
    };
    void Hit(audio::SoundWorld& sounds, std::string_view surface, const glm::vec3& point, float approach, float mass);
    void Loop(audio::SoundWorld& sounds, audio::VoiceHandle& voice, audio::ClipHandle clip, float level,
              const glm::vec3& position);

    PhysicsSounds m_sounds;
    std::unordered_map<std::uint32_t, float> m_lastHit;
    std::unordered_map<std::uint32_t, Loops> m_loops;
    std::minstd_rand m_random{0x5eed};
    float m_clock = 0.0f;
    std::uint32_t m_hits = 0;
};

} // namespace daidalos::scene
