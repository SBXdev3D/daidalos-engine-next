// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/animation/Animation.hpp>
#include <daidalos/core/Profile.hpp>
#include <daidalos/ecs/Transform.hpp>
#include <daidalos/scene/Scene.hpp>
#include <daidalos/scene/Systems.hpp>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/matrix.hpp>

#include <cmath>
#include <utility>

namespace daidalos::scene {

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
    : m_assets(renderer, assetRoot), m_sounds(audio, std::move(assetRoot)) {}

void Scene::SetPaused(bool paused) {
    m_paused = paused;
    m_sounds.SetPaused(paused);
}

void Scene::RunFixedStep(core::Seconds step) {
    if (m_paused) {
        return;
    }
    m_fixedSystems.Run(m_world, step);
    const auto seconds = static_cast<float>(step.count());
    SyncKinematicBodies(m_world, m_physics, seconds);
    m_physics.Step(seconds);
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
    ExtractScene(m_world, view, m_activeCamera);
    view.atmosphere = m_atmosphere;
    view.timeStep = m_frameDelta;
    view.cut = std::exchange(m_cut, false);
    view.particleSpawns = std::move(m_bursts);
    m_bursts.clear();
    EmitParticles(m_world, m_frameDelta, view.particleSpawns);

    SyncSoundEmitters(m_world, m_sounds);
    if (view.camera.has_value()) {
        const glm::mat4 camera = glm::inverse(view.camera->view);
        m_sounds.SetListener(glm::vec3(camera[3]), glm::normalize(-glm::vec3(camera[2])),
                             glm::normalize(glm::vec3(camera[1])));
    }
    m_sounds.Update();
}

} // namespace daidalos::scene
