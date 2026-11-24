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
#include <daidalos/scene/Bodies.hpp>
#include <daidalos/scene/Perception.hpp>
#include <daidalos/scene/Sequence.hpp>
#include <daidalos/scene/Systems.hpp>
#include <daidalos/scene/Text.hpp>

#include <glm/vec4.hpp>

#include <algorithm>
#include <chrono>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
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
    /// Throws the loose objects within radius of center away from it (ADR-0127): an impulse of strength
    /// kilogram-meters per second at the center, falling to none at the radius as the square of what is left, lifted a
    /// little, at most what gives a body 25 m/s, at a point a little off its center so that it spins, and a fifth of
    /// it behind a wall. Joints pulled past their force break. Returns how many bodies it pushed.
    std::size_t Blast(const glm::vec3& center, float radius, float strength);

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
    /// Plays a sequence of cues from now (ADR-0071), alongside those already playing.
    void PlaySequence(std::shared_ptr<const Sequence> sequence);
    /// Plays a .sequence.json file, relative to the asset root, reading it the first time.
    core::Result<void> PlaySequenceFile(std::string_view path);
    [[nodiscard]] std::size_t PlayingSequences() const { return m_sequences.size(); }
    /// The names of the event cues played since the last call, for the game's code.
    [[nodiscard]] std::vector<std::string> TakeEvents() { return std::exchange(m_events, {}); }
    /// Whether a camera cue has the camera.
    [[nodiscard]] bool InCinematic() const { return m_cinematic.has_value(); }
    /// Where the camera cue has the camera now, if one has it: for the game to open doors on its way (ADR-0120).
    [[nodiscard]] std::optional<glm::vec3> CinematicPosition() const;
    /// Gives the camera back at once, as when a player skips a cinematic (ADR-0086), and lifts a fade
    /// (ADR-0122). The sequence's other cues go on.
    void StopCinematic() {
        m_cinematic.reset();
        m_fade = {};
    }
    /// What covers the picture now, from fade cues (ADR-0122): a linear color, and how much, 0 for nothing.
    /// The game draws it over the scene, under its menus.
    [[nodiscard]] glm::vec4 Fade() const;
    /// Shows a subtitle or a sound's caption (ADR-0075) for its seconds; the same text shown again starts
    /// over rather than showing twice. The game draws them, as its settings say.
    void ShowCaption(Caption caption);
    [[nodiscard]] std::span<const Caption> Captions() const { return m_captions; }
    static constexpr std::size_t kMaxCaptions = 4;
    /// How much the camera shakes, from 0 (not at all) to 1 (ADR-0075): an accessibility setting.
    void SetShakeScale(float scale) { m_shakeScale = std::clamp(scale, 0.0f, 1.0f); }
    [[nodiscard]] float ShakeScale() const { return m_shakeScale; }

    /// A sound that AI can hear (ADR-0070), kept for kNoiseMemory seconds.
    void MakeNoise(const Noise& noise) { m_noises.push_back(noise); }
    [[nodiscard]] std::span<const Noise> Noises() const { return m_noises; }
    void SpawnDecal(const DecalSpawn& spawn);
    [[nodiscard]] std::size_t TransientDecalCount() const { return m_transientDecals.size(); }

    void SetPaused(bool paused);
    [[nodiscard]] bool IsPaused() const { return m_paused; }
    /// Physics holds still while everything else runs: an editor's scene, until it simulates (ADR-0110).
    void SetPhysicsPaused(bool paused);
    [[nodiscard]] bool IsPhysicsPaused() const { return m_physicsPaused; }

    /// The sounds and dust of loose objects hitting, rolling and scraping (ADR-0125), from a directory under the
    /// asset root that holds hit_<surface>_light_<n>.wav, hit_<surface>_heavy_<n>.wav, roll_<surface>.wav,
    /// scrape_<surface>.wav and snap_<surface>.wav (a joint breaking, ADR-0126) for the surfaces it has, and a dust
    /// effect if any. Without them, bodies are silent.
    void LoadPhysicsSounds(std::string_view directory, std::string_view dustEffect = {});
    [[nodiscard]] const PhysicsFeedback& Feedback() const { return m_feedback; }

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
    bool m_physicsPaused = false;
    physics::PhysicsWorld m_physics;
    std::optional<navigation::NavMesh> m_navigation;
    std::vector<Noise> m_noises;
    // Sequences (ADR-0071): those playing, the files read, and what their cues set going.
    struct PlayingSequence {
        std::shared_ptr<const Sequence> sequence;
        float time = 0.0f;
        std::size_t next = 0;
    };
    struct Moving {
        ecs::Entity entity;
        glm::vec3 from{0.0f};
        glm::vec3 to{0.0f};
        float seconds = 0.0f;
        float time = 0.0f;
    };
    struct Fading {
        glm::vec3 color{0.0f};
        float from = 0.0f;
        float to = 0.0f;
        float seconds = 0.0f;
        float time = 0.0f;
    };
    struct Cinematic {
        std::vector<CameraKey> keys;
        CameraPath path = CameraPath::Eased;
        float time = 0.0f;
    };
    void UpdateTriggers();
    void RunSequences();
    void RunCue(const Cue& cue);
    [[nodiscard]] ecs::Entity Named(std::string_view name);
    std::filesystem::path m_assetRoot;
    std::vector<PlayingSequence> m_sequences;
    std::map<std::string, std::shared_ptr<const Sequence>, std::less<>> m_sequenceFiles;
    std::vector<Moving> m_moving;
    std::optional<Cinematic> m_cinematic;
    Fading m_fade;
    float m_shake = 0.0f;
    float m_shakeTime = 0.0f;
    float m_shakeLength = 0.0f;
    float m_shakeScale = 1.0f;
    std::vector<Caption> m_captions;
    std::vector<std::string> m_events;
    audio::SoundWorld m_sounds;
    PhysicsFeedback m_feedback;
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
    /// The blasts' spin, the same in every run (ADR-0127).
    std::uint32_t m_blastSeed = 0x2545f491U;
    PowerCircuits m_power;
    std::vector<renderer::ParticleSpawn> m_bursts;
    EmissionView m_emission;
    std::vector<Thunder> m_thunder;
    /// When Extract last looked for changed effect files.
    std::chrono::steady_clock::time_point m_effectsChecked;
};

} // namespace daidalos::scene
