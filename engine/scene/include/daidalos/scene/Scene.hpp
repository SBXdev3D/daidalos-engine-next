// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <daidalos/core/FrameTimer.hpp>
#include <daidalos/ecs/Schedule.hpp>
#include <daidalos/ecs/World.hpp>
#include <daidalos/renderer/AssetLibrary.hpp>
#include <daidalos/renderer/Renderer.hpp>
#include <daidalos/renderer/Scene.hpp>

#include <filesystem>

namespace daidalos::scene {

/// A world and what it needs to run: its entities, its assets, its systems and the camera it is seen
/// through (ADR-0017). Assets live exactly as long as the entities that use them, because they die
/// together. A level is a glTF file loaded into the scene's library, plus the code that builds on it:
/// there is no scene file format.
///
/// The renderer must outlive the scene. app::Application takes care of that for its current scene.
class Scene {
public:
    /// Assets are found under the root.
    Scene(renderer::Renderer& renderer, std::filesystem::path assetRoot);
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    [[nodiscard]] ecs::World& World() { return m_world; }
    [[nodiscard]] renderer::AssetLibrary& Assets() { return m_assets; }

    /// Runs on every fixed step (60 Hz by default), for what must not depend on the frame rate.
    [[nodiscard]] ecs::Schedule& FixedSystems() { return m_fixedSystems; }
    /// Runs once per frame.
    [[nodiscard]] ecs::Schedule& FrameSystems() { return m_frameSystems; }

    /// The entity seen through, which needs a Camera. Without one, or if it is gone, the camera with
    /// the lowest entity index is used.
    void SetActiveCamera(ecs::Entity camera) { m_activeCamera = camera; }
    [[nodiscard]] ecs::Entity ActiveCamera() const { return m_activeCamera; }

    /// Fog of the scene, in the view handed to the renderer.
    void SetAtmosphere(const renderer::Atmosphere& atmosphere) { m_atmosphere = atmosphere; }

    void RunFixedStep(core::Seconds step);
    void RunFrame(core::Seconds delta);
    /// Computes the world transforms and fills the view.
    void Extract(renderer::SceneView& view);

private:
    // In this order, so that entities are gone before the assets they refer to.
    renderer::AssetLibrary m_assets;
    ecs::World m_world;
    ecs::Schedule m_fixedSystems;
    ecs::Schedule m_frameSystems;
    ecs::Entity m_activeCamera;
    renderer::Atmosphere m_atmosphere;
};

} // namespace daidalos::scene
