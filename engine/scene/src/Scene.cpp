// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/animation/Animation.hpp>
#include <daidalos/core/Profile.hpp>
#include <daidalos/ecs/Transform.hpp>
#include <daidalos/logging/Log.hpp>
#include <daidalos/scene/Scene.hpp>
#include <daidalos/scene/Systems.hpp>

#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/matrix.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <iterator>
#include <utility>

namespace daidalos::scene {

namespace {
constexpr std::string_view kModule = "scene";
} // namespace

namespace {

/// The shortest rotation that turns one unit vector into another.
glm::quat RotationBetween(const glm::vec3& from, const glm::vec3& to) {
    const float cosine = glm::dot(from, to);
    if (cosine < -0.9999f) {
        // Opposite: half a turn around any axis across from.
        const glm::vec3 axis = std::abs(from.x) < 0.9f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 0.0f, 1.0f);
        return glm::angleAxis(glm::pi<float>(), glm::normalize(glm::cross(from, axis)));
    }
    const glm::vec3 axis = glm::cross(from, to);
    return glm::normalize(glm::quat(1.0f + cosine, axis.x, axis.y, axis.z));
}

} // namespace

Scene::Scene(renderer::Renderer& renderer, std::filesystem::path assetRoot, audio::AudioEngine* audio)
    : m_assets(renderer, assetRoot), m_assetRoot(assetRoot), m_sounds(audio, std::move(assetRoot)) {}

void Scene::SetPaused(bool paused) {
    m_paused = paused;
    m_sounds.SetPaused(paused);
}

void Scene::RunFixedStep(core::Seconds step) {
    if (m_paused) {
        return;
    }
    m_fixedSystems.Run(m_world, step);
    if (m_physicsPaused) {
        return;
    }
    const auto seconds = static_cast<float>(step.count());
    SyncKinematicBodies(m_world, m_physics, seconds);
    m_physics.Step(seconds);
    // Loose objects go where physics put them (ADR-0073).
    m_world.Each<RigidBody, ecs::LocalTransform>(
        [this](ecs::Entity, const RigidBody& body, ecs::LocalTransform& transform) {
            transform.translation = m_physics.BodyPosition(body.body);
            transform.rotation = m_physics.BodyRotation(body.body);
        });
}

void Scene::SpawnParticles(renderer::ParticleEffectHandle effect, const glm::vec3& position, const glm::vec3& direction,
                           std::uint32_t count) {
    const float length = glm::length(direction);
    const glm::quat rotation = length > 0.0f ? RotationBetween(glm::vec3(0.0f, 1.0f, 0.0f), direction / length)
                                             : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    m_bursts.push_back({.effect = effect, .position = position, .rotation = rotation, .count = count});
}

void Scene::RunFrame(core::Seconds delta, float fixedAlpha) {
    if (m_paused) {
        // Particles hold still too: they move by the time of the last frame.
        m_frameDelta = 0.0f;
        return;
    }
    m_fixedAlpha = fixedAlpha;
    m_frameDelta = static_cast<float>(delta.count());
    m_lightClock += delta.count();
    for (TransientDecal& decal : m_transientDecals) {
        decal.age += m_frameDelta;
    }
    std::erase_if(m_transientDecals, [](const TransientDecal& decal) { return decal.age >= decal.lifetime; });
    for (renderer::Ripple& ripple : m_ripples) {
        ripple.age += m_frameDelta;
    }
    std::erase_if(m_ripples, [](const renderer::Ripple& ripple) { return ripple.age > 2.0f; });
    // Noises fade from memory (ADR-0070).
    for (Noise& noise : m_noises) {
        noise.age += m_frameDelta;
    }
    std::erase_if(m_noises, [](const Noise& noise) { return noise.age > kNoiseMemory; });
    // Triggers set off sequences, and sequences play their cues (ADR-0071).
    UpdateTriggers();
    RunSequences();
    // Wet places dry (ADR-0061).
    m_world.Each<WetVolume>([this](ecs::Entity, WetVolume& volume) {
        volume.wetness = std::max(volume.wetness - volume.dryingRate * m_frameDelta / 60.0f, 0.0f);
    });
    m_frameSystems.Run(m_world, delta);
    animation::UpdateAnimators(m_world, static_cast<float>(delta.count()));
}

