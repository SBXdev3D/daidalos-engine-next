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

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "AssetBrowser.hpp"
#include "History.hpp"
#include "MaterialPanel.hpp"
#include "Paint.hpp"
#include "Picking.hpp"
#include "SequencePanel.hpp"

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
    void OnEvent(daidalos::app::Application& app, const daidalos::platform::Event& event) override;
    void OnStop(daidalos::app::Application& app) override;

private:
    /// What waits for an answer about unsaved changes.
    enum class Pending { None, Open, New, Reload, Quit };

    // The document.
    daidalos::core::Result<void> Open(daidalos::app::Application& app, const std::string& levelPath);
    /// Saves to the level's file, or to another path (Save As), whose paths are then rewritten to name the same
    /// files. Returns false if it failed; the status says why.
    bool Save(const std::string& levelPath = {});
    /// The level, a material or the open sequence differs from its file.
    [[nodiscard]] bool Dirty() const {
        return m_history.Current() != m_savedRevision || m_materials.Dirty() || m_sequences.Dirty();
    }
    /// Changes the level through edit, as one step of the history labelled label, and makes the scene follow.
    /// Edits made while the mouse holds a widget or a gizmo join the step they started.
    void Change(std::string_view label, const std::function<void(daidalos::scene::Level&)>& edit);
    /// Puts an edited entity in the level, through Change.
    void Apply(const daidalos::scene::LevelEntity& after, std::string_view label);
    /// Goes to a level given by the history, and makes the scene follow.
    void GoTo(daidalos::scene::Level level);
    void Undo();
    void Redo();
    /// Creates an entity at a point, or a few meters in front of the camera, and selects it.
    void Create(std::string_view label, daidalos::scene::LevelEntity entity,
                std::optional<glm::vec3> position = std::nullopt);
    /// Places an asset of the browser: a model, an effect or a ribbon becomes an entity, at what the view shows
    /// under the point if one is given; a sky becomes the level's; a level opens.
    void PlaceAsset(daidalos::app::Application& app, const AssetEntry& asset, std::optional<glm::vec2> at);
    /// A path under the asset root, relative to the level's directory, as level files name them.
    [[nodiscard]] std::string RelativeToLevel(const std::string& path) const;
    void DeleteSelected();
    /// Copies the selection, with what is under it, next to itself, and selects the copies.
    void Duplicate();
    /// Puts the selection on the system's clipboard, as level text that another editor can paste.
    void CopySelection();
    void Paste();
    void SelectAll();
    /// Moves an entity under another, or to the root with 0, where it stays in the world.
    void Reparent(std::uint64_t child, std::uint64_t parent);

    // Prefabs (ADR-0078).
    /// Saves the selection, with what is under it, as a prefab file under the asset root, and puts one entity
    /// placing it where the selection was. Returns false if it could not; the status says why.
    bool MakePrefab(const std::string& path);
    /// Replaces the prefab an entity places by copies of its entities, under it, which are then the level's.
    void UnpackPrefab(std::uint64_t id);
    /// Reads the prefabs again, after one was saved in another editor.
    void ReloadPrefabs();

    // Materials and sequences (ADR-0079).
    /// Shows the materials of the selection's model in their panel, unless a click chose one already.
    void FollowSelectionMaterials();
    /// The panel's materials for what a click hit: the model it belongs to and the material it is drawn with.
    void ShowMaterialOf(daidalos::ecs::Entity drawn);
    /// Opens the sequence a trigger plays, making its path first if it has none.
    void EditSequence(std::uint64_t id);
    /// Runs the sequence panel and what it asks: a preview, or the scene made again.
    void UpdateSequences(float seconds);
    /// Makes every entity of the level again, undoing what a preview changed.
    void RemakeScene();
    /// A camera cue's path and where a move cue goes, in the view.
    void DrawSelectedCue();

    // Painting (ADR-0078).
    /// Strokes of the paint tool in the view: stamps where the mouse drags over surfaces, one step of history
    /// per stroke, under an entity of their own.
    void PaintStrokes(daidalos::app::Application& app);
    void DrawPaint();
    /// The circle the next stamp covers, on the surface under the mouse.
    void DrawBrush();
    /// The surface under a point of the viewport, if any.
    [[nodiscard]] std::optional<PickHit> SurfaceAt(const glm::vec2& point);
    /// Places one stamp of the brush in the stroke's entity.
    void PlaceStamp(const glm::vec3& position, const glm::vec3& normal);
    /// The decal images beside the level, in its directory's Decals folder, relative to the level.
    void FindDecalImages();
    [[nodiscard]] daidalos::scene::LevelEntity* Selected();
    /// Selects one entity alone, or nothing with 0.
    void Select(std::uint64_t id);
    /// Adds an entity to the selection, or takes it out (Ctrl+click).
    void ToggleSelect(std::uint64_t id);
    [[nodiscard]] bool IsSelected(std::uint64_t id) const;
    /// The selected entities that no other selected entity holds: what a gizmo, a copy or a delete moves.
    [[nodiscard]] std::vector<std::uint64_t> SelectionRoots() const;
    void UpdateTitle(daidalos::app::Application& app);
    /// Runs what was waiting once unsaved changes are saved or dropped.
    void Proceed(daidalos::app::Application& app);
    /// Asks about unsaved changes before doing what, unless there are none.
    void Guard(daidalos::app::Application& app, Pending what, std::string path = {});

    // Camera bookmarks, nine per level, kept beside the editor.
    void LoadBookmarks();
    void SaveBookmarks() const;
    void Bookmark(int slot, bool store);

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
    /// Lines every meter on the ground, brighter every ten, fading with distance.
    void DrawGrid();
    void DrawHelp();
    void Pick(daidalos::app::Application& app);
    /// The world ray through a point of the viewport, in interface coordinates: origin and direction.
    [[nodiscard]] std::pair<glm::vec3, glm::vec3> RayAt(const glm::vec2& point) const;
    /// Where a placed asset goes: what the view shows under the point, or a few meters along the ray.
    [[nodiscard]] glm::vec3 PlaceAt(const glm::vec2& point);
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
    std::unique_ptr<AssetBrowser> m_browser;
    /// The primary selection, which the inspector and the gizmo follow: the last one selected.
    std::uint64_t m_selected = 0;
    /// Everything selected, the primary last.
    std::vector<std::uint64_t> m_selection;
    std::string m_status;
    std::string m_hierarchyFilter;
    /// The hierarchy opens the selection's parents and scrolls to it, once, after a pick in the view.
    bool m_scrollToSelection = false;
    bool m_grid = true;
    bool m_snap = false;
    /// Meters, degrees and scale.
    glm::vec3 m_snapSteps{0.25f, 15.0f, 0.1f};
    bool m_showHelp = false;
    std::map<std::string, std::array<std::optional<EditorCamera>, 9>> m_bookmarks;

    // The paint tool: on while m_painting; a stroke lasts while the left button is held in the view.
    bool m_painting = false;
    PaintBrush m_brush;
    std::vector<std::string> m_decalImages;
    bool m_stroking = false;
    std::uint64_t m_strokeEntity = 0;
    glm::vec3 m_lastStamp{0.0f};
    glm::vec3 m_lastNormal{0.0f, 1.0f, 0.0f};
    std::size_t m_strokeStamps = 0;
    MaterialPanel m_materials;
    SequencePanel m_sequences;
    /// Seconds since a preview of the open sequence started, while it plays.
    std::optional<float> m_previewTime;
    /// The selection the material panel last followed: a click's choice of material holds while it stays.
    std::uint64_t m_followedSelection = ~std::uint64_t{0};
    std::uint64_t m_pendingSequence = 0;
    /// What the inspector's buttons asked for, done after it is drawn.
    std::string m_pendingOpenPrefab;
    std::uint64_t m_pendingUnpack = 0;
    std::mt19937 m_random{0x5eed};
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
