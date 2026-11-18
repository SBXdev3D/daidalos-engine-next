// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/audio/Audio.hpp>
#include <daidalos/core/FrameTimer.hpp>
#include <daidalos/ecs/Schedule.hpp>
#include <daidalos/ecs/World.hpp>
#include <daidalos/navigation/NavMesh.hpp>
#include <daidalos/physics/PhysicsWorld.hpp>
#include <daidalos/renderer/Renderer.hpp>
#include <daidalos/renderer/SceneView.hpp>
#include <daidalos/scene/AssetLibrary.hpp>
#include <daidalos/scene/Perception.hpp>
#include <daidalos/scene/Systems.hpp>

#include <chrono>
#include <deque>
#include <filesystem>
#include <optional>
#include <utility>
#include <vector>

namespace daidalos::scene {

/// A world and what it needs to run: its entities, its assets, its physics, its systems and the camera
/// it is seen through (ADR-0017). Assets live exactly as long as the entities that use them, because they die
/// together. A level is a glTF file loaded into the scene's library, plus the code that builds on it:
/// there is no scene file format.
///
/// The renderer must outlive the scene. app::Application takes care of that for its current scene.
class Scene {
public:
    /// Assets are found under the root.
    /// The audio engine is optional: without one, the scene is silent. It must outlive the scene.
    Scene(renderer::Renderer& renderer, std::filesystem::path assetRoot, audio::AudioEngine* audio = nullptr);
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    [[nodiscard]] ecs::World& World() { return m_world; }
    [[nodiscard]] AssetLibrary& Assets() { return m_assets; }
    [[nodiscard]] physics::PhysicsWorld& Physics() { return m_physics; }
    /// Where walkers can go (ADR-0069), once the game has built it from the level; null before.
    [[nodiscard]] navigation::NavMesh* Navigation() { return m_navigation.has_value() ? &*m_navigation : nullptr; }
    void SetNavigation(navigation::NavMesh mesh) { m_navigation.emplace(std::move(mesh)); }
    [[nodiscard]] audio::SoundWorld& Sounds() { return m_sounds; }

    /// Runs on every fixed step (60 Hz by default), for what must not depend on the frame rate.
    [[nodiscard]] ecs::Schedule& FixedSystems() { return m_fixedSystems; }
    /// Runs once per frame.
    [[nodiscard]] ecs::Schedule& FrameSystems() { return m_frameSystems; }

    /// The entity seen through, which needs a Camera. Without one, or if it is gone, the camera with
    /// the lowest entity index is used.
    void SetActiveCamera(ecs::Entity camera) { m_activeCamera = camera; }
    [[nodiscard]] ecs::Entity ActiveCamera() const { return m_activeCamera; }

    /// Ambient light and fog of the scene, in the view handed to the renderer. Lights are entities.
    void SetAtmosphere(const renderer::Atmosphere& atmosphere) { m_atmosphere = atmosphere; }
    [[nodiscard]] const renderer::Atmosphere& Atmosphere() const { return m_atmosphere; }

    /// Emits particles once, in this frame's view: count of them, leaving along direction, which stands for
    /// the effect's own direction (+Y unless it says otherwise). For impacts, flashes and the like.
    void SpawnParticles(renderer::ParticleEffectHandle effect, const glm::vec3& position, const glm::vec3& direction,
                        std::uint32_t count);

    /// How many particles are worth emitting, and how wide the camera sees (EmissionView), from the next
    /// Extract: renderer.particle_density and the field of view.
    void SetEmission(float density, float fieldOfView) {
        m_emission.density = density;
        m_emission.fieldOfView = fieldOfView;
    }
    /// The thunder that reached the camera since the last call (Lightning), for sounds.
    [[nodiscard]] std::vector<Thunder> TakeThunder() { return std::exchange(m_thunder, {}); }

    /// Stops time in the scene, for a game's pause menu: its systems, physics, animations, particles and
    /// sounds hold where they are, and it is still drawn as it was.
    /// The power circuits lights are on (ADR-0057): game code cuts and restores them.
    [[nodiscard]] PowerCircuits& Power() { return m_power; }
    /// Seconds of unpaused frames, which animated lights follow.
    [[nodiscard]] double LightClock() const { return m_lightClock; }