void Scene::ReloadChangedParticleEffects() {
    for (const ParticleEffectAsset& reloaded : m_assets.ReloadChangedParticleEffects()) {
        m_world.Each<ParticleEmitter>([&](ecs::Entity /*entity*/, ParticleEmitter& emitter) {
            if (emitter.effect == reloaded.effect) {
                emitter.emission = reloaded.emission;
            }
        });
    }
}

void Scene::Extract(renderer::SceneView& view) {
    DAI_PROFILE_SCOPE("scene::Extract");
#if DAI_DEV_TOOLS
    // Effect files edited while the game runs (ADR-0053).
    constexpr std::chrono::milliseconds kEffectCheckPeriod{500};
    if (const auto now = std::chrono::steady_clock::now(); now - m_effectsChecked >= kEffectCheckPeriod) {
        m_effectsChecked = now;
        ReloadChangedParticleEffects();
    }
#endif
    ecs::PropagateTransforms(m_world);
    AnimateLights(m_world, m_lightClock, m_power);
    ExtractScene(m_world, view, m_activeCamera);
    // A camera cue has the camera, and a shake shakes whichever has it (ADR-0071).
    if (view.camera.has_value()) {
        if (m_cinematic.has_value()) {
            const CameraKey key = CameraAt(m_cinematic->keys, m_cinematic->time, m_cinematic->path);
            const glm::vec3 forward = key.target - key.position;
            if (const float length = glm::length(forward); length > 1e-4f) {
                // Straight up or down, world up tells nothing: the top of the picture is then towards -Z.
                const bool vertical = std::abs(forward.y) > 0.99999f * length;
                view.camera->view = glm::lookAt(key.position, key.target,
                                                vertical ? glm::vec3(0.0f, 0.0f, -1.0f) : glm::vec3(0.0f, 1.0f, 0.0f));
            }
            view.camera->aperture = key.aperture;
            view.camera->focusDistance = key.focus;
        }
        if (m_shakeTime > 0.0f && m_shakeLength > 0.0f) {
            // Handheld: two slow sines on each axis, fading out.
            const float left = m_shakeTime / m_shakeLength;
            const auto t = static_cast<float>(m_lightClock);
            const float amount = m_shake * m_shakeScale * left * glm::radians(1.5f);
            const float pitch = amount * (std::sin(t * 17.0f) + 0.5f * std::sin(t * 29.0f));
            const float yaw = amount * (std::sin(t * 13.0f + 1.0f) + 0.5f * std::sin(t * 23.0f));
            const glm::mat4 shake = glm::rotate(glm::rotate(glm::mat4(1.0f), pitch, glm::vec3(1.0f, 0.0f, 0.0f)), yaw,
                                                glm::vec3(0.0f, 1.0f, 0.0f));
            view.camera->view = shake * view.camera->view;
        }
    }
    view.atmosphere = m_atmosphere;
    view.timeStep = m_frameDelta;
    view.cut = std::exchange(m_cut, false);
    view.particleSpawns = std::move(m_bursts);
    m_bursts.clear();
    // Who sees the particles: the view's camera.
    m_emission.camera =
        view.camera.has_value() ? std::optional<glm::mat4>(glm::inverse(view.camera->view)) : std::nullopt;
    EmissionView emission = m_emission;
    if (view.camera.has_value() && view.camera->verticalFov.has_value()) {
        // A camera of its own field of view, top to bottom: across, as wide as 21:9 at most.
        emission.fieldOfView = 2.0f * std::atan(std::tan(*view.camera->verticalFov * 0.5f) * 21.0f / 9.0f);
    }
    EmitParticles(m_world, m_frameDelta, view.particleSpawns, emission);
    EmitWeather(m_world, m_frameDelta, view.particleSpawns, emission);
    ExtractEmitterLights(m_world, view);
    view.ripples.assign(m_ripples.begin(), m_ripples.end());
    // Decals that come and go, fading at the end of their lives (ADR-0060).
    for (const TransientDecal& transient : m_transientDecals) {
        renderer::Decal decal = transient.decal;
        const float left = transient.lifetime - transient.age;
        decal.opacity *= transient.fade > 0.0f ? std::clamp(left / transient.fade, 0.0f, 1.0f) : 1.0f;
        view.decals.push_back(decal);
    }
    ExtractRibbons(m_world, m_frameDelta, view);
    UpdateLightning(m_world, m_frameDelta, view, emission, m_thunder);

    SyncSoundEmitters(m_world, m_sounds);
    if (view.camera.has_value()) {
        const glm::mat4 camera = glm::inverse(view.camera->view);
        m_sounds.SetListener(glm::vec3(camera[3]), glm::normalize(-glm::vec3(camera[2])),
                             glm::normalize(glm::vec3(camera[1])));
    }
    m_sounds.Update();
}

