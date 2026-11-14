// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/audio/Audio.hpp>
#include <daidalos/core/FrameTimer.hpp>
#include <daidalos/ecs/Schedule.hpp>
#include <daidalos/ecs/World.hpp>
#include <daidalos/physics/PhysicsWorld.hpp>
#include <daidalos/renderer/Renderer.hpp>
#include <daidalos/renderer/SceneView.hpp>
#include <daidalos/scene/AssetLibrary.hpp>

#include <filesystem>
#include <optional>
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

    /// Stops time in the scene, for a game's pause menu: its systems, physics, animations, particles and
    /// sounds hold where they are, and it is still drawn as it was.
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

private:
    // In this order, so that entities are gone before the assets they refer to.
    AssetLibrary m_assets;
    ecs::World m_world;
    physics::PhysicsWorld m_physics;
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
    std::vector<renderer::ParticleSpawn> m_bursts;
};

} // namespace daidalos::scene
