// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/app/Application.hpp>
#include <daidalos/navigation/NavMesh.hpp>
#include <daidalos/platform/Process.hpp>
#include <daidalos/scene/Level.hpp>
#include <daidalos/scene/ModelBatch.hpp>
#include <daidalos/scene/Scene.hpp>

#include <glm/mat4x4.hpp>

struct ImDrawList;
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
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "AssetBrowser.hpp"
#include "Commands.hpp"
#include "Game.hpp"
#include "History.hpp"
#include "MapCheck.hpp"
#include "MaterialPanel.hpp"
#include "Organise.hpp"
#include "Overlays.hpp"
#include "Paint.hpp"
#include "Picking.hpp"
#include "SequencePanel.hpp"
#include "Views.hpp"

namespace editor {

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
    enum class Pending { None, Open, New, Reload, Quit, CloseTab };
    /// How the viewport lights the level (ADR-0105).
    enum class Lighting { Lit, Unlit, LightingOnly, EditorLight };

    /// A level whose models load on workers while the editor keeps drawing the one open (ADR-0105).
    struct Loading {
        std::string path;
        daidalos::scene::Level level;
        std::unique_ptr<daidalos::scene::Scene> scene;
        std::shared_ptr<daidalos::scene::ModelBatch> batch;
        /// Where the camera goes once it is open; at the level's player start without one.
        std::optional<EditorCamera> camera;
        float seconds = 0.0f;
        /// The batch finished: the level is made at the start of the next frame, outside the workers' delivery.
        bool ready = false;
        /// It opens in a tab of its own, beside the documents open (ADR-0109).
        bool newTab = false;
    };

    // The document.
    /// Reads the level file and starts loading its models; the level opens once they are in (FinishOpen). An
    /// error is returned only when the file cannot be read.
    daidalos::core::Result<void> Open(daidalos::app::Application& app, const std::string& levelPath,
                                      std::optional<EditorCamera> camera = std::nullopt, bool newTab = false);
    /// Makes the loaded level's entities and puts its scene in the view.
    void FinishOpen(daidalos::app::Application& app);
    /// The loader's window, with its progress and Cancel.
    void DrawLoading();
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
    // The history of every document, and tabs (ADR-0109).
    struct Tab;
    /// What the history keeps of the editor now: the level, the edited materials, the open sequence.
    [[nodiscard]] Document Snapshot() const;
    /// Goes to a state given by the history, and makes the scene, the materials and the sequence follow.
    void GoTo(Document document);
    /// The material and sequence panels' edits of the frame, as steps of the history.
    void TrackPanelEdits();
    /// The steps of the history, to go back to one.
    void DrawHistory();
    [[nodiscard]] bool TabDirty(const Tab& tab) const;
    /// Moves the shown document's state into a tab, or a tab's into the editor.
    void StashTab(Tab& tab);
    void RestoreTab(Tab& tab);
    void SwitchTab(std::size_t index);
    /// Closes a tab, unsaved changes already asked about; the last one stays.
    void CloseTab(std::size_t index);
    void DrawTabs();
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

