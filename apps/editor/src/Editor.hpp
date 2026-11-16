// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/app/Application.hpp>
#include <daidalos/scene/Level.hpp>
#include <daidalos/scene/Scene.hpp>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "Picking.hpp"

namespace editor {

/// The viewport's camera: it flies with the right mouse button held, as in most level editors.
struct EditorCamera {
    glm::vec3 position{0.0f, 2.0f, 6.0f};
    /// Radians: yaw 0 looks along -Z and turns left as it grows; pitch looks up as it grows.
    float yaw = 0.0f;
    float pitch = -0.2f;
    /// Meters per second.
    float speed = 6.0f;
    float verticalFov = 1.0471976f;
    float nearPlane = 0.05f;
    float farPlane = 2000.0f;

    [[nodiscard]] glm::mat4 World() const;
    [[nodiscard]] glm::mat4 View() const;
    [[nodiscard]] glm::vec3 Forward() const;
};

/// The level editor (ADR-0055): the game's renderer as its viewport, a hierarchy of the level's entities, an
/// inspector of their components, gizmos to move them, and the level file as its document.
class Editor final : public daidalos::app::IApplicationHandler {
public:
    daidalos::core::Result<void> OnStart(daidalos::app::Application& app) override;
    void OnUpdate(daidalos::app::Application& app, const daidalos::core::FrameTime& time) override;
    void OnRender(daidalos::app::Application& app, daidalos::renderer::SceneView& view) override;

private:
    // The document.
    daidalos::core::Result<void> Open(daidalos::app::Application& app, const std::string& levelPath);
    void Save();
    /// Puts an edited entity in the level and in the scene: a change of transform moves the entity, any other
    /// change makes it again.
    void Apply(const daidalos::scene::LevelEntity& after);
    [[nodiscard]] daidalos::scene::LevelEntity* Selected();
    void Select(std::uint64_t id);
    void UpdateTitle(daidalos::app::Application& app);

    // The interface.
    void DrawMenu(daidalos::app::Application& app);
    void DrawDockspace();
    void DrawHierarchy();
    void DrawHierarchyNode(std::uint64_t id, const std::vector<std::vector<std::uint64_t>>& children,
                           const std::vector<std::size_t>& indexOf);
    void DrawInspector();
    void DrawEnvironment();
    void DrawStatus(daidalos::app::Application& app, const daidalos::core::FrameTime& time);

    // The viewport.
    void UpdateCamera(daidalos::app::Application& app, float seconds);
    void DrawGizmo();
    void DrawMarkers();
    void Pick(daidalos::app::Application& app);
    [[nodiscard]] glm::mat4 Projection() const;
    /// Where a world point lands in the viewport, in interface coordinates, if in front of the camera.
    [[nodiscard]] std::optional<glm::vec2> Project(const glm::vec3& point) const;
    [[nodiscard]] bool MouseInViewport() const;

    daidalos::app::Application* m_app = nullptr;
    std::filesystem::path m_assetRoot;
    std::string m_levelPath;
    daidalos::scene::Level m_level;
    std::unique_ptr<daidalos::scene::LevelInstance> m_instance;
    daidalos::scene::Scene* m_scene = nullptr;
    daidalos::ecs::Entity m_cameraEntity;
    EditorCamera m_camera;
    MeshPicker m_picker;
    std::uint64_t m_selected = 0;
    bool m_dirty = false;
    std::string m_status;

    /// The central node of the dockspace, in interface coordinates: where the scene is drawn.
    glm::vec2 m_viewportMin{0.0f};
    glm::vec2 m_viewportMax{0.0f};
    bool m_flying = false;
    bool m_layoutBuilt = false;

    int m_operation = 0;
    bool m_local = false;
    bool m_gizmoUsing = false;
    /// The selection's rotation in degrees, kept while it is edited, so that angles do not jump between Euler
    /// solutions as they are dragged.
    std::uint64_t m_eulerOf = 0;
    glm::vec3 m_euler{0.0f};
};

} // namespace editor