void Scene::PlaySequence(std::shared_ptr<const Sequence> sequence) {
    if (sequence != nullptr) {
        m_sequences.push_back({std::move(sequence)});
    }
}

glm::vec4 Scene::Fade() const {
    // Eased in and out, as the camera's keys.
    const float t = m_fade.seconds > 0.0f ? std::clamp(m_fade.time / m_fade.seconds, 0.0f, 1.0f) : 1.0f;
    return {m_fade.color, glm::mix(m_fade.from, m_fade.to, t * t * (3.0f - 2.0f * t))};
}

std::optional<glm::vec3> Scene::CinematicPosition() const {
    if (!m_cinematic.has_value()) {
        return std::nullopt;
    }
    return CameraAt(m_cinematic->keys, m_cinematic->time, m_cinematic->path).position;
}

core::Result<void> Scene::PlaySequenceFile(std::string_view path) {
    auto found = m_sequenceFiles.find(path);
    if (found == m_sequenceFiles.end()) {
        const std::filesystem::path file = m_assetRoot / path;
        std::ifstream stream(file, std::ios::binary);
        if (!stream) {
            return core::MakeError(core::ErrorCode::NotFound, kModule,
                                   std::format("Missing sequence '{}'", file.generic_string()));
        }
        const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        DAI_TRY_ASSIGN(Sequence sequence, ParseSequence(text, path));
        found = m_sequenceFiles.emplace(std::string(path), std::make_shared<const Sequence>(std::move(sequence))).first;
    }
    PlaySequence(found->second);
    return {};
}

ecs::Entity Scene::Named(std::string_view name) {
    ecs::Entity found;
    m_world.Each<NodeName>([&](ecs::Entity entity, const NodeName& node) {
        if (found.IsNull() && node.value == name) {
            found = entity;
        }
    });
    return found;
}

void Scene::UpdateTriggers() {
    std::vector<glm::vec3> activators;
    m_world.Each<TriggerActivator, ecs::WorldTransform>(
        [&](ecs::Entity, const TriggerActivator&, const ecs::WorldTransform& transform) {
            activators.emplace_back(transform.matrix[3]);
        });
    std::vector<std::string> fired;
    m_world.Each<Trigger, ecs::WorldTransform>(
        [&](ecs::Entity entity, const Trigger& trigger, const ecs::WorldTransform& transform) {
            TriggerState& state = m_world.Has<TriggerState>(entity) ? m_world.Get<TriggerState>(entity)
                                                                    : m_world.Add<TriggerState>(entity);
            const glm::mat4 inverse = glm::inverse(transform.matrix);
            bool inside = false;
            for (const glm::vec3& position : activators) {
                const glm::vec3 local = glm::vec3(inverse * glm::vec4(position, 1.0f));
                inside = inside || glm::all(glm::lessThanEqual(glm::abs(local), glm::vec3(0.5f)));
            }
            if (inside && !state.inside && trigger.enabled && !(trigger.once && state.fired)) {
                state.fired = true;
                fired.push_back(trigger.sequence);
            }
            state.inside = inside;
        });
    for (const std::string& sequence : fired) {
        if (const core::Result<void> played = PlaySequenceFile(sequence); !played) {
            DAI_LOG_WARNING(kModule, "Trigger: {}", core::Format(played.error()));
        }
    }
}