    // Organising (ADR-0107).
    /// Brings the scene from what showed of one level to what shows of the current one.
    void SyncShown(const daidalos::scene::Level& before, const std::set<std::uint64_t>& isolatedBefore);
    /// Hides or shows entities in the editor's view, as one step of the history.
    void SetHidden(const std::vector<std::uint64_t>& ids, bool hidden);
    void ShowEverything();
    /// Shows only the selection, or everything again.
    void ToggleIsolate();
    void SetLocked(const std::vector<std::uint64_t>& ids, bool locked);
    /// Moves entities to the root, where they stay in the world, into a folder of the hierarchy.
    void MoveToFolder(const std::vector<std::uint64_t>& ids, const std::string& folder);
    /// Lines the selection up on an axis, or spreads it evenly along it.
    void AlignSelection(int axis, AlignTo to);
    void SpreadSelection(int axis);
    /// Moves entities by world offsets, each where it hangs.
    void MoveBy(const std::vector<std::pair<std::uint64_t, glm::vec3>>& offsets, std::string_view label);
    /// The world positions of the selection's roots, in their order.
    [[nodiscard]] std::vector<glm::vec3> RootPositions(const std::vector<std::uint64_t>& roots);
    /// The box around the selection's meshes, or around their positions.
    [[nodiscard]] Bounds SelectionBounds();
    void DrawFolder(const std::string& folder, const std::vector<std::string>& folders,
                    const std::map<std::string, std::vector<std::uint64_t>>& byFolder,
                    const std::vector<std::vector<std::uint64_t>>& children, const std::vector<std::size_t>& indexOf);
    /// The hide and lock buttons at the end of a hierarchy row; returns which was clicked.
    void RowToggles(const std::string& key, bool hidden, bool locked, bool& toggleHidden, bool& toggleLocked);
    /// The measuring tool (M): drag between two surfaces to see the distance.
    void Measure(daidalos::app::Application& app);
    void DrawMeasure();

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

    // Lighting of the viewport and the renderer's debug views (ADR-0105).
    void SetLighting(Lighting lighting);
    /// Shows a renderer.debug_view by name, "off" for the lit scene.
    void SetDebugView(std::string_view name);
    [[nodiscard]] std::string DebugViewName() const;
    void DrawLightingMenu();
    /// The editor's light: the level's lights off, one soft light at the camera.
    void LightFromCamera(daidalos::renderer::SceneView& view) const;

    // Views and panes (ADR-0108).
    struct Pane {
        ViewKind kind = ViewKind::Perspective;
        OrthoView ortho;
        Rect rect;
        /// The last image it drew while it was the live pane, and the depth then, shown while another is live.
        daidalos::rhi::TextureHandle snapshot;
        std::optional<daidalos::renderer::DepthPicture> depth;
    };
    /// A document open in a tab that is not shown: everything the editor holds of it while it is.
    struct Tab {
        std::string levelPath;
        daidalos::scene::Level level;
        std::unique_ptr<daidalos::scene::LevelInstance> instance;
        std::unique_ptr<daidalos::scene::Scene> scene;
        daidalos::ecs::Entity cameraEntity;
        EditorCamera camera;
        History history;
        History::Revision savedRevision = 0;
        History::Revision autosavedRevision = 0;
        std::vector<std::uint64_t> selection;
        std::uint64_t selected = 0;
        std::set<std::uint64_t> isolated;
        std::map<MaterialKey, daidalos::resources::MaterialData> materialValues;
        std::map<MaterialKey, daidalos::resources::MaterialData> materialFirst;
        std::array<Pane, 4> panes;
    };
    /// The camera of the pane being drawn or used.
    [[nodiscard]] EditorCamera Eye() const;
    /// Where distances from the view are measured: the camera, or an axis view's focus.
    [[nodiscard]] glm::vec3 Focus() const;
    /// Where something created goes: a few meters in front of the camera, or at an axis view's focus.
    [[nodiscard]] glm::vec3 InFront() const;
    /// Points the overlays, picking and the gizmo at a pane.
    void UsePane(std::size_t pane);
    void LayOutPanes();
    void SetViewKind(ViewKind kind);
    void SetSplit(bool split);
    /// Makes a pane clicked in the split layout the live one, once the live one is captured; true when the click
    /// was taken for that.
    bool UpdatePanes(daidalos::app::Application& app);
    void TakeSnapshot(const daidalos::renderer::CapturedFrame& frame, std::size_t pane);
    /// A pane's last image if it is not live, its name and its border.
    void DrawPaneFrame(std::size_t pane);
    /// The grid of an axis view, in the plane through its focus.
    void DrawAxisGrid();
    /// Puts each selected entity on what is under it (End).
    void DropToFloor();