    /// A decal that comes and goes (ADR-0060): a bullet hole, a splash of blood. It lies on the surface at
    /// position, facing along normal, turned by a random angle, and fades out over the last seconds of its life.
    /// The oldest go first once there are kMaxTransientDecals.
    struct DecalSpawn {
        glm::vec3 position{0.0f};
        glm::vec3 normal{0.0f, 1.0f, 0.0f};
        /// Meters across, and how deep it reaches into the surface and out of it.
        float size = 0.1f;
        float depth = 0.1f;
        rhi::TextureHandle albedo;
        rhi::TextureHandle normalMap;
        glm::vec3 color{1.0f};
        float roughness = 0.6f;
        float metallic = 0.0f;
        float lifetime = 60.0f;
        float fade = 5.0f;
    };
    static constexpr std::size_t kMaxTransientDecals = 96;
    /// Rings on still water (ADR-0061): a step in a puddle, a shot into it. The 16 most recent show.
    void AddRipple(const glm::vec3& position, float strength = 1.0f);
    /// A sound that AI can hear (ADR-0070), kept for kNoiseMemory seconds.
    void MakeNoise(const Noise& noise) { m_noises.push_back(noise); }
    [[nodiscard]] std::span<const Noise> Noises() const { return m_noises; }
    void SpawnDecal(const DecalSpawn& spawn);
    [[nodiscard]] std::size_t TransientDecalCount() const { return m_transientDecals.size(); }

    void SetPaused(bool paused);
    [[nodiscard]] bool IsPaused() const { return m_paused; }

    /// Runs the fixed systems, then moves the physics to where they left the entities. Does nothing
    /// while paused.
    void RunFixedStep(core::Seconds step);
    /// Runs the frame systems, then plays the animations. fixedAlpha is how far this frame is into the
    /// next fixed step, in [0, 1). Does nothing while paused.
    void RunFrame(core::Seconds delta, float fixedAlpha = 0.0f);
    /// For frame systems that draw between two fixed steps.
    [[nodiscard]] float FixedAlpha() const { return m_fixedAlpha; }
    /// Computes the world transforms and fills the view. Sounds follow the same transforms, and the
    /// listener is the camera.
    void Extract(renderer::SceneView& view);
    /// The next view does not follow from the last one, after a teleport: temporal antialiasing starts
    /// over instead of smearing the jump (ADR-0037). A scene's first view is always one.
    void CutView() { m_cut = true; }

    /// Reads again the particle effect files changed on disk (AssetLibrary::ReloadChangedParticleEffects),
    /// and gives their emitters the files' emission. Development builds do it twice a second in Extract.
    void ReloadChangedParticleEffects();

private:
    // In this order, so that entities are gone before the assets they refer to.
    AssetLibrary m_assets;
    ecs::World m_world;
    physics::PhysicsWorld m_physics;
    std::optional<navigation::NavMesh> m_navigation;
    std::vector<Noise> m_noises;
    audio::SoundWorld m_sounds;
    ecs::Schedule m_fixedSystems;
    ecs::Schedule m_frameSystems;
    ecs::Entity m_activeCamera;
    renderer::Atmosphere m_atmosphere;
    float m_fixedAlpha = 0.0f;
    bool m_paused = false;
    bool m_cut = true;
    /// Seconds of the last frame, for the particles of the next view.
    float m_frameDelta = 0.0f;
    double m_lightClock = 0.0;
    struct TransientDecal {
        renderer::Decal decal;
        float age = 0.0f;
        float lifetime = 0.0f;
        float fade = 0.0f;
    };
    std::deque<TransientDecal> m_transientDecals;
    std::deque<renderer::Ripple> m_ripples;
    std::uint32_t m_decalSeed = 0x9e3779b9U;
    PowerCircuits m_power;
    std::vector<renderer::ParticleSpawn> m_bursts;
    EmissionView m_emission;
    std::vector<Thunder> m_thunder;
    /// When Extract last looked for changed effect files.
    std::chrono::steady_clock::time_point m_effectsChecked;
};

} // namespace daidalos::scene
