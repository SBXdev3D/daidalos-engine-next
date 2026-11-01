// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/core/Profile.hpp>
#include <daidalos/ecs/Transform.hpp>
#include <daidalos/scene/Scene.hpp>

#include <utility>

namespace daidalos::scene {

Scene::Scene(renderer::Renderer& renderer, std::filesystem::path assetRoot)
    : m_assets(renderer, std::move(assetRoot)) {}

void Scene::RunFixedStep(core::Seconds step) {
    m_fixedSystems.Run(m_world, step);
}

void Scene::RunFrame(core::Seconds delta) {
    m_frameSystems.Run(m_world, delta);
}

void Scene::Extract(renderer::SceneView& view) {
    DAI_PROFILE_SCOPE("scene::Extract");
    ecs::PropagateTransforms(m_world);
    renderer::ExtractScene(m_world, view, m_activeCamera);
    view.atmosphere = m_atmosphere;
}

} // namespace daidalos::scene