void Scene::RunSequences() {
    // Cues due this frame, in order.
    for (std::size_t index = 0; index < m_sequences.size(); ++index) {
        PlayingSequence& playing = m_sequences[index];
        playing.time += m_frameDelta;
        while (playing.next < playing.sequence->cues.size() &&
               playing.sequence->cues[playing.next].at <= playing.time) {
            const Cue cue = playing.sequence->cues[playing.next++];
            RunCue(cue);
        }
    }
    std::erase_if(m_sequences,
                  [](const PlayingSequence& playing) { return playing.next >= playing.sequence->cues.size(); });
    for (Moving& moving : m_moving) {
        moving.time = std::min(moving.time + m_frameDelta, moving.seconds);
        if (ecs::LocalTransform* transform = m_world.TryGet<ecs::LocalTransform>(moving.entity)) {
            const float t = moving.seconds > 0.0f ? moving.time / moving.seconds : 1.0f;
            transform->translation = glm::mix(moving.from, moving.to, t * t * (3.0f - 2.0f * t));
        }
    }
    std::erase_if(m_moving, [](const Moving& moving) { return moving.time >= moving.seconds; });
    if (m_cinematic.has_value()) {
        m_cinematic->time += m_frameDelta;
        if (m_cinematic->keys.empty() || m_cinematic->time > m_cinematic->keys.back().at) {
            m_cinematic.reset();
            m_cut = true;
        }
    }
    m_fade.time = std::min(m_fade.time + m_frameDelta, m_fade.seconds);
    m_shakeTime = std::max(m_shakeTime - m_frameDelta, 0.0f);
    for (Caption& caption : m_captions) {
        caption.age += m_frameDelta;
    }
    std::erase_if(m_captions, [](const Caption& caption) { return caption.age >= caption.seconds; });
}

void Scene::ShowCaption(Caption caption) {
    caption.age = 0.0f;
    if (const auto same = std::find_if(
            m_captions.begin(), m_captions.end(),
            [&](const Caption& shown) { return shown.text == caption.text && shown.speaker == caption.speaker; });
        same != m_captions.end()) {
        *same = std::move(caption);
        return;
    }
    if (m_captions.size() >= kMaxCaptions) {
        m_captions.erase(m_captions.begin());
    }
    m_captions.push_back(std::move(caption));
}

