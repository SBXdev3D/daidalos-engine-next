// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/core/Profile.hpp>
#include <daidalos/ecs/Transform.hpp>
#include <daidalos/scene/Scene.hpp>

#include <glm/geometric.hpp>
#include <glm/matrix.hpp>

#include <utility>

namespace daidalos::scene {

Scene::Scene(renderer::Renderer& renderer, std::filesystem::path assetRoot, audio::AudioEngine* audio)
    : m_assets(renderer, assetRoot), m_sounds(audio, std::move(assetRoot)) {}

void Scene::RunFixedStep(core::Seconds step) {
    m_fixedSystems.Run(m_world, step);
    const auto seconds = static_cast<float>(step.count());
    physics::SyncKinematicBodies(m_world, m_physics, seconds);
    m_physics.Step(seconds);
}

void Scene::RunFrame(core::Seconds delta, float fixedAlpha) {
    m_fixedAlpha = fixedAlpha;
    m_frameSystems.Run(m_world, delta);
}

void Scene::Extract(renderer::SceneView& view) {
    DAI_PROFILE_SCOPE("scene::Extract");
    ecs::PropagateTransforms(m_world);
    renderer::ExtractScene(m_world, view, m_activeCamera);
    view.atmosphere = m_atmosphere;
    view.spotLight = m_spotLight;

    audio::SyncSoundEmitters(m_world, m_sounds);
    if (view.camera.has_value()) {
        const glm::mat4 camera = glm::inverse(view.camera->view);
        m_sounds.SetListener(glm::vec3(camera[3]), glm::normalize(-glm::vec3(camera[2])),
                             glm::normalize(glm::vec3(camera[1])));
    }
    m_sounds.Update();
}

} // namespace daidalos::scene