    // What the view shows (ADR-0106).
    void DrawShowMenu();
    /// A line of the world over the view, in pieces, each faint where the scene hides it.
    void Line3D(ImDrawList* draw, const glm::vec3& from, const glm::vec3& to, unsigned color, float thickness) const;
    /// The twelve edges of a box from min to max placed by a matrix.
    void Box3D(ImDrawList* draw, const glm::mat4& matrix, const glm::vec3& min, const glm::vec3& max, unsigned color,
               float thickness) const;
    /// A light's reach: three circles for a point light, a cone for a spot light.
    void DrawReach(ImDrawList* draw, const daidalos::scene::LevelEntity& entity, const glm::mat4& matrix,
                   unsigned color) const;
    /// Collision boxes, the navigation mesh and the openings between rooms, as the Show menu asks.
    void DrawShownExtras();
    /// Builds the navigation mesh from the triangles of the level's models, as the game does from its own.
    void BuildNavigation();
    /// Leaves out of the frame what the Show menu hides: decals, particles, fog.
    void FilterView(daidalos::renderer::SceneView& view) const;

    // Simulating and playing in the editor (ADR-0110).
    /// Physics, effects and triggers run in the view, the camera standing in for the player; edits wait.
    void StartSimulating();
    /// Everything back as the level has it.
    void StopSimulating();
    /// Stops simulating and keeps where physics left the loose objects, as one step (K).
    void KeepSimulation();
    /// The game plays the level, as it is, in the editor's window, from its start or the camera.
    void StartPlaying(daidalos::app::Application& app, bool fromCamera);
    void StopPlaying(daidalos::app::Application& app);
    /// Runs the game's frame while it plays; false when nothing plays.
    bool UpdateGame(daidalos::app::Application& app, const daidalos::core::FrameTime& time);

    // Checking and measuring the level (ADR-0111).
    void RunMapCheck();
    void DrawMapCheck();
    void DrawStatistics(daidalos::app::Application& app);

    // Commands, keys, search and context menus (ADR-0112).
    /// Every command, with its default keys, then the keymap the player saved.
    void RegisterCommands();
    void LoadKeymap();
    void SaveKeymap();
    /// The keys of a command as menus show them, or null.
    [[nodiscard]] const char* Keys(std::string_view id);
    /// Runs a command by id, if it can run.
    void Run(std::string_view id);
    [[nodiscard]] bool ChordPressed(const KeyChord& chord, bool repeats) const;
    void OpenPalette();
    void DrawPalette(daidalos::app::Application& app);
    /// What can be done to the selection: the view's and the hierarchy's right-click menus.
    void DrawEntityMenu();
    /// A right click in the view that does not move: the selection's menu, and creating there.
    void ViewportContextMenu(daidalos::app::Application& app);
    /// The Create menu's items, which create in front of the camera, or at m_createAt.
    void DrawCreateItems();

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