void Scene::RunCue(const Cue& cue) {
    const auto entityPosition = [this](ecs::Entity entity) -> std::optional<glm::vec3> {
        if (const ecs::WorldTransform* transform = m_world.TryGet<ecs::WorldTransform>(entity)) {
            return glm::vec3(transform->matrix[3]);
        }
        return std::nullopt;
    };
    switch (cue.kind) {
    case Cue::Kind::Power:
        m_power.Set(cue.name, cue.on, m_lightClock);
        break;
    case Cue::Kind::Light:
        if (Light* light = m_world.TryGet<Light>(Named(cue.name))) {
            light->enabled = cue.on;
        }
        break;
    case Cue::Kind::Sound: {
        const core::Result<audio::ClipHandle> clip = m_sounds.Load(cue.path);
        if (!clip) {
            DAI_LOG_WARNING(kModule, "Sequence: {}", core::Format(clip.error()));
            break;
        }
        const std::optional<glm::vec3> at = cue.name.empty() ? std::nullopt : entityPosition(Named(cue.name));
        m_sounds.Play(*clip, {.position = at, .volume = cue.volume, .ducking = cue.ducking});
        if (!cue.caption.empty()) {
            ShowCaption({.text = cue.caption, .position = at, .reach = at.has_value() ? 25.0f : 0.0f});
        }
        break;
    }
    case Cue::Kind::Particles: {
        const core::Result<ParticleEffectAsset> effect = m_assets.LoadParticleEffect(cue.path);
        const ecs::Entity entity = Named(cue.name);
        const std::optional<glm::vec3> at = entityPosition(entity);
        if (effect && at.has_value()) {
            // The effect's +Y is the entity's (ADR-0122): a hit on a wall sprays out of it.
            glm::vec3 up(0.0f, 1.0f, 0.0f);
            if (const ecs::WorldTransform* transform = m_world.TryGet<ecs::WorldTransform>(entity)) {
                const glm::vec3 turned = glm::mat3(transform->matrix) * up;
                if (const float length = glm::length(turned); length > 1e-6f) {
                    up = turned / length;
                }
            }
            SpawnParticles(effect->effect, *at, up, cue.count);
        } else if (!effect) {
            DAI_LOG_WARNING(kModule, "Sequence: {}", core::Format(effect.error()));
        }
        break;
    }
    case Cue::Kind::Move: {
        const ecs::Entity entity = Named(cue.name);
        if (const ecs::LocalTransform* transform = m_world.TryGet<ecs::LocalTransform>(entity)) {
            m_moving.push_back({entity, transform->translation, cue.position, cue.seconds, 0.0f});
        }
        break;
    }
    case Cue::Kind::Visible: {
        const ecs::Entity entity = Named(cue.name);
        if (m_world.IsAlive(entity)) {
            RayVisibility& visibility = m_world.Has<RayVisibility>(entity) ? m_world.Get<RayVisibility>(entity)
                                                                           : m_world.Add<RayVisibility>(entity);
            visibility = {cue.on, cue.on, cue.on, cue.on};
        }
        break;
    }
    case Cue::Kind::Shake:
        m_shake = std::max(cue.strength, 0.0f);
        m_shakeLength = std::max(cue.seconds, 0.01f);
        m_shakeTime = m_shakeLength;
        break;
    case Cue::Kind::Fade:
        // From where the fade is now, so that a fade cut short turns back without a jump.
        m_fade = {cue.color, Fade().a, cue.strength, std::max(cue.seconds, 0.0f), 0.0f};
        break;
    case Cue::Kind::Camera:
        m_cinematic = Cinematic{cue.keys, cue.cameraPath, 0.0f};
        m_cut = true;
        break;
    case Cue::Kind::Mix:
        m_sounds.SetSnapshot(cue.mix, cue.seconds);
        break;
    case Cue::Kind::Event:
        m_events.push_back(cue.name);
        break;
    case Cue::Kind::Subtitle:
        ShowCaption({.text = cue.caption, .speaker = cue.name, .seconds = cue.seconds});
        break;
    }
}

void Scene::SpawnDecal(const DecalSpawn& spawn) {
    if (spawn.albedo.IsNull()) {
        return;
    }
    // A frame on the surface: Z out of it, the image turned by a random angle about it.
    const glm::vec3 out = glm::normalize(spawn.normal);
    const glm::vec3 helper = std::abs(out.y) < 0.95f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 right = glm::normalize(glm::cross(helper, out));
    glm::vec3 up = glm::cross(out, right);
    m_decalSeed = m_decalSeed * 1664525U + 1013904223U;
    const float angle = static_cast<float>(m_decalSeed >> 8U) / static_cast<float>(1U << 24U) * 6.2831853f;
    const glm::vec3 turnedRight = right * std::cos(angle) + up * std::sin(angle);
    up = glm::cross(out, turnedRight);
    right = turnedRight;
    // The box projects along its -Z, which points into the surface.
    glm::mat4 world(1.0f);
    world[0] = glm::vec4(right * spawn.size, 0.0f);
    world[1] = glm::vec4(up * spawn.size, 0.0f);
    world[2] = glm::vec4(out * spawn.depth, 0.0f);
    world[3] = glm::vec4(spawn.position, 1.0f);
    if (m_transientDecals.size() >= kMaxTransientDecals) {
        m_transientDecals.pop_front();
    }
    m_transientDecals.push_back({.decal = {.world = world,
                                           .albedo = spawn.albedo,
                                           .normal = spawn.normalMap,
                                           .color = spawn.color,
                                           .roughness = spawn.roughness,
                                           .metallic = spawn.metallic,
                                           .angleFade = 0.2f},
                                 .lifetime = spawn.lifetime,
                                 .fade = spawn.fade});
}

void Scene::AddRipple(const glm::vec3& position, float strength) {
    if (m_ripples.size() >= 16) {
        m_ripples.pop_front();
    }
    m_ripples.push_back({.position = position, .age = 0.0f, .strength = strength});
}

} // namespace daidalos::scene
