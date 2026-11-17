// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/app/Application.hpp>
#include <daidalos/platform/Process.hpp>
#include <daidalos/scene/Level.hpp>
#include <daidalos/scene/Scene.hpp>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "History.hpp"
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
    bool OnCloseRequested(daidalos::app::Application& app) override;
    void OnStop(daidalos::app::Application& app) override;

private:
    /// What waits for an answer about unsaved changes.
    enum class Pending { None, Open, New, Reload, Quit };

    // The document.
    daidalos::core::Result<void> Open(daidalos::app::Application& app, const std::string& levelPath);
    /// Saves to the level's file, or to another path (Save As), whose paths are then rewritten to name the same
    /// files. Returns false if it failed; the status says why.
    bool Save(const std::string& levelPath = {});
    [[nodiscard]] bool Dirty() const { return m_history.Current() != m_savedRevision; }
    /// Changes the level through edit, as one step of the history labelled label, and makes the scene follow.
    /// Edits made while the mouse holds a widget or a gizmo join the step they started.
    void Change(std::string_view label, const std::function<void(daidalos::scene::Level&)>& edit);
    /// Puts an edited entity in the level, through Change.
    void Apply(const daidalos::scene::LevelEntity& after, std::string_view label);
    /// Goes to a level given by the history, and makes the scene follow.
    void GoTo(daidalos::scene::Level level);
    void Undo();
    void Redo();
    /// Creates an entity a few meters in front of the camera, and selects it.
    void Create(std::string_view label, daidalos::scene::LevelEntity entity);
    void DeleteSelected();
    [[nodiscard]] daidalos::scene::LevelEntity* Selected();
    void Select(std::uint64_t id);
    void UpdateTitle(daidalos::app::Application& app);
    /// Runs what was waiting once unsaved changes are saved or dropped.
    void Proceed(daidalos::app::Application& app);
    /// Asks about unsaved changes before doing what, unless there are none.
    void Guard(daidalos::app::Application& app, Pending what, std::string path = {});

    // Autosave and play.
    void Autosave(float seconds);
    [[nodiscard]] std::filesystem::path AutosaveFile() const;
    /// Offers the autosave of the level just opened, if it is newer than the file.
    void CheckAutosave();
    /// Starts the sandbox on the level as it is, saved or not; from the camera if fromCamera.
    void Play(bool fromCamera);

    // The interface.
    void DrawMenu(daidalos::app::Application& app);
    void DrawDialogs(daidalos::app::Application& app);
    void HandleShortcuts(daidalos::app::Application& app);
    void FrameSelection();
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
    std::string m_status;

    History m_history;
    History::Revision m_savedRevision = 0;
    /// The window's title, given again only when it changes.
    std::string m_shownTitle;

    float m_autosaveSeconds = 120.0f;
    float m_sinceAutosave = 0.0f;
    History::Revision m_autosavedRevision = 0;
    std::optional<daidalos::scene::Level> m_recovered;

    daidalos::platform::ChildProcess m_play;
    std::filesystem::path m_playFile;

    // Dialogs: which to open this frame, what waits for the answer about unsaved changes, and their fields.
    std::string m_openPopup;
    Pending m_pending = Pending::None;
    std::string m_pendingPath;
    std::vector<std::string> m_levelFiles;
    std::string m_filter;
    std::string m_pathField;

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