    std::unique_ptr<Loading> m_loading;
    /// The documents open, one tab each; the shown one's state is in the editor, the others' in their tab.
    std::vector<std::unique_ptr<Tab>> m_tabs;
    std::size_t m_activeTab = 0;
    /// The tab bar follows a tab chosen other than by a click on it.
    bool m_selectTab = false;
    std::optional<std::size_t> m_pendingCloseTab;
    std::size_t m_closingTab = 0;
    /// The row of tabs over the panes.
    Rect m_tabBar;
    /// The values of the materials edited since the level opened, and what each had before its first edit.
    std::map<MaterialKey, daidalos::resources::MaterialData> m_materialValues;
    std::map<MaterialKey, daidalos::resources::MaterialData> m_materialFirst;
    /// The sequence last seen in its panel, which an edit is compared with.
    std::string m_sequencePath;
    daidalos::scene::Sequence m_sequenceShadow;
    /// The Open dialog opens the level in a new tab.
    bool m_openInNewTab = false;
    Lighting m_lighting = Lighting::Lit;
    ShowFlags m_show;
    std::array<Pane, 4> m_panes{Pane{.kind = ViewKind::Perspective}, Pane{.kind = ViewKind::Top},
                                Pane{.kind = ViewKind::Front}, Pane{.kind = ViewKind::Side}};
    /// Four panes, or the live one alone.
    bool m_split = false;
    std::size_t m_active = 0;
    std::size_t m_drawPane = 0;
    std::optional<std::size_t> m_switchTo;
    std::optional<std::size_t> m_captureFor;
    /// The next frame does not follow from the last: nothing is carried over from it.
    bool m_cutView = false;
    /// The dockspace's central node, which the panes share.
    Rect m_central;
    /// While moving, the selection goes onto the surface under the mouse, turned to it if asked (ADR-0108).
    bool m_surfaceSnap = false;
    bool m_alignToSurface = false;
    /// What the view shows alone while isolated (Shift+H); empty for everything.
    std::set<std::uint64_t> m_isolated;
    Pivot m_pivot = Pivot::Origin;
    bool m_measuring = false;
    bool m_measureHeld = false;
    std::optional<glm::vec3> m_measureFrom;
    std::optional<glm::vec3> m_measureTo;
    /// Whether the scene's time runs in the view: off, effects, flickers and animations hold still (Ctrl+R).
    bool m_realtime = true;
    /// What hides what in the view, from the depth the renderer read back; refreshed each frame.
    DepthTest m_depth;
    std::optional<daidalos::navigation::NavMesh> m_navigation;
    std::vector<glm::vec3> m_navigationEdges;

    std::vector<Command> m_commands;
    Keymap m_keymap;
    /// Dear ImGui's keys by their names, as chords name them.
    std::map<std::string, int> m_keysByName;
    std::map<std::string, std::string> m_chordTexts;
    /// The command whose keys the Shortcuts window waits for.
    std::optional<std::string> m_capturing;
    std::string m_keyFilter;
    bool m_paletteOpen = false;
    /// The palette's field takes the keyboard on the frame it opens.
    bool m_paletteFocus = false;
    std::string m_paletteQuery;
    std::size_t m_paletteChoice = 0;
    std::optional<std::string> m_paletteSetting;
    std::vector<AssetEntry> m_paletteAssets;
    std::chrono::steady_clock::time_point m_paletteAssetsAt{};
    /// A right press in the view, how far the mouse went and for how long while held.
    std::optional<glm::vec2> m_rightPress;
    float m_rightTravel = 0.0f;
    float m_rightSeconds = 0.0f;
    /// Where the right click hit, to create or paste at.
    glm::vec3 m_contextPoint{0.0f};
    std::optional<glm::vec3> m_createAt;

    std::vector<Problem> m_problems;
    bool m_showNotes = false;
    /// The level and its state the problems were found in.
    History::Revision m_checkedRevision = 0;
    std::string m_checkedLevel;

    bool m_simulating = false;
    /// Frames the scene runs while paused, one per F10.
    int m_stepFrames = 0;
    std::unique_ptr<sandbox::Game> m_game;
    /// The editor's scene while the game plays its own.
    std::unique_ptr<daidalos::scene::Scene> m_stashedScene;
    bool m_stopGame = false;
    std::filesystem::path m_gameFile;

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
    /// The gizmo's frame when the drag began and now, and where each moved entity was then (ADR-0107).
    glm::mat4 m_gizmoStart{1.0f};
    glm::mat4 m_gizmoMatrix{1.0f};
    std::vector<std::pair<std::uint64_t, glm::mat4>> m_dragStart;
    bool m_local = false;
    bool m_gizmoUsing = false;
    /// The selection's rotation in degrees, kept while it is edited, so that angles do not jump between Euler
    /// solutions as they are dragged.
    std::uint64_t m_eulerOf = 0;
    glm::vec3 m_euler{0.0f};
};

} // namespace editor
