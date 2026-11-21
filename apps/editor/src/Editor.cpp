// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "Editor.hpp"

#include <daidalos/debugui/DebugUi.hpp>
#include <daidalos/ecs/Transform.hpp>
#include <daidalos/logging/Log.hpp>
#include <daidalos/math/Axes.hpp>
#include <daidalos/math/Transform.hpp>
#include <daidalos/platform/Platform.hpp>
#include <daidalos/scene/Components.hpp>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/matrix.hpp>
#include <glm/trigonometric.hpp>

#include <imgui.h>
#include <imgui_internal.h>

// After imgui.h, which it needs.
#include <ImGuizmo.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <utility>
#include <vector>

#include "Clipboard.hpp"
#include "Cues.hpp"
#include "Inspector.hpp"

using namespace daidalos;

namespace editor {

namespace {

constexpr std::string_view kModule = "editor";
constexpr float kMarkerRadius = 7.0f;  // Interface pixels.
constexpr float kPickDistance = 12.0f; // How close a click must be to a marker.
constexpr float kLookSpeed = 0.0035f;  // Radians per pixel of mouse motion.
constexpr float kMarkerReach = 60.0f;  // Meters beyond which markers of unselected entities are hidden.
constexpr std::string_view kLevelExtension = ".level.json";
constexpr std::string_view kPrefabExtension = ".prefab.json";

// Finds "assets" in the working directory, then beside the executable and in the directories above it.
std::filesystem::path FindAssetRoot() {
    constexpr const char* kName = "assets";
    std::error_code error;
    if (std::filesystem::is_directory(kName, error)) {
        return kName;
    }
    std::filesystem::path directory = platform::ExecutableDirectory();
    while (!directory.empty()) {
        std::filesystem::path candidate = directory / kName;
        if (std::filesystem::is_directory(candidate, error)) {
            return candidate;
        }
        if (directory == directory.parent_path()) {
            break;
        }
        directory = directory.parent_path();
    }
    return kName;
}

/// Translation, rotation and scale of a matrix without shear.
math::Transform Decompose(const glm::mat4& matrix) {
    math::Transform transform;
    transform.translation = glm::vec3(matrix[3]);
    glm::vec3 axes[3] = {glm::vec3(matrix[0]), glm::vec3(matrix[1]), glm::vec3(matrix[2])};
    for (int axis = 0; axis < 3; ++axis) {
        transform.scale[axis] = glm::length(axes[axis]);
        if (transform.scale[axis] > 1e-8f) {
            axes[axis] /= transform.scale[axis];
        }
    }
    // An axis scaled to nothing has no direction: the rotation is then taken as none rather than made from a
    // zero column, which gives a quaternion that is not a number.
    if (!(transform.scale.x > 1e-8f && transform.scale.y > 1e-8f && transform.scale.z > 1e-8f)) {
        return transform;
    }
    // A mirrored matrix keeps a proper rotation and a negative scale.
    if (glm::dot(glm::cross(axes[0], axes[1]), axes[2]) < 0.0f) {
        transform.scale.x = -transform.scale.x;
        axes[0] = -axes[0];
    }
    transform.rotation = glm::normalize(glm::quat_cast(glm::mat3(axes[0], axes[1], axes[2])));
    return transform;
}

/// The shortest turn from one direction to another, both of length 1.
glm::quat TurnBetween(const glm::vec3& from, const glm::vec3& to) {
    // A surface without a normal (a triangle with no area) turns nothing.
    if (!std::isfinite(glm::dot(to, to)) || glm::dot(to, to) < 1e-12f) {
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }
    const float cosine = glm::dot(from, to);
    if (cosine < -0.9999f) {
        // Opposite: half a turn about any axis square to from.
        const glm::vec3 axis = glm::normalize(std::abs(from.x) < 0.9f ? glm::cross(from, glm::vec3(1.0f, 0.0f, 0.0f))
                                                                      : glm::cross(from, glm::vec3(0.0f, 1.0f, 0.0f)));
        return glm::angleAxis(glm::pi<float>(), axis);
    }
    const glm::vec3 axis = glm::cross(from, to);
    return glm::normalize(glm::quat(1.0f + cosine, axis.x, axis.y, axis.z));
}

/// The twelve edges of the cube from -0.5 to 0.5, as pairs of corners.
constexpr std::array<std::array<int, 2>, 12> kCubeEdges{
    {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}}};

glm::vec3 CubeCorner(int corner, const glm::vec3& min, const glm::vec3& max) {
    return {(corner & 1) != 0 ? max.x : min.x, (corner & 2) != 0 ? max.y : min.y, (corner & 4) != 0 ? max.z : min.z};
}

/// The level and prefab files under the asset root, as paths under it, sorted: what File > Open offers.
std::vector<std::string> FindLevelFiles(const std::filesystem::path& root) {
    std::vector<std::string> files;
    std::error_code error;
    for (auto entry = std::filesystem::recursive_directory_iterator(root, error);
         !error && entry != std::filesystem::recursive_directory_iterator(); entry.increment(error)) {
        const std::string name = entry->path().filename().generic_string();
        if (entry->is_regular_file(error) &&
            ((name.ends_with(kLevelExtension) && !name.ends_with(".play.level.json")) ||
             name.ends_with(kPrefabExtension))) {
            files.push_back(entry->path().lexically_relative(root).generic_string());
        }
    }
    std::ranges::sort(files);
    return files;
}

/// A level path under the asset root, as typed, with the extension added when missing: a level's, or a
/// prefab's when it names one.
std::string LevelPathOf(std::string path, std::string_view extension = kLevelExtension) {
    std::ranges::replace(path, '\\', '/');
    while (!path.empty() && (path.back() == ' ' || path.back() == '/')) {
        path.pop_back();
    }
    if (!path.empty() && !path.ends_with(extension) && !path.ends_with(kPrefabExtension)) {
        path += extension;
    }
    return path;
}

/// The sandbox, which plays levels: beside the editor once installed, else where the build put it.
std::filesystem::path SandboxExecutable() {
    const std::filesystem::path beside = platform::ExecutableDirectory() / platform::ExecutableFileName("sandbox");
#ifdef DAI_SANDBOX_PATH
    std::error_code error;
    if (!std::filesystem::exists(beside, error)) {
        return DAI_SANDBOX_PATH;
    }
#endif
    return beside;
}

std::string Lower(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

/// The first line of what the editor puts on the clipboard: the level the entities come from, whose directory
/// their paths are relative to.
constexpr std::string_view kClipboardTag = "daidalos-entities ";

std::string EntityLabel(const scene::LevelEntity& entity) {
    return entity.name.empty() ? std::format("entity {}", entity.id) : entity.name;
}

/// The renderer's debug views (renderer.debug_view), by name, with their label in the menu.
constexpr std::array<std::array<const char*, 2>, 15> kDebugViews{{
    {"light_count", "Lights reaching each pixel"},
    {"normals", "Normals"},
    {"indirect", "Indirect light"},
    {"radiance_cache", "Radiance cache"},
    {"reflections", "Reflections"},
    {"lights", "Lights chosen per pixel"},
    {"bloom", "Bloom"},
    {"fog", "Fog"},
    {"layers", "Clear coat and sheen"},
    {"decals", "Decals"},
    {"wetness", "Wetness"},
    {"motion", "Motion vectors"},
    {"rays", "Ray tracing instances"},
    {"path", "Path tracer (reference)"},
    {"base_color", "Base color"},
}};

} // namespace

core::Result<void> Editor::OnStart(app::Application& app) {
    m_app = &app;
    m_assetRoot = FindAssetRoot();
    // Overlays are drawn faint where the scene hides them (ADR-0106).
    app.Renderer().SetDepthProbe(true);
    const std::string shown =
        app.CVars()
            .Register<std::string>("editor.show", "",
                                   "Overlays turned on at start, separated by commas: light_reach, collision, "
                                   "navigation, portals, game_view; and no_grid, no_decals, no_particles, no_fog")
            .Get();
    ApplyShowFlags(m_show, shown);
    const std::string level =
        app.CVars()
            .Register<std::string>("editor.level", "Factory/Factory.level.json", "Level file to open, under assets/")
            .Get();
    const std::string sequence =
        app.CVars()
            .Register<std::string>("editor.sequence", "", "A .sequence.json file under assets/ to open in its panel")
            .Get();
    m_frameLimit = app.CVars()
                       .Register<std::int64_t>("editor.frames", 0,
                                               "Quit after this many frames, for the smoke test; 0 runs until closed")
                       .Get();
    const std::string effect =
        app.CVars()
            .Register<std::string>("editor.effect", "",
                                   "An .effect.json or .ribbon.json file under assets/ to open in the Effect panel")
            .Get();
    if (!effect.empty()) {
        m_pendingEffect = effect;
    }
    m_autosaveSeconds = static_cast<float>(
        app.CVars()
            .Register<double>("editor.autosave", 120.0,
                              "Seconds between autosaves of a changed level, beside the editor; 0 turns them off")
            .Get());

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    // The layout is the editor's, kept next to its executable.
    static const std::string layout = (platform::ExecutableDirectory() / "editor_layout.ini").generic_string();
    io.IniFilename = layout.c_str();
    ImGui::LoadIniSettingsFromDisk(layout.c_str());
    ApplyTheme(app);
    app.MainWindow().SetTextInput(true);
    if (app::DevTools* tools = app.Tools()) {
        tools->SetSummaryShown(false); // The status panel shows the same.
    }

    const bool resave = app.CVars()
                            .Register<bool>("editor.resave", false,
                                            "Open the level, save it and quit: rewrites a level file that a script "
                                            "wrote in the engine's own layout")
                            .Get();
    if (resave) {
        core::Result<scene::Level> read = scene::LoadLevelFile(m_assetRoot / std::filesystem::path(level));
        if (!read) {
            return tl::unexpected(read.error());
        }
        DAI_TRY(scene::SaveLevelFile(m_assetRoot / std::filesystem::path(level), *read));
        DAI_LOG_INFO(kModule, "Saved {} again", level);
        app.RequestQuit();
        return {};
    }
    m_tabs.push_back(std::make_unique<Tab>());
    RegisterCommands();
    m_browser = std::make_unique<AssetBrowser>(m_assetRoot, app.Renderer().Device(), app.Workers());
    LoadBookmarks();
    if (!sequence.empty()) {
        m_sequences.Open(m_assetRoot, sequence);
    }
    if (core::Result<void> opened = Open(app, level); !opened) {
        // An editor with no level still opens, on an empty one.
        DAI_LOG_WARNING(kModule, "{}", core::Format(opened.error()));
        m_status = core::Format(opened.error());
        m_level = {};
        m_levelPath = level;
    }
    return {};
}

core::Result<void> Editor::Open(app::Application& app, const std::string& levelPath, std::optional<EditorCamera> camera,
                                bool newTab) {
    core::Result<scene::Level> level = scene::LoadLevelFile(m_assetRoot / std::filesystem::path(levelPath));
    if (!level) {
        return tl::unexpected(level.error());
    }
    if (m_loading != nullptr) {
        m_loading->batch->Cancel();
    }
    // The editor is silent: effects and lights play, the game's sounds do not. The level open stays in the view,
    // and the new one replaces it once its models are in (ADR-0105).
    auto loading = std::make_unique<Loading>();
    loading->path = levelPath;
    loading->level = std::move(*level);
    // Its sounds are heard only when asked (ADR-0116): the scene starts with them paused.
    loading->scene = std::make_unique<scene::Scene>(app.Renderer(), m_assetRoot, app.Audio());
    loading->scene->Sounds().SetPaused(true);
    loading->camera = camera;
    loading->newTab = newTab && m_scene != nullptr;
    Loading* waiting = loading.get();
    m_loading = std::move(loading);
    m_loading->batch = scene::ModelBatch::Start(
        m_loading->scene->Assets(), app.Workers(), scene::LevelModels(m_loading->level, levelPath),
        [waiting](const scene::ModelBatch& /*batch*/) { waiting->ready = true; });
    m_status = std::format("Opening {}", levelPath);
    return {};
}

void Editor::FinishOpen(app::Application& app) {
    StopSimulating();
    std::unique_ptr<Loading> loading = std::move(m_loading);
    for (const core::Error& failure : loading->batch->Failures()) {
        DAI_LOG_WARNING(kModule, "{}", core::Format(failure));
    }
    const std::string levelPath = loading->path;
    scene::Scene& target = *loading->scene;
    if (loading->newTab) {
        // The document shown goes into its tab, with its scene alive, and the new one gets a tab of its own.
        m_history.Commit(Snapshot());
        Tab& shown = *m_tabs[m_activeTab];
        StashTab(shown);
        shown.scene = app.ExchangeScene(std::move(loading->scene));
        if (shown.scene != nullptr) {
            shown.scene->Sounds().SetPaused(true);
        }
        m_tabs.push_back(std::make_unique<Tab>());
        m_activeTab = m_tabs.size() - 1;
        m_selectTab = true;
        m_materialValues.clear();
        m_materialFirst.clear();
    } else {
        // The same tab's document is replaced: what its materials' undo knew goes with its models.
        m_materialValues.clear();
        m_materialFirst.clear();
    }
    m_instance = std::make_unique<scene::LevelInstance>(target, levelPath);
    m_isolated.clear();
    m_navigation.reset();
    m_instance->Build(ShownLevel(loading->level, m_isolated));
    m_cameraEntity = target.World().Create();
    target.World().Add<ecs::LocalTransform>(m_cameraEntity);
    target.World().Add<scene::Camera>(
        m_cameraEntity, scene::Camera{.verticalFov = m_camera.verticalFov, .nearPlane = m_camera.nearPlane});
    target.SetActiveCamera(m_cameraEntity);
    m_picker.Rebuild(target.World());

    if (loading->camera) {
        m_camera = *loading->camera;
    } else {
        for (const scene::LevelEntity& entity : loading->level.entities) {
            if (entity.game && entity.game->kind == "player") {
                m_camera.position = entity.transform.translation + glm::vec3(0.0f, 0.3f, 0.0f);
                m_camera.yaw = 0.0f;
                m_camera.pitch = -0.1f;
                break;
            }
        }
    }
    m_level = std::move(loading->level);
    m_levelPath = levelPath;
    m_decalImages.clear();
    m_stroking = false;
    m_materials.Forget();
    m_followedSelection = ~std::uint64_t{0};
    m_previewTime.reset();
    Select(0);
    m_history.Clear();
    m_savedRevision = m_history.Current();
    m_autosavedRevision = m_history.Current();
    m_sinceAutosave = 0.0f;
    m_scene = &target;
    if (loading->scene != nullptr) {
        app.SetScene(std::move(loading->scene));
    }
    m_cutView = true;
    if (m_browser != nullptr) {
        m_browser->Open(std::filesystem::path(levelPath).parent_path().generic_string());
    }
    m_status = std::format("Opened {}: {} entities in {:.1f} s", levelPath, m_level.entities.size(), loading->seconds);
    DAI_LOG_INFO(kModule, "{}", m_status);
    UpdateTitle(app);
    CheckAutosave();
}

void Editor::DrawLoading() {
    if (m_loading == nullptr) {
        return;
    }
    if (!ImGui::IsPopupOpen("Loading")) {
        ImGui::OpenPopup("Loading");
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(520.0f, 0.0f), ImGuiCond_Always);
    if (!ImGui::BeginPopupModal("Loading", nullptr,
                                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                                    ImGuiWindowFlags_NoTitleBar)) {
        return;
    }
    const scene::ModelBatch& batch = *m_loading->batch;
    ImGui::Text("Opening %s", m_loading->path.c_str());
    const std::string count = std::format("{} of {} models", batch.Loaded(), batch.Total());
    ImGui::ProgressBar(batch.Fraction(), ImVec2(-1.0f, 0.0f), count.c_str());
    const std::string latest = batch.Latest().empty() ? std::string("Reading the first model...") : batch.Latest();
    ImGui::TextDisabled("%s", latest.c_str());
    ImGui::TextDisabled("%.1f s", m_loading->seconds);
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        // The level open stays; the new one's models finish loading on the workers and go with its scene.
        m_loading->batch->Cancel();
        m_status = std::format("Did not open {}", m_loading->path);
        m_loading.reset();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

bool Editor::Save(const std::string& levelPath) {
    // Materials and the open sequence are files of their own, saved with the level (ADR-0079).
    if (!m_materials.SaveAll(m_assetRoot)) {
        m_status = m_materials.Status();
        return false;
    }
    if (m_sequences.Dirty() && !m_sequences.Save(m_assetRoot)) {
        m_status = m_sequences.Status();
        return false;
    }
    m_history.Commit(Snapshot());
    const std::string target = levelPath.empty() ? m_levelPath : levelPath;
    scene::Level level = m_level;
    scene::Relocate(level, m_levelPath, target);
    if (core::Result<void> saved = scene::SaveLevelFile(m_assetRoot / std::filesystem::path(target), level); !saved) {
        m_status = core::Format(saved.error());
        DAI_LOG_ERROR(kModule, "{}", m_status);
        return false;
    }
    std::error_code error;
    std::filesystem::remove(AutosaveFile(), error);
    if (target != m_levelPath) {
        // The copy is the document now. Its paths resolve from its own directory, so it opens again, seen from
        // where the camera is.
        if (core::Result<void> opened = Open(*m_app, target, m_camera); !opened) {
            m_status = core::Format(opened.error());
            return false;
        }
    }
    m_savedRevision = m_history.Current();
    m_status = std::format("Saved {}", m_levelPath);
    DAI_LOG_INFO(kModule, "{}", m_status);
    UpdateTitle(*m_app);
    return true;
}

void Editor::UpdateTitle(app::Application& app) {
    std::string title = std::format("Daidalos Editor - {}{}", m_levelPath, Dirty() ? " *" : "");
    if (title != m_shownTitle) {
        app.MainWindow().SetTitle(title);
        m_shownTitle = std::move(title);
    }
}

scene::LevelEntity* Editor::Selected() {
    return m_selected == 0 ? nullptr : m_level.Find(m_selected);
}

void Editor::Select(std::uint64_t id) {
    m_selected = id;
    m_selection.clear();
    if (id != 0) {
        m_selection.push_back(id);
    }
    m_eulerOf = 0;
    m_scrollToSelection = id != 0;
}

void Editor::ToggleSelect(std::uint64_t id) {
    if (id == 0) {
        return;
    }
    if (std::erase(m_selection, id) == 0) {
        m_selection.push_back(id);
    }
    m_selected = m_selection.empty() ? 0 : m_selection.back();
    m_eulerOf = 0;
    m_scrollToSelection = m_selected != 0;
}

bool Editor::IsSelected(std::uint64_t id) const {
    return std::ranges::find(m_selection, id) != m_selection.end();
}

std::vector<std::uint64_t> Editor::SelectionRoots() const {
    std::vector<std::uint64_t> roots;
    for (const std::uint64_t id : m_selection) {
        const scene::LevelEntity* entity = m_level.Find(id);
        if (entity == nullptr) {
            continue;
        }
        bool held = false;
        for (std::uint64_t parent = entity->parent; parent != 0 && !held;) {
            held = IsSelected(parent);
            const scene::LevelEntity* above = m_level.Find(parent);
            parent = above != nullptr ? above->parent : 0;
        }
        if (!held) {
            roots.push_back(id);
        }
    }
    return roots;
}
void Editor::Change(std::string_view label, const std::function<void(scene::Level&)>& edit) {
    if (m_simulating) {
        // The simulation's scene is not the level's: what would be edited is where physics put it (ADR-0110).
        m_status = "Stop simulating to edit (Alt+S, or K to keep where loose objects are)";
        return;
    }
    scene::Level edited = m_level;
    edit(edited);
    // A transform that is not a number (a scale dragged to nothing, a surface with no normal) would show nothing
    // and make the level file unreadable: the edit is refused, and the level stays as it was.
    if (const scene::LevelEntity* broken = scene::FindBrokenTransform(edited)) {
        m_status = std::format("{}: not done, it would break the transform of {}", label, broken->name);
        DAI_LOG_WARNING(kModule, "{} (id {})", m_status, broken->id);
        return;
    }
    m_history.Begin(Snapshot(), label);
    const scene::Level before = std::exchange(m_level, std::move(edited));
    SyncShown(before, m_isolated);
}

void Editor::SyncShown(const scene::Level& before, const std::set<std::uint64_t>& isolatedBefore) {
    // Hidden entities, and those left out while others are isolated, are not in the scene (ADR-0107).
    std::erase_if(m_isolated, [this](std::uint64_t id) { return m_level.Find(id) == nullptr; });
    if (m_instance != nullptr &&
        m_instance->Sync(ShownLevel(before, isolatedBefore), ShownLevel(m_level, m_isolated)).models) {
        m_picker.Rebuild(m_scene->World());
        m_navigation.reset();
    }
}

void Editor::Apply(const scene::LevelEntity& after, std::string_view label) {
    const scene::LevelEntity* before = m_level.Find(after.id);
    if (before == nullptr || *before == after) {
        return;
    }
    Change(label, [&after](scene::Level& level) { *level.Find(after.id) = after; });
}

void Editor::Undo() {
    m_history.Commit(Snapshot());
    const std::string label = m_history.UndoLabel();
    if (std::optional<Document> state = m_history.Undo(Snapshot())) {
        GoTo(std::move(*state));
        m_status = std::format("Undid: {}", label);
    }
}

void Editor::Redo() {
    m_history.Commit(Snapshot());
    const std::string label = m_history.RedoLabel();
    if (std::optional<Document> state = m_history.Redo(Snapshot())) {
        GoTo(std::move(*state));
        m_status = std::format("Redid: {}", label);
    }
}

void Editor::Create(std::string_view label, scene::LevelEntity entity, std::optional<glm::vec3> position) {
    m_history.Commit(Snapshot());
    entity.id = m_level.NextId();
    entity.parent = 0;
    if (entity.name.empty()) {
        entity.name = std::string(label);
    }
    entity.transform.translation = position.value_or(std::exchange(m_createAt, std::nullopt).value_or(InFront()));
    Change(std::format("Create {}", label), [&entity](scene::Level& level) { level.entities.push_back(entity); });
    m_history.Commit(Snapshot());
    Select(entity.id);
    m_status = std::format("Created {}", EntityLabel(entity));
}

std::string Editor::RelativeToLevel(const std::string& path) const {
    std::filesystem::path directory = std::filesystem::path(m_levelPath).parent_path();
    if (directory.empty()) {
        directory = ".";
    }
    return std::filesystem::path(path).lexically_relative(directory).generic_string();
}

void Editor::PlaceAsset(app::Application& app, const AssetEntry& asset, std::optional<glm::vec2> at) {
    if (m_loading != nullptr) {
        return;
    }
    const std::optional<glm::vec3> position = at ? std::optional<glm::vec3>(PlaceAt(*at)) : std::nullopt;
    const std::string name = std::filesystem::path(asset.name).stem().generic_string();
    scene::LevelEntity entity;
    switch (asset.kind) {
    case AssetKind::Level:
        Guard(app, Pending::Open, asset.path);
        return;
    case AssetKind::Sky: {
        const std::string sky = RelativeToLevel(asset.path);
        Change("Change the sky", [&sky](scene::Level& level) { level.environment.skyFaces = sky; });
        m_history.Commit(Snapshot());
        m_status = std::format("The sky is {}", asset.name);
        return;
    }
    case AssetKind::Model:
        entity.model = RelativeToLevel(asset.path);
        entity.name = name;
        break;
    case AssetKind::Effect:
        entity.particles = scene::LevelParticles{.effect = RelativeToLevel(asset.path)};
        entity.name = std::filesystem::path(name).stem().generic_string();
        break;
    case AssetKind::Ribbon:
        entity.ribbon = scene::LevelRibbon{.effect = RelativeToLevel(asset.path)};
        entity.name = std::filesystem::path(name).stem().generic_string();
        break;
    case AssetKind::Sound:
        entity.sound = scene::LevelSound{.clip = RelativeToLevel(asset.path)};
        entity.name = std::filesystem::path(name).stem().generic_string();
        if (!m_hear) {
            m_status = "Show > Hear the level plays it here";
        }
        break;
    case AssetKind::Sequence:
        m_sequences.Open(m_assetRoot, asset.path);
        m_status = m_sequences.Status();
        return;
    case AssetKind::Prefab:
        entity.prefab = RelativeToLevel(asset.path);
        entity.name = std::filesystem::path(name).stem().generic_string();
        break;
    case AssetKind::Decal: {
        // A decal is painted with: the brush takes it, and a drop on a surface stamps it there.
        m_brush.kind = PaintBrush::Kind::Decal;
        m_brush.texture = RelativeToLevel(asset.path);
        m_brush.normal.clear();
        if (const std::string normal = asset.path.substr(0, asset.path.size() - 4) + "_normal.png";
            std::filesystem::exists(m_assetRoot / std::filesystem::path(normal))) {
            m_brush.normal = RelativeToLevel(normal);
        }
        if (!at) {
            m_painting = true;
            m_status = std::format("Painting with {}: drag over surfaces in the view", asset.name);
            return;
        }
        if (const std::optional<PickHit> hit = SurfaceAt(*at)) {
            scene::LevelEntity decal = Stamp(m_brush, hit->position, hit->normal, 0.0f);
            decal.name = name;
            // Create keeps the stamp's rotation and size, facing the surface.
            const glm::vec3 onSurface = decal.transform.translation;
            Create(name, std::move(decal), onSurface);
        }
        return;
    }
    case AssetKind::Folder:
    case AssetKind::Other:
        return;
    }
    Create(entity.name, std::move(entity), position);
}

void Editor::OnEvent(app::Application& app, const platform::Event& event) {
    if (m_game != nullptr) {
        m_game->OnEvent(app, event);
        return;
    }
    const auto* dropped = std::get_if<platform::FileDropped>(&event);
    if (dropped == nullptr || m_browser == nullptr) {
        return;
    }
    core::Result<AssetEntry> imported =
        m_browser->Import(std::filesystem::path(std::u8string(dropped->path.begin(), dropped->path.end())));
    if (!imported) {
        m_status = core::Format(imported.error());
        DAI_LOG_WARNING(kModule, "{}", m_status);
        return;
    }
    const glm::vec2 at = dropped->position;
    const bool inView =
        at.x >= m_viewportMin.x && at.y >= m_viewportMin.y && at.x < m_viewportMax.x && at.y < m_viewportMax.y;
    if (inView) {
        PlaceAsset(app, *imported, at);
    } else {
        m_status = std::format("Imported {}", imported->path);
    }
}

void Editor::DeleteSelected() {
    if (m_selection.empty()) {
        return;
    }
    // The entities and everything under them.
    std::set<std::uint64_t> doomed(m_selection.begin(), m_selection.end());
    for (bool grew = true; grew;) {
        grew = false;
        for (const scene::LevelEntity& entity : m_level.entities) {
            if (doomed.contains(entity.parent) && doomed.insert(entity.id).second) {
                grew = true;
            }
        }
    }
    const scene::LevelEntity* selected = Selected();
    const std::string label = m_selection.size() == 1 && selected != nullptr
                                  ? std::format("Delete {}", EntityLabel(*selected))
                                  : std::format("Delete {} entities", m_selection.size());
    m_history.Commit(Snapshot());
    Change(label, [&doomed](scene::Level& level) {
        std::erase_if(level.entities,
                      [&doomed](const scene::LevelEntity& entity) { return doomed.contains(entity.id); });
    });
    m_history.Commit(Snapshot());
    m_status = std::format("{}: {} entities gone", label, doomed.size());
    Select(0);
}

void Editor::Duplicate() {
    if (m_selection.empty()) {
        return;
    }
    const std::vector<scene::LevelEntity> copies = CopyEntities(m_level, SelectionRoots());
    std::vector<std::uint64_t> roots;
    m_history.Commit(Snapshot());
    Change("Duplicate", [&](scene::Level& level) { roots = PasteEntities(level, copies); });
    m_history.Commit(Snapshot());
    Select(0);
    for (const std::uint64_t id : roots) {
        ToggleSelect(id);
    }
    m_status = std::format("Duplicated {} entities", copies.size());
}

void Editor::CopySelection() {
    if (m_selection.empty()) {
        return;
    }
    scene::Level clip;
    clip.entities = CopyEntities(m_level, SelectionRoots());
    const std::string text = std::string(kClipboardTag) + m_levelPath + "\n" + scene::WriteLevel(clip);
    ImGui::SetClipboardText(text.c_str());
    m_status = std::format("Copied {} entities", clip.entities.size());
}

void Editor::Paste() {
    const char* clipboard = ImGui::GetClipboardText();
    const std::string_view text = clipboard != nullptr ? std::string_view(clipboard) : std::string_view();
    const std::size_t line = text.find('\n');
    if (!text.starts_with(kClipboardTag) || line == std::string_view::npos) {
        m_status = "The clipboard holds no entities";
        return;
    }
    const std::string from(text.substr(kClipboardTag.size(), line - kClipboardTag.size()));
    core::Result<scene::Level> clip = scene::ParseLevel(text.substr(line + 1));
    if (!clip) {
        m_status = core::Format(clip.error());
        return;
    }
    // Copied from another level: the paths are made relative to this one's directory.
    scene::Relocate(*clip, from, m_levelPath);
    std::vector<std::uint64_t> roots;
    m_history.Commit(Snapshot());
    Change("Paste", [&](scene::Level& level) { roots = PasteEntities(level, std::move(clip->entities)); });
    m_history.Commit(Snapshot());
    Select(0);
    for (const std::uint64_t id : roots) {
        ToggleSelect(id);
    }
    m_status = std::format("Pasted {} entities", roots.size());
}

void Editor::SelectAll() {
    Select(0);
    for (const scene::LevelEntity& entity : m_level.entities) {
        m_selection.push_back(entity.id);
    }
    m_selected = m_selection.empty() ? 0 : m_selection.back();
}

void Editor::Reparent(std::uint64_t child, std::uint64_t parent) {
    const scene::LevelEntity* entity = m_level.Find(child);
    if (entity == nullptr || entity->parent == parent || (parent != 0 && IsAncestor(m_level, child, parent)) ||
        m_instance == nullptr) {
        return;
    }
    // Where it is in the world stays: its transform becomes relative to the new parent.
    ecs::World& world = m_scene->World();
    ecs::PropagateTransforms(world);
    const ecs::Entity created = m_instance->Find(child);
    const ecs::Entity holder = parent != 0 ? m_instance->Find(parent) : ecs::Entity{};
    scene::LevelEntity moved = *entity;
    moved.parent = parent;
    if (!created.IsNull()) {
        const glm::mat4 childWorld = world.Get<ecs::WorldTransform>(created).matrix;
        const glm::mat4 parentWorld = holder.IsNull() ? glm::mat4(1.0f) : world.Get<ecs::WorldTransform>(holder).matrix;
        moved.transform = Decompose(glm::inverse(parentWorld) * childWorld);
    }
    const scene::LevelEntity* target = m_level.Find(parent);
    m_history.Commit(Snapshot());
    Apply(moved, std::format("Move {} under {}", EntityLabel(*entity),
                             target != nullptr ? EntityLabel(*target) : std::string("the root")));
    m_history.Commit(Snapshot());
}

void Editor::SetHidden(const std::vector<std::uint64_t>& ids, bool hidden) {
    if (ids.empty()) {
        return;
    }
    m_history.Commit(Snapshot());
    Change(std::format("{} {} entities", hidden ? "Hide" : "Show", ids.size()), [&](scene::Level& level) {
        for (const std::uint64_t id : ids) {
            if (scene::LevelEntity* entity = level.Find(id)) {
                entity->editing.hidden = hidden;
            }
        }
    });
    m_history.Commit(Snapshot());
    if (hidden) {
        std::erase_if(m_selection, [&ids](std::uint64_t id) { return std::ranges::find(ids, id) != ids.end(); });
        m_selected = m_selection.empty() ? 0 : m_selection.back();
        m_status = std::format("Hid {} entities: Alt+H shows everything", ids.size());
    }
}

void Editor::ShowEverything() {
    std::vector<std::uint64_t> hidden;
    for (const scene::LevelEntity& entity : m_level.entities) {
        if (entity.editing.hidden) {
            hidden.push_back(entity.id);
        }
    }
    const std::set<std::uint64_t> isolatedBefore = std::exchange(m_isolated, {});
    if (!isolatedBefore.empty()) {
        SyncShown(m_level, isolatedBefore);
    }
    SetHidden(hidden, false);
    m_status = "Everything shows";
}

void Editor::ToggleIsolate() {
    const std::set<std::uint64_t> before = m_isolated;
    if (m_isolated.empty()) {
        const std::vector<std::uint64_t> roots = SelectionRoots();
        m_isolated.insert(roots.begin(), roots.end());
        m_status = std::format("Showing {} entities alone: Shift+H shows the rest again", roots.size());
    } else {
        m_isolated.clear();
        m_status = "Showing the whole level again";
    }
    SyncShown(m_level, before);
}

void Editor::SetLocked(const std::vector<std::uint64_t>& ids, bool locked) {
    if (ids.empty()) {
        return;
    }
    m_history.Commit(Snapshot());
    Change(std::format("{} {} entities", locked ? "Lock" : "Unlock", ids.size()), [&](scene::Level& level) {
        for (const std::uint64_t id : ids) {
            if (scene::LevelEntity* entity = level.Find(id)) {
                entity->editing.locked = locked;
            }
        }
    });
    m_history.Commit(Snapshot());
    m_status = locked ? std::format("Locked {} entities: clicks in the view go through them", ids.size())
                      : std::format("Unlocked {} entities", ids.size());
}

void Editor::MoveToFolder(const std::vector<std::uint64_t>& ids, const std::string& folder) {
    if (ids.empty() || m_instance == nullptr) {
        return;
    }
    // Folders hold entities at the root: one under another leaves it, where it stands in the world.
    ecs::World& world = m_scene->World();
    ecs::PropagateTransforms(world);
    std::vector<std::pair<std::uint64_t, math::Transform>> placed;
    for (const std::uint64_t id : ids) {
        const scene::LevelEntity* entity = m_level.Find(id);
        if (entity == nullptr) {
            continue;
        }
        math::Transform transform = entity->transform;
        if (entity->parent != 0) {
            const ecs::Entity created = m_instance->Find(id);
            if (!created.IsNull()) {
                transform = Decompose(world.Get<ecs::WorldTransform>(created).matrix);
            }
        }
        placed.emplace_back(id, transform);
    }
    m_history.Commit(Snapshot());
    Change(folder.empty() ? std::string("Take out of folders") : std::format("Put in {}", folder),
           [&](scene::Level& level) {
               for (const auto& [id, transform] : placed) {
                   if (scene::LevelEntity* entity = level.Find(id)) {
                       entity->parent = 0;
                       entity->transform = transform;
                       entity->editing.folder = folder;
                   }
               }
           });
    m_history.Commit(Snapshot());
    m_status = folder.empty() ? std::format("{} entities out of folders", placed.size())
                              : std::format("{} entities in {}", placed.size(), folder);
}

std::vector<glm::vec3> Editor::RootPositions(const std::vector<std::uint64_t>& roots) {
    std::vector<glm::vec3> positions;
    ecs::World& world = m_scene->World();
    ecs::PropagateTransforms(world);
    for (const std::uint64_t id : roots) {
        const ecs::Entity created = m_instance->Find(id);
        positions.push_back(created.IsNull() ? glm::vec3(0.0f)
                                             : glm::vec3(world.Get<ecs::WorldTransform>(created).matrix[3]));
    }
    return positions;
}

void Editor::MoveBy(const std::vector<std::pair<std::uint64_t, glm::vec3>>& offsets, std::string_view label) {
    ecs::World& world = m_scene->World();
    std::vector<std::pair<std::uint64_t, glm::vec3>> local;
    for (const auto& [id, offset] : offsets) {
        const ecs::Entity created = m_instance->Find(id);
        if (created.IsNull()) {
            continue;
        }
        // A world offset, in the space of what holds the entity.
        const ecs::Entity parent = world.Parent(created);
        const glm::mat4 parentWorld = parent.IsNull() ? glm::mat4(1.0f) : world.Get<ecs::WorldTransform>(parent).matrix;
        local.emplace_back(id, glm::vec3(glm::inverse(parentWorld) * glm::vec4(offset, 0.0f)));
    }
    m_history.Commit(Snapshot());
    Change(label, [&](scene::Level& level) {
        for (const auto& [id, offset] : local) {
            if (scene::LevelEntity* entity = level.Find(id)) {
                entity->transform.translation += offset;
            }
        }
    });
    m_history.Commit(Snapshot());
}

void Editor::AlignSelection(int axis, AlignTo to) {
    if (m_instance == nullptr) {
        return;
    }
    std::vector<std::uint64_t> roots = SelectionRoots();
    std::erase_if(roots, [this](std::uint64_t id) { return LockedInEditor(m_level, id); });
    const std::vector<glm::vec3> before = RootPositions(roots);
    const std::vector<glm::vec3> after = Aligned(before, axis, to);
    std::vector<std::pair<std::uint64_t, glm::vec3>> offsets;
    for (std::size_t index = 0; index < roots.size(); ++index) {
        offsets.emplace_back(roots[index], after[index] - before[index]);
    }
    constexpr std::array<const char*, 3> kAxes{"X", "Y", "Z"};
    MoveBy(offsets, std::format("Align {} entities on {}", roots.size(), kAxes[static_cast<std::size_t>(axis)]));
    m_status = std::format("Aligned {} entities on {}", roots.size(), kAxes[static_cast<std::size_t>(axis)]);
}

void Editor::SpreadSelection(int axis) {
    if (m_instance == nullptr) {
        return;
    }
    std::vector<std::uint64_t> roots = SelectionRoots();
    std::erase_if(roots, [this](std::uint64_t id) { return LockedInEditor(m_level, id); });
    const std::vector<glm::vec3> before = RootPositions(roots);
    const std::vector<glm::vec3> after = Spread(before, axis);
    std::vector<std::pair<std::uint64_t, glm::vec3>> offsets;
    for (std::size_t index = 0; index < roots.size(); ++index) {
        offsets.emplace_back(roots[index], after[index] - before[index]);
    }
    constexpr std::array<const char*, 3> kAxes{"X", "Y", "Z"};
    MoveBy(offsets, std::format("Spread {} entities along {}", roots.size(), kAxes[static_cast<std::size_t>(axis)]));
    m_status = std::format("Spread {} entities along {}", roots.size(), kAxes[static_cast<std::size_t>(axis)]);
}

Bounds Editor::SelectionBounds() {
    Bounds bounds;
    if (m_instance == nullptr) {
        return bounds;
    }
    ecs::World& world = m_scene->World();
    ecs::PropagateTransforms(world);
    for (const std::uint64_t id : SelectionRoots()) {
        const ecs::Entity entity = m_instance->Find(id);
        if (entity.IsNull()) {
            continue;
        }
        const Bounds own = m_picker.WorldBounds(world, entity);
        if (own.Empty()) {
            bounds.Add(glm::vec3(world.Get<ecs::WorldTransform>(entity).matrix[3]));
        } else {
            bounds.Add(own);
        }
    }
    return bounds;
}

void Editor::Measure(app::Application& app) {
    const ImGuiIO& io = ImGui::GetIO();
    const glm::vec2 mouse(io.MousePos.x, io.MousePos.y);
    const auto pointAt = [this, &mouse]() -> glm::vec3 {
        const std::optional<PickHit> hit = SurfaceAt(mouse);
        glm::vec3 point = hit ? hit->position : PlaceAt(mouse);
        if (m_snap != ImGui::GetIO().KeyCtrl) {
            // Snapped as the gizmo snaps, to the move step.
            point = glm::round(point / m_snapSteps.x) * m_snapSteps.x;
        }
        return point;
    };
    if (m_measureHeld && !app.Input().IsDown(platform::MouseButton::Left)) {
        m_measureHeld = false;
        return;
    }
    if (m_measureHeld) {
        m_measureTo = pointAt();
        return;
    }
    if (app.Input().WasPressed(platform::MouseButton::Left) && MouseInViewport() && !io.WantCaptureMouse &&
        m_scene != nullptr) {
        m_measureHeld = true;
        m_measureFrom = pointAt();
        m_measureTo = m_measureFrom;
    }
}

void Editor::DrawMeasure() {
    if (!m_measureFrom || !m_measureTo) {
        return;
    }
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    draw->PushClipRect({m_viewportMin.x, m_viewportMin.y}, {m_viewportMax.x, m_viewportMax.y}, true);
    const glm::vec3 from = *m_measureFrom;
    const glm::vec3 to = *m_measureTo;
    const ImU32 color = IM_COL32(255, 230, 90, 255);
    Line3D(draw, from, to, color, 2.0f);
    // The three legs along the axes, faint.
    const glm::vec3 alongX(to.x, from.y, from.z);
    const glm::vec3 alongXZ(to.x, from.y, to.z);
    Line3D(draw, from, alongX, IM_COL32(255, 90, 90, 150), 1.0f);
    Line3D(draw, alongX, alongXZ, IM_COL32(90, 140, 255, 150), 1.0f);
    Line3D(draw, alongXZ, to, IM_COL32(90, 255, 90, 150), 1.0f);
    for (const glm::vec3& end : {from, to}) {
        if (const std::optional<glm::vec2> at = Project(end)) {
            draw->AddCircleFilled({at->x, at->y}, 4.0f, color);
        }
    }
    if (const std::optional<glm::vec2> middle = Project((from + to) * 0.5f)) {
        const glm::vec3 delta = to - from;
        const std::string label =
            std::format("{:.2f} m  (x {:.2f}, y {:.2f}, z {:.2f})", glm::length(delta), delta.x, delta.y, delta.z);
        const ImVec2 size = ImGui::CalcTextSize(label.c_str());
        const ImVec2 corner(middle->x - size.x * 0.5f, middle->y - size.y - 8.0f);
        draw->AddRectFilled({corner.x - 4.0f, corner.y - 2.0f}, {corner.x + size.x + 4.0f, corner.y + size.y + 2.0f},
                            IM_COL32(20, 20, 20, 210), 3.0f);
        draw->AddText(corner, color, label.c_str());
    }
    draw->PopClipRect();
}

bool Editor::MakePrefab(const std::string& path) {
    const std::vector<std::uint64_t> roots = SelectionRoots();
    if (roots.empty() || m_instance == nullptr) {
        return false;
    }
    if (path == m_levelPath) {
        m_status = "A prefab cannot replace the level it is made from";
        return false;
    }
    // The prefab's origin is the primary selection's place in the world; its roots keep where they are around it.
    ecs::World& world = m_scene->World();
    ecs::PropagateTransforms(world);
    const auto worldOf = [&](std::uint64_t id) {
        const ecs::Entity created = m_instance->Find(id);
        return created.IsNull() ? glm::mat4(1.0f) : world.Get<ecs::WorldTransform>(created).matrix;
    };
    const std::uint64_t primary = std::ranges::find(roots, m_selected) != roots.end() ? m_selected : roots.front();
    const glm::vec3 origin(worldOf(primary)[3]);
    scene::Level prefab = PrefabOf(m_level, m_selection, [&](const scene::LevelEntity& root) {
        return Decompose(glm::translate(glm::mat4(1.0f), -origin) * worldOf(root.id));
    });
    scene::Relocate(prefab, m_levelPath, path);
    const std::filesystem::path file = m_assetRoot / std::filesystem::path(path);
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    if (core::Result<void> saved = scene::SaveLevelFile(file, prefab); !saved) {
        m_status = core::Format(saved.error());
        return false;
    }

    // The selection becomes one entity placing the prefab, where the primary was, under its parent.
    const scene::LevelEntity* first = m_level.Find(primary);
    scene::LevelEntity placed;
    placed.id = m_level.NextId();
    placed.name = std::filesystem::path(path).filename().generic_string();
    placed.name = placed.name.substr(0, placed.name.size() - kPrefabExtension.size());
    placed.prefab = RelativeToLevel(path);
    placed.parent = first != nullptr ? first->parent : 0;
    const ecs::Entity holder = placed.parent != 0 ? m_instance->Find(placed.parent) : ecs::Entity{};
    const glm::mat4 parentWorld = holder.IsNull() ? glm::mat4(1.0f) : world.Get<ecs::WorldTransform>(holder).matrix;
    placed.transform = Decompose(glm::inverse(parentWorld) * glm::translate(glm::mat4(1.0f), origin));
    std::set<std::uint64_t> gone;
    for (const scene::LevelEntity& copy : CopyEntities(m_level, m_selection)) {
        gone.insert(copy.id);
    }
    m_history.Commit(Snapshot());
    Change(std::format("Make the prefab {}", placed.name), [&](scene::Level& level) {
        std::erase_if(level.entities, [&gone](const scene::LevelEntity& entity) { return gone.contains(entity.id); });
        level.entities.push_back(placed);
    });
    m_history.Commit(Snapshot());
    Select(placed.id);
    if (m_browser != nullptr) {
        m_browser->Open(m_browser->Folder());
    }
    m_status = std::format("Saved {} entities as {}", prefab.entities.size(), path);
    return true;
}

void Editor::UnpackPrefab(std::uint64_t id) {
    const scene::LevelEntity* entity = m_level.Find(id);
    if (entity == nullptr || entity->prefab.empty() || m_instance == nullptr) {
        return;
    }
    const std::string path = m_instance->Resolve(entity->prefab);
    core::Result<scene::Level> prefab = scene::LoadLevelFile(m_assetRoot / std::filesystem::path(path));
    if (!prefab) {
        m_status = core::Format(prefab.error());
        return;
    }
    // The copy's own changes are kept in what it leaves behind (ADR-0113).
    *prefab = scene::ApplyOverrides(*prefab, entity->overrides);
    scene::Relocate(*prefab, path, m_levelPath);
    const std::string label = EntityLabel(*entity);
    m_history.Commit(Snapshot());
    Change(std::format("Unpack {}", label), [&](scene::Level& level) { Unpack(level, id, std::move(*prefab)); });
    m_history.Commit(Snapshot());
    m_status = std::format("Unpacked {}: its entities are the level's now", label);
}

bool Editor::InspectPrefabParts(scene::LevelEntity& edited) {
    if (m_instance == nullptr || m_scene == nullptr) {
        return false;
    }
    const std::string path = m_instance->Resolve(edited.prefab);
    core::Result<std::shared_ptr<const scene::Level>> prefab = m_scene->Assets().LoadPrefab(path);
    if (!prefab) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", core::Format(prefab.error()).c_str());
        return false;
    }
    const scene::Level& parts = **prefab;
    bool changed = false;
    ImGui::Text("%zu parts, %zu changed in this copy", parts.entities.size(), edited.overrides.size());
    for (const scene::LevelEntity& part : parts.entities) {
        const auto found = std::ranges::find(edited.overrides, part.id, &scene::PrefabOverride::part);
        const bool overridden = found != edited.overrides.end();
        const bool removed = overridden && found->removed;
        if (m_openPart && part.id == m_selectedPart) {
            ImGui::SetNextItemOpen(true);
            ImGui::SetScrollHereY(0.3f);
        }
        const std::string label = std::format("{}{}{}###part{}", EntityLabel(part), overridden ? " *" : "",
                                              removed ? " (left out)" : "", part.id);
        if (overridden) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.35f, 1.0f));
        }
        const bool open = ImGui::TreeNode(label.c_str());
        if (overridden) {
            ImGui::PopStyleColor();
        }
        if (!open) {
            continue;
        }
        // The part as this copy has it, edited, then turned back into what differs from the prefab.
        scene::LevelEntity effective = part;
        if (overridden) {
            const scene::Level one = scene::ApplyOverrides(scene::Level{.entities = {part}}, {*found});
            if (!one.entities.empty()) {
                effective = one.entities.front();
            }
        }
        bool partChanged = false;
        bool leftOut = removed;
        partChanged |= ImGui::Checkbox("Left out of this copy", &leftOut);
        ImGui::BeginDisabled(leftOut);
        partChanged |= ImGui::DragFloat3("Position (m)", &effective.transform.translation.x, 0.01f);
        glm::vec3 degrees = glm::degrees(glm::eulerAngles(effective.transform.rotation));
        if (ImGui::DragFloat3("Rotation (deg)", &degrees.x, 0.25f)) {
            effective.transform.rotation = glm::normalize(glm::quat(glm::radians(degrees)));
            partChanged = true;
        }
        partChanged |= ImGui::DragFloat3("Scale", &effective.transform.scale.x, 0.01f);
        if (!effective.model.empty()) {
            partChanged |= InputString("Model", effective.model);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Relative to the prefab's file");
            }
        }
        if (effective.light) {
            // Only the light shows: the rest of the part stays the prefab's.
            scene::LevelEntity lit;
            lit.light = effective.light;
            if (InspectComponents(lit)) {
                effective.light = lit.light;
                partChanged = true;
            }
        }
        ImGui::EndDisabled();
        if (overridden && ImGui::SmallButton("Back to the prefab")) {
            edited.overrides.erase(found);
            changed = true;
            ImGui::TreePop();
            break;
        }
        if (partChanged) {
            std::optional<scene::PrefabOverride> change = scene::OverrideOf(part, effective);
            if (leftOut) {
                change = change.value_or(scene::PrefabOverride{.part = part.id});
                change->removed = true;
            }
            if (overridden) {
                edited.overrides.erase(found);
            }
            if (change) {
                edited.overrides.push_back(*change);
                std::ranges::sort(edited.overrides, {}, &scene::PrefabOverride::part);
            }
            changed = true;
            ImGui::TreePop();
            break;
        }
        ImGui::TreePop();
    }
    m_openPart = false;
    if (!edited.overrides.empty()) {
        if (ImGui::Button("Back to the prefab, all of it")) {
            edited.overrides.clear();
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Apply to the prefab")) {
            m_pendingApplyOverrides = edited.id;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Writes this copy's changes into %s, for every copy", path.c_str());
        }
    }
    return changed;
}

void Editor::ApplyOverridesToPrefab(std::uint64_t id) {
    const scene::LevelEntity* entity = m_level.Find(id);
    if (entity == nullptr || entity->prefab.empty() || entity->overrides.empty() || m_instance == nullptr) {
        return;
    }
    const std::string path = m_instance->Resolve(entity->prefab);
    const std::filesystem::path file = m_assetRoot / std::filesystem::path(path);
    core::Result<scene::Level> prefab = scene::LoadLevelFile(file);
    if (!prefab) {
        m_status = core::Format(prefab.error());
        return;
    }
    if (core::Result<void> saved = scene::SaveLevelFile(file, scene::ApplyOverrides(*prefab, entity->overrides));
        !saved) {
        m_status = core::Format(saved.error());
        return;
    }
    // The prefab has the changes now: this copy needs them no more, and every copy reads the file again.
    scene::LevelEntity cleared = *entity;
    cleared.overrides.clear();
    m_history.Commit(Snapshot());
    Apply(cleared, std::format("Apply {}'s changes to its prefab", EntityLabel(cleared)));
    m_history.Commit(Snapshot());
    ReloadPrefabs();
    m_status = std::format("Wrote the changes into {}: every copy shows them", path);
}

void Editor::ReloadPrefabs() {
    if (m_instance == nullptr) {
        return;
    }
    const std::size_t count = m_instance->RemakePrefabs(m_level);
    m_picker.Rebuild(m_scene->World());
    m_status = std::format("Read {} prefabs again", count);
}

void Editor::FindDecalImages() {
    m_decalImages.clear();
    const std::filesystem::path directory = std::filesystem::path(m_levelPath).parent_path() / "Decals";
    for (const AssetEntry& entry : ListFolder(m_assetRoot, directory.generic_string())) {
        if (entry.kind == AssetKind::Decal) {
            m_decalImages.push_back(RelativeToLevel(entry.path));
        }
    }
}

std::optional<PickHit> Editor::SurfaceAt(const glm::vec2& point) {
    if (m_scene == nullptr) {
        return std::nullopt;
    }
    const auto [origin, direction] = RayAt(point);
    ecs::PropagateTransforms(m_scene->World());
    std::optional<PickHit> hit = m_picker.Cast(m_scene->World(), origin, direction);
    if (hit && hit->distance > 200.0f) {
        return std::nullopt;
    }
    return hit;
}

void Editor::PlaceStamp(const glm::vec3& position, const glm::vec3& normal) {
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    if (m_brush.kind == PaintBrush::Kind::Props && m_brush.models.empty()) {
        m_status = "No props to scatter: select some, then Take the selection's models";
        return;
    }
    const float spin = m_brush.randomSpin ? unit(m_random) * glm::two_pi<float>() : 0.0f;
    const float scale = 1.0f + (unit(m_random) * 2.0f - 1.0f) * std::clamp(m_brush.sizeJitter, 0.0f, 0.9f);
    const auto pick =
        static_cast<std::size_t>(unit(m_random) * static_cast<float>(std::max<std::size_t>(m_brush.models.size(), 1)));
    scene::LevelEntity stamp = Stamp(m_brush, position, normal, spin, scale, pick);
    const std::string kind = m_brush.kind == PaintBrush::Kind::Wet ? std::string("wet")
                             : m_brush.kind == PaintBrush::Kind::Props
                                 ? std::string("props")
                                 : std::filesystem::path(m_brush.texture).stem().generic_string();
    const bool first = m_strokeEntity == 0;
    Change(std::format("Paint {}", kind), [&](scene::Level& level) {
        if (m_strokeEntity == 0) {
            // A stroke's stamps hang from an entity at its start, so that a stroke moves and goes as one.
            scene::LevelEntity stroke{.id = level.NextId(), .name = std::format("Painted {}", kind)};
            stroke.transform.translation = position;
            level.entities.push_back(stroke);
            m_strokeEntity = stroke.id;
        }
        stamp.id = level.NextId();
        stamp.parent = m_strokeEntity;
        stamp.name = std::format("{} {}", kind, m_strokeStamps + 1);
        stamp.transform.translation -= level.Find(m_strokeEntity)->transform.translation;
        level.entities.push_back(stamp);
    });
    if (first) {
        Select(m_strokeEntity);
    }
    ++m_strokeStamps;
    m_lastStamp = position;
    m_lastNormal = normal;
}

void Editor::PaintStrokes(app::Application& app) {
    const ImGuiIO& io = ImGui::GetIO();
    const glm::vec2 mouse(io.MousePos.x, io.MousePos.y);
    if (m_stroking && !app.Input().IsDown(platform::MouseButton::Left)) {
        m_stroking = false;
        m_history.Commit(Snapshot());
        m_status = std::format("Painted {} stamps", m_strokeStamps);
        return;
    }
    if (!m_stroking) {
        if (!app.Input().WasPressed(platform::MouseButton::Left) || !MouseInViewport() || io.WantCaptureMouse ||
            m_instance == nullptr) {
            return;
        }
        const std::optional<PickHit> hit = SurfaceAt(mouse);
        if (!hit) {
            return;
        }
        m_history.Commit(Snapshot());
        m_stroking = true;
        m_strokeEntity = 0;
        m_strokeStamps = 0;
        PlaceStamp(hit->position, hit->normal);
        return;
    }
    // While held: stamps every spacing along where the mouse went over surfaces.
    const std::optional<PickHit> hit = SurfaceAt(mouse);
    if (!hit) {
        return;
    }
    for (int stamps = 0; stamps < 16; ++stamps) {
        const std::optional<glm::vec3> next = NextStamp(m_lastStamp, hit->position, m_brush.spacing);
        if (!next) {
            break;
        }
        // Between stamps the surface is the one under the mouse: a stroke across a corner turns with it.
        PlaceStamp(*next, hit->normal);
    }
}

void Editor::DrawBrush() {
    if (!m_painting || !MouseInViewport() || m_flying) {
        return;
    }
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse && !m_stroking) {
        return;
    }
    const std::optional<PickHit> hit = SurfaceAt(glm::vec2(io.MousePos.x, io.MousePos.y));
    if (!hit) {
        return;
    }
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    draw->PushClipRect({m_viewportMin.x, m_viewportMin.y}, {m_viewportMax.x, m_viewportMax.y}, true);
    const glm::quat rotation = SurfaceRotation(hit->normal, 0.0f);
    const float radius = m_brush.size * 0.5f;
    const ImU32 color =
        m_brush.kind == PaintBrush::Kind::Wet ? IM_COL32(90, 170, 255, 230) : IM_COL32(255, 120, 90, 230);
    constexpr int kSegments = 40;
    std::optional<glm::vec2> previous;
    for (int segment = 0; segment <= kSegments; ++segment) {
        const float angle = glm::two_pi<float>() * static_cast<float>(segment) / static_cast<float>(kSegments);
        const glm::vec3 point =
            hit->position + hit->normal * 0.01f + rotation * glm::vec3(std::cos(angle), std::sin(angle), 0.0f) * radius;
        const std::optional<glm::vec2> at = Project(point);
        if (at && previous) {
            draw->AddLine({previous->x, previous->y}, {at->x, at->y}, color, 2.0f);
        }
        previous = at;
    }
    if (const std::optional<glm::vec2> center = Project(hit->position)) {
        if (const std::optional<glm::vec2> tip = Project(hit->position + hit->normal * radius)) {
            draw->AddLine({center->x, center->y}, {tip->x, tip->y}, color, 1.5f);
        }
    }
    draw->PopClipRect();
}

void Editor::DrawPaint() {
    if (!m_painting) {
        return;
    }
    // Small, at the view's top right, until moved or docked.
    ImGui::SetNextWindowPos(ImVec2(m_viewportMax.x - 12.0f, m_viewportMin.y + 12.0f), ImGuiCond_FirstUseEver,
                            ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(340.0f, 0.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Paint", &m_painting)) {
        ImGui::End();
        return;
    }
    ImGui::TextWrapped("Drag over surfaces in the view to paint. Each stroke is one undo step, under an entity "
                       "of its own. B or Esc leaves the tool.");
    int kind = static_cast<int>(m_brush.kind);
    ImGui::RadioButton("Decals", &kind, static_cast<int>(PaintBrush::Kind::Decal));
    ImGui::SameLine();
    ImGui::RadioButton("Wet patches", &kind, static_cast<int>(PaintBrush::Kind::Wet));
    ImGui::SameLine();
    ImGui::RadioButton("Props", &kind, static_cast<int>(PaintBrush::Kind::Props));
    m_brush.kind = static_cast<PaintBrush::Kind>(kind);
    ImGui::SliderFloat("Size (m)", &m_brush.size, 0.1f, 6.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("Spacing (m)", &m_brush.spacing, 0.05f, 6.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("Size jitter", &m_brush.sizeJitter, 0.0f, 0.9f);
    ImGui::Checkbox("Random turn", &m_brush.randomSpin);
    if (m_brush.kind == PaintBrush::Kind::Decal) {
        if (m_decalImages.empty()) {
            FindDecalImages();
        }
        if (ImGui::BeginCombo("Image", m_brush.texture.c_str())) {
            for (const std::string& image : m_decalImages) {
                if (ImGui::Selectable(image.c_str(), image == m_brush.texture)) {
                    m_brush.texture = image;
                    const std::string normal = image.substr(0, image.size() - 4) + "_normal.png";
                    m_brush.normal =
                        std::filesystem::exists(m_assetRoot / std::filesystem::path(m_instance->Resolve(normal)))
                            ? normal
                            : std::string{};
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SliderFloat("Opacity", &m_brush.opacity, 0.0f, 1.0f);
        ImGui::SliderFloat("Roughness", &m_brush.roughness, 0.045f, 1.0f);
        ImGui::TextDisabled("Images come from the level's Decals folder, or a double-click in the browser.");
    } else if (m_brush.kind == PaintBrush::Kind::Wet) {
        ImGui::SliderFloat("Wetness", &m_brush.wetness, 0.0f, 1.0f);
        ImGui::SliderFloat("Puddles", &m_brush.puddles, 0.0f, 1.0f);
    } else {
        // Scattering (ADR-0116): one of these models for each stamp, its size the jitter's.
        if (ImGui::Button("Take the selection's models")) {
            m_brush.models.clear();
            for (const std::uint64_t id : m_selection) {
                if (const scene::LevelEntity* entity = m_level.Find(id);
                    entity != nullptr && !entity->model.empty() &&
                    std::ranges::find(m_brush.models, entity->model) == m_brush.models.end()) {
                    m_brush.models.push_back(entity->model);
                }
            }
        }
        std::optional<std::size_t> gone;
        for (std::size_t index = 0; index < m_brush.models.size(); ++index) {
            ImGui::PushID(static_cast<int>(index));
            if (ImGui::SmallButton("x")) {
                gone = index;
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(m_brush.models[index].c_str());
            ImGui::PopID();
        }
        if (gone) {
            m_brush.models.erase(m_brush.models.begin() + static_cast<std::ptrdiff_t>(*gone));
        }
        if (m_brush.models.empty()) {
            ImGui::TextDisabled("Select props in the view, then take their models.");
        }
        ImGui::Checkbox("Turn to the surface", &m_brush.alignToSurface);
        ImGui::TextDisabled("Size jitter scales each prop; spacing is how far apart they fall.");
    }
    ImGui::End();
}

void Editor::FollowSelectionMaterials() {
    if (m_selected == m_followedSelection || m_instance == nullptr) {
        return;
    }
    m_followedSelection = m_selected;
    const ecs::Entity created = m_selected != 0 ? m_instance->Find(m_selected) : ecs::Entity{};
    const scene::ModelInstance* placed =
        created.IsNull() ? nullptr : m_scene->World().TryGet<scene::ModelInstance>(created);
    if (placed != nullptr && placed->model != nullptr) {
        m_materials.Show(placed->model, placed->model->path, std::nullopt);
    } else {
        m_materials.Clear();
    }
}

void Editor::ShowMaterialOf(ecs::Entity drawn) {
    const ecs::World& world = m_scene->World();
    const scene::MeshRenderer* renderer = world.TryGet<scene::MeshRenderer>(drawn);
    // The model is placed by the nearest entity above that has one: a level entity, or a prefab's part.
    for (ecs::Entity entity = drawn; renderer != nullptr && !entity.IsNull(); entity = world.Parent(entity)) {
        const scene::ModelInstance* placed = world.TryGet<scene::ModelInstance>(entity);
        if (placed == nullptr || placed->model == nullptr) {
            continue;
        }
        const scene::Model& model = *placed->model;
        for (std::uint32_t index = 0; index < model.materials.size(); ++index) {
            if (model.materials[index].handle == renderer->material) {
                m_materials.Show(&model, model.path, index);
                m_followedSelection = m_selected;
                return;
            }
        }
        return;
    }
}

void Editor::EditSequence(std::uint64_t id) {
    scene::LevelEntity* entity = m_level.Find(id);
    if (entity == nullptr || !entity->trigger || m_instance == nullptr) {
        return;
    }
    if (entity->trigger->sequence.empty()) {
        std::string name = entity->name.empty() ? std::format("trigger_{}", id) : Lower(entity->name);
        std::ranges::replace(name, ' ', '_');
        scene::LevelEntity named = *entity;
        named.trigger->sequence = std::format("Sequences/{}.sequence.json", name);
        m_history.Commit(Snapshot());
        Apply(named, std::format("Give {} a sequence", EntityLabel(named)));
        m_history.Commit(Snapshot());
        entity = m_level.Find(id);
    }
    if (m_sequences.IsOpen() && m_sequences.Dirty()) {
        m_status = std::format("Save or revert {} first", m_sequences.Path());
        return;
    }
    m_sequences.Open(m_assetRoot, m_instance->Resolve(entity->trigger->sequence));
    m_status = m_sequences.Status();
}

void Editor::RemakeScene() {
    if (m_instance == nullptr) {
        return;
    }
    const scene::Level shown = ShownLevel(m_level, m_isolated);
    m_instance->Sync(shown, scene::Level{});
    m_instance->Sync(scene::Level{}, shown);
    m_picker.Rebuild(m_scene->World());
    m_followedSelection = ~std::uint64_t{0};
    m_previewTime.reset();
    m_sequences.SetPlayhead(std::nullopt);
}

void Editor::UpdateSequences(float seconds) {
    if (m_previewTime) {
        *m_previewTime += seconds;
        if (*m_previewTime > m_sequences.Sequence().Length() + 1.0f) {
            m_previewTime.reset();
        }
        m_sequences.SetPlayhead(m_previewTime);
    }
    SequencePanel::Context context{.level = &m_level,
                                   .assetRoot = m_assetRoot,
                                   .cameraPosition = m_camera.position,
                                   .cameraForward = m_camera.Forward()};
    switch (m_sequences.Draw(context)) {
    case SequencePanel::Request::Play:
        if (m_scene != nullptr) {
            m_scene->PlaySequence(std::make_shared<const scene::Sequence>(m_sequences.Sequence()));
            m_previewTime = 0.0f;
            m_status = "Playing the sequence: \"Put the scene back\" undoes what it changed";
        }
        break;
    case SequencePanel::Request::Reset:
        RemakeScene();
        m_status = "Made the level's entities again";
        break;
    case SequencePanel::Request::None:
        break;
    }
}

void Editor::DrawSelectedCue() {
    const scene::Cue* cue = m_sequences.Selected();
    if (cue == nullptr || m_instance == nullptr) {
        return;
    }
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    draw->PushClipRect({m_viewportMin.x, m_viewportMin.y}, {m_viewportMax.x, m_viewportMax.y}, true);
    const auto line = [&](const glm::vec3& from, const glm::vec3& to, ImU32 color, float thickness) {
        const std::optional<glm::vec2> a = Project(from);
        const std::optional<glm::vec2> b = Project(to);
        if (a && b) {
            draw->AddLine({a->x, a->y}, {b->x, b->y}, color, thickness);
        }
    };
    const ImU32 color = CueColor(cue->kind);
    if (cue->kind == scene::Cue::Kind::Camera && !cue->keys.empty()) {
        // The path the camera takes, sampled as the scene moves it, and where each key looks.
        const int steps = std::clamp(static_cast<int>(cue->keys.size()) * 16, 64, 2048);
        const float length = cue->keys.back().at - cue->keys.front().at;
        for (int step = 0; step < steps; ++step) {
            const float a = cue->keys.front().at + length * static_cast<float>(step) / static_cast<float>(steps);
            const float b = cue->keys.front().at + length * static_cast<float>(step + 1) / static_cast<float>(steps);
            line(scene::CameraAt(cue->keys, a, cue->cameraPath).position,
                 scene::CameraAt(cue->keys, b, cue->cameraPath).position, color, 2.0f);
        }
        for (const scene::CameraKey& key : cue->keys) {
            line(key.position, key.target, IM_COL32(255, 255, 255, 140), 1.0f);
            if (const std::optional<glm::vec2> at = Project(key.position)) {
                draw->AddCircleFilled({at->x, at->y}, 5.0f, color);
            }
        }
    } else if (cue->kind == scene::Cue::Kind::Move) {
        for (const scene::LevelEntity& entity : m_level.entities) {
            if (entity.name == cue->name) {
                const ecs::Entity created = m_instance->Find(entity.id);
                if (!created.IsNull()) {
                    line(glm::vec3(m_scene->World().Get<ecs::WorldTransform>(created).matrix[3]), cue->position, color,
                         2.0f);
                }
            }
        }
        if (const std::optional<glm::vec2> at = Project(cue->position)) {
            draw->AddCircleFilled({at->x, at->y}, 5.0f, color);
        }
    }
    draw->PopClipRect();
}

void Editor::LoadBookmarks() {
    // "slot x y z yaw pitch level", one per line.
    std::ifstream file(platform::ExecutableDirectory() / "editor_bookmarks.txt");
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream fields(line);
        int slot = 0;
        EditorCamera camera;
        std::string level;
        if (fields >> slot >> camera.position.x >> camera.position.y >> camera.position.z >> camera.yaw >>
                camera.pitch >> std::ws &&
            std::getline(fields, level) && slot >= 1 && slot <= 9) {
            m_bookmarks[level][static_cast<std::size_t>(slot - 1)] = camera;
        }
    }
}

void Editor::SaveBookmarks() const {
    std::ofstream file(platform::ExecutableDirectory() / "editor_bookmarks.txt");
    for (const auto& [level, slots] : m_bookmarks) {
        for (std::size_t slot = 0; slot < slots.size(); ++slot) {
            if (const std::optional<EditorCamera>& camera = slots[slot]) {
                file << slot + 1 << ' ' << camera->position.x << ' ' << camera->position.y << ' ' << camera->position.z
                     << ' ' << camera->yaw << ' ' << camera->pitch << ' ' << level << '\n';
            }
        }
    }
}

void Editor::Bookmark(int slot, bool store) {
    std::optional<EditorCamera>& saved = m_bookmarks[m_levelPath][static_cast<std::size_t>(slot - 1)];
    if (store) {
        saved = m_camera;
        SaveBookmarks();
        m_status = std::format("Bookmark {} set: Ctrl+{} sets it again, {} goes there", slot, slot, slot);
    } else if (saved) {
        m_camera.position = saved->position;
        m_camera.yaw = saved->yaw;
        m_camera.pitch = saved->pitch;
        m_status = std::format("Bookmark {}", slot);
    } else {
        m_status = std::format("No bookmark {}: Ctrl+{} sets it", slot, slot);
    }
}
void Editor::Guard(app::Application& app, Pending what, std::string path) {
    m_history.Commit(Snapshot());
    m_pending = what;
    m_pendingPath = std::move(path);
    if (Dirty()) {
        m_openPopup = "Unsaved changes";
    } else {
        Proceed(app);
    }
}

void Editor::Proceed(app::Application& app) {
    const Pending pending = std::exchange(m_pending, Pending::None);
    std::string path = std::move(m_pendingPath);
    switch (pending) {
    case Pending::None:
        return;
    case Pending::Quit:
        app.RequestQuit();
        return;
    case Pending::CloseTab:
        CloseTab(m_closingTab);
        return;
    case Pending::Reload:
        path = m_levelPath;
        break;
    case Pending::New:
        if (core::Result<void> written =
                scene::SaveLevelFile(m_assetRoot / std::filesystem::path(path), scene::Level{});
            !written) {
            m_status = core::Format(written.error());
            return;
        }
        break;
    case Pending::Open:
        break;
    }
    if (core::Result<void> opened = Open(app, path); !opened) {
        m_status = core::Format(opened.error());
        DAI_LOG_ERROR(kModule, "{}", m_status);
    }
}

bool Editor::OnCloseRequested(app::Application& app) {
    StopPlaying(app);
    StopSimulating();
    m_history.Commit(Snapshot());
    if (!Dirty()) {
        // Another tab with changes is shown, and asked about.
        for (std::size_t index = 0; index < m_tabs.size(); ++index) {
            if (index != m_activeTab && TabDirty(*m_tabs[index])) {
                SwitchTab(index);
                Guard(app, Pending::Quit);
                return false;
            }
        }
        return true;
    }
    Guard(app, Pending::Quit);
    return false;
}

void Editor::OnStop(app::Application& app) {
    StopPlaying(app);
    // What holds GPU resources goes while the renderer is still there: the application destroys it when Run
    // returns, before the editor itself goes.
    m_loading.reset();
    m_browser.reset();
    if (!m_previewTexture.IsNull() && m_app != nullptr) {
        m_app->Renderer().Device().DestroyTexture(m_previewTexture);
        m_previewTexture = {};
    }
    for (std::unique_ptr<Tab>& tab : m_tabs) {
        if (tab != nullptr) {
            for (Pane& pane : tab->panes) {
                if (!pane.snapshot.IsNull() && m_app != nullptr) {
                    m_app->Renderer().Device().DestroyTexture(pane.snapshot);
                }
            }
            tab->instance.reset();
            tab->scene.reset();
        }
    }
    for (Pane& pane : m_panes) {
        if (!pane.snapshot.IsNull() && m_app != nullptr) {
            m_app->Renderer().Device().DestroyTexture(pane.snapshot);
            pane.snapshot = {};
        }
    }
    m_instance.reset();
    // The sandbox reads its level again when F5 starts it over: the play copy stays while it runs.
    if (!m_play.Running() && !m_playFile.empty()) {
        std::error_code error;
        std::filesystem::remove(m_playFile, error);
    }
}

std::filesystem::path Editor::AutosaveFile() const {
    std::string name = m_levelPath;
    std::ranges::replace(name, '/', '_');
    std::ranges::replace(name, '\\', '_');
    return platform::ExecutableDirectory() / "autosave" / name;
}

void Editor::Autosave(float seconds) {
    if (m_autosaveSeconds <= 0.0f || !Dirty() || m_history.Current() == m_autosavedRevision) {
        m_sinceAutosave = 0.0f;
        return;
    }
    m_sinceAutosave += seconds;
    if (m_sinceAutosave < m_autosaveSeconds || m_history.IsOpen()) {
        return;
    }
    m_sinceAutosave = 0.0f;
    const std::filesystem::path file = AutosaveFile();
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    if (core::Result<void> saved = scene::SaveLevelFile(file, m_level); !saved) {
        DAI_LOG_WARNING(kModule, "Autosave failed: {}", core::Format(saved.error()));
        return;
    }
    m_autosavedRevision = m_history.Current();
    DAI_LOG_INFO(kModule, "Autosaved to {}", file.generic_string());
}

void Editor::CheckAutosave() {
    const std::filesystem::path file = AutosaveFile();
    std::error_code error;
    if (!std::filesystem::exists(file, error)) {
        return;
    }
    const auto autosaved = std::filesystem::last_write_time(file, error);
    const auto saved = std::filesystem::last_write_time(m_assetRoot / std::filesystem::path(m_levelPath), error);
    core::Result<scene::Level> read = scene::LoadLevelFile(file);
    if (error || autosaved <= saved || !read || *read == m_level) {
        std::filesystem::remove(file, error);
        return;
    }
    m_recovered = std::move(*read);
    m_openPopup = "Recover";
}

void Editor::Play(bool fromCamera) {
    m_history.Commit(Snapshot());
    if (m_play.Running()) {
        m_play.Kill();
    }
    // The level as it is, saved or not, beside its file so that its paths still resolve.
    std::string name = std::filesystem::path(m_levelPath).filename().generic_string();
    if (name.ends_with(kLevelExtension)) {
        name.resize(name.size() - kLevelExtension.size());
    }
    const std::string playPath =
        (std::filesystem::path(m_levelPath).parent_path() / (name + ".play.level.json")).generic_string();
    m_playFile = m_assetRoot / std::filesystem::path(playPath);
    if (core::Result<void> saved = scene::SaveLevelFile(m_playFile, m_level); !saved) {
        m_status = core::Format(saved.error());
        return;
    }
    const std::filesystem::path sandbox = SandboxExecutable();
    std::vector<std::string> arguments{sandbox.string(), "+sandbox.scene",      playPath, "+sandbox.menu",
                                       "false",          "+sandbox.calibrated", "true"};
    if (fromCamera) {
        arguments.emplace_back("+sandbox.start");
        arguments.push_back(std::format("{} {} {} {} {}", m_camera.position.x, m_camera.position.y, m_camera.position.z,
                                        glm::degrees(m_camera.yaw), glm::degrees(m_camera.pitch)));
    }
    core::Result<platform::ChildProcess> started = platform::StartProcess(arguments, sandbox.parent_path());
    if (!started) {
        m_status = core::Format(started.error());
        DAI_LOG_ERROR(kModule, "{}", m_status);
        return;
    }
    m_play = std::move(*started);
    m_status = fromCamera ? "Playing from the camera" : "Playing from the start";
    DAI_LOG_INFO(kModule, "Playing {} with {}", playPath, sandbox.generic_string());
}

void Editor::OnUpdate(app::Application& app, const core::FrameTime& time) {
    if (m_frameLimit > 0 && ++m_frames >= m_frameLimit) {
        app.RequestQuit();
    }
    // While the game plays, it has the window and the frame (ADR-0110).
    if (UpdateGame(app, time)) {
        return;
    }
    if (m_loading != nullptr) {
        m_loading->seconds += static_cast<float>(time.unclampedDelta.count());
        if (m_loading->ready) {
            FinishOpen(app);
        }
    }
    ImGuizmo::BeginFrame();
    m_createAt.reset();
    const std::optional<renderer::DepthPicture>& depth = app.Renderer().LatestDepth();
    m_depth = DepthTest(depth ? &*depth : nullptr);
    if (m_scene != nullptr) {
        // Paused unless real time, but for the frames F10 steps; physics only while simulating.
        const bool paused = !m_realtime && m_stepFrames == 0;
        m_stepFrames = std::max(m_stepFrames - 1, 0);
        if (m_scene->IsPaused() != paused) {
            m_scene->SetPaused(paused);
        }
        m_scene->SetPhysicsPaused(!m_simulating);
        // The level heard, if asked: its ambient sounds and what a sequence plays (ADR-0116).
        if (m_scene->Sounds().IsPaused() == m_hear) {
            m_scene->Sounds().SetPaused(!m_hear);
        }
    }
    DrawMenu(app);
    DrawDockspace();
    DrawHierarchy();
    DrawInspector();
    DrawEnvironment();
    FollowSelectionMaterials();
    if (m_scene != nullptr) {
        // A tab beside the inspector the first time, also in a layout saved before the panel existed.
        if (const ImGuiWindow* inspector = ImGui::FindWindowByName("Inspector");
            inspector != nullptr && inspector->DockId != 0) {
            ImGui::SetNextWindowDockID(inspector->DockId, ImGuiCond_FirstUseEver);
        }
        m_materials.Draw(m_scene->Assets(), m_assetRoot);
    }
    if (const ImGuiWindow* inspector = ImGui::FindWindowByName("Inspector");
        inspector != nullptr && inspector->DockId != 0 &&
        ImGui::FindWindowSettingsByID(ImHashStr("Effect")) == nullptr) {
        ImGui::SetNextWindowDockID(inspector->DockId, ImGuiCond_FirstUseEver);
    }
    m_effects.Draw(m_assetRoot);
    UpdateSequences(static_cast<float>(time.unclampedDelta.count()));
    TrackPanelEdits();
    DrawHistory();
    DrawMapCheck();
    DrawStatistics(app);
    DrawCameraPreview();
    DrawTabs();
    DrawPaint();
    DrawStatus(app, time);
    DrawDialogs(app);
    DrawAssetDialogs();
    DrawPalette(app);
    DrawLoading();
    if (m_browser != nullptr) {
        if (const std::optional<AssetEntry> chosen = m_browser->Draw()) {
            PlaceAsset(app, *chosen, std::nullopt);
        }
        if (const std::optional<AssetEntry> edit = m_browser->TakeEdit()) {
            m_pendingEffect = edit->path;
        }
        if (std::optional<AssetRequest> request = m_browser->TakeRequest()) {
            m_assetRequest = std::move(request);
            switch (m_assetRequest->kind) {
            case AssetRequest::Kind::References:
                m_references = FindReferences(m_assetRoot, m_assetRequest->asset.path);
                m_openPopup = "What uses it";
                break;
            case AssetRequest::Kind::Move:
                m_pathField = m_assetRequest->asset.path;
                m_openPopup = "Rename or move";
                break;
            case AssetRequest::Kind::Import:
                m_importSettings =
                    scene::ReadImportSettings(m_assetRoot / std::filesystem::path(m_assetRequest->asset.path));
                m_openPopup = "Import settings";
                break;
            }
        }
        // An asset dragged out of the browser is placed where it is let go over the view.
        if (const std::optional<AssetEntry> dragged = m_browser->Dragged();
            dragged && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            m_browser->EndDrag();
            const bool overPanel =
                ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
            if (MouseInViewport() && !overPanel) {
                const ImVec2 mouse = ImGui::GetIO().MousePos;
                PlaceAsset(app, *dragged, glm::vec2(mouse.x, mouse.y));
            }
        }
    }

    // While a level loads, the one open shows but does not move or change.
    if (m_scene != nullptr && m_loading == nullptr) {
        UpdatePreview(app);
        const bool switching = UpdatePanes(app);
        UsePane(m_active);
        if (!switching) {
            UpdateCamera(app, static_cast<float>(time.unclampedDelta.count()));
        }
        for (std::size_t pane = 0; pane < m_panes.size(); ++pane) {
            if (!m_split && pane != m_active) {
                continue;
            }
            UsePane(pane);
            DrawPaneFrame(pane);
            if (!m_show.gameView) {
                DrawGrid();
                DrawMarkers();
                DrawShownExtras();
                DrawSelectedCue();
            }
        }
        UsePane(m_active);
        if (!switching && !m_show.gameView) {
            ViewportContextMenu(app);
        }
        if (switching) {
            // The click chose a pane: it does nothing else.
        } else if (m_measuring) {
            Measure(app);
            DrawMeasure();
        } else if (m_painting) {
            DrawBrush();
            PaintStrokes(app);
        } else {
            if (m_stroking) {
                m_stroking = false;
                m_history.Commit(Snapshot());
            }
            if (!m_show.gameView) {
                DrawGizmo();
            }
            Pick(app);
        }
    }
    if (m_loading == nullptr) {
        HandleShortcuts(app);
    }
    DrawHelp();

    // What the inspector asked for, once it is drawn.
    if (!m_pendingOpenPrefab.empty()) {
        // A prefab opens beside the level that places it (ADR-0109).
        if (core::Result<void> opened = Open(app, std::exchange(m_pendingOpenPrefab, {}), std::nullopt, true);
            !opened) {
            m_status = core::Format(opened.error());
        }
    }
    if (m_pendingUnpack != 0) {
        UnpackPrefab(std::exchange(m_pendingUnpack, 0));
    }
    if (m_pendingApplyOverrides != 0) {
        ApplyOverridesToPrefab(std::exchange(m_pendingApplyOverrides, 0));
    }
    if (!m_pendingEffect.empty() && m_loading == nullptr) {
        m_effects.Open(m_assetRoot, std::exchange(m_pendingEffect, {}));
        m_status = m_effects.Status();
        ImGui::SetWindowFocus("Effect");
    }
    if (m_pendingSequence != 0) {
        EditSequence(std::exchange(m_pendingSequence, 0));
    }

    // An edit ends when the mouse lets go of the widget or the gizmo that made it.
    if (m_history.IsOpen() && !ImGui::IsAnyItemActive() && !ImGuizmo::IsUsing() && !m_stroking) {
        m_history.Commit(Snapshot());
    }
    Autosave(static_cast<float>(time.unclampedDelta.count()));
    if (!m_playFile.empty() && !m_play.Running()) {
        std::error_code error;
        std::filesystem::remove(m_playFile, error);
        m_playFile.clear();
    }
    UpdateTitle(app);
    if (m_scene != nullptr && !m_cameraEntity.IsNull()) {
        // The scene is drawn from the live pane's camera: an axis view's stands far back with a narrow lens.
        m_drawPane = m_active;
        const EditorCamera eye = Eye();
        static_cast<math::Transform&>(m_scene->World().Get<ecs::LocalTransform>(m_cameraEntity)) =
            Decompose(eye.World());
        scene::Camera& camera = m_scene->World().Get<scene::Camera>(m_cameraEntity);
        camera.verticalFov = eye.verticalFov;
        camera.nearPlane = eye.nearPlane;
    }
}

void Editor::FrameSelection() {
    if (m_selection.empty() || m_instance == nullptr) {
        return;
    }
    ecs::World& world = m_scene->World();
    ecs::PropagateTransforms(world);
    Bounds bounds;
    for (const std::uint64_t id : m_selection) {
        const ecs::Entity entity = m_instance->Find(id);
        if (entity.IsNull()) {
            continue;
        }
        const Bounds own = m_picker.WorldBounds(world, entity);
        if (own.Empty()) {
            const glm::vec3 at(world.Get<ecs::WorldTransform>(entity).matrix[3]);
            bounds.Add(at - glm::vec3(0.5f));
            bounds.Add(at + glm::vec3(0.5f));
        } else {
            bounds.Add(own);
        }
    }
    if (bounds.Empty()) {
        return;
    }
    if (Pane& live = m_panes[m_active]; live.kind != ViewKind::Perspective) {
        const glm::vec2 size = live.rect.Size();
        live.ortho = Framed(live.kind, bounds.min, bounds.max, size.x / std::max(size.y, 1.0f));
        return;
    }
    const glm::vec3 center = (bounds.min + bounds.max) * 0.5f;
    const float radius = glm::length(bounds.max - bounds.min) * 0.5f;
    const float distance = std::max(radius / std::tan(m_camera.verticalFov * 0.5f), 1.0f);
    m_camera.position = center - m_camera.Forward() * distance;
}
void Editor::OnRender(app::Application& app, renderer::SceneView& view) {
    if (m_game != nullptr) {
        // The game has the whole window, as in the sandbox.
        view.viewport.reset();
        m_game->OnRender(app, view);
        return;
    }
    LightFromCamera(view);
    FilterView(view);
    // A pane that becomes live, or changes its view, does not follow from the last frame (ADR-0108).
    view.cut = view.cut || std::exchange(m_cutView, false);
    // The scene fills the dockspace's central node, in pixels (ADR-0055).
    const platform::Extent size = app.MainWindow().Size();
    const platform::Extent pixels = app.MainWindow().PixelSize();
    const float scaleX = size.width > 0 ? static_cast<float>(pixels.width) / static_cast<float>(size.width) : 1.0f;
    const float scaleY = size.height > 0 ? static_cast<float>(pixels.height) / static_cast<float>(size.height) : 1.0f;
    const Rect& live = m_panes[m_active].rect;
    const glm::vec2 extent = live.Size();
    if (extent.x >= 8.0f && extent.y >= 8.0f) {
        view.viewport = renderer::ViewRect{.x = static_cast<std::uint32_t>(std::max(live.min.x, 0.0f) * scaleX),
                                           .y = static_cast<std::uint32_t>(std::max(live.min.y, 0.0f) * scaleY),
                                           .width = static_cast<std::uint32_t>(extent.x * scaleX),
                                           .height = static_cast<std::uint32_t>(extent.y * scaleY)};
    }
}

void Editor::SetDebugView(std::string_view name) {
    if (m_app != nullptr) {
        if (core::Result<void> set = m_app->CVars().Set("renderer.debug_view", name); !set) {
            m_status = core::Format(set.error());
        }
    }
}

std::string Editor::DebugViewName() const {
    const core::CVarEntry* entry = m_app != nullptr ? m_app->CVars().Find("renderer.debug_view") : nullptr;
    const std::string* name = entry != nullptr ? std::get_if<std::string>(&entry->value) : nullptr;
    return name != nullptr ? *name : std::string("off");
}

void Editor::SetLighting(Lighting lighting) {
    m_lighting = lighting;
    switch (lighting) {
    case Lighting::Lit:
    case Lighting::EditorLight:
        SetDebugView("off");
        break;
    case Lighting::Unlit:
        SetDebugView("base_color");
        break;
    case Lighting::LightingOnly:
        SetDebugView("lighting_only");
        break;
    }
    constexpr std::array<const char*, 4> kNames{"Lit", "Unlit", "Lighting only", "Editor light"};
    m_status = std::format("Lighting: {}", kNames[static_cast<std::size_t>(lighting)]);
}

void Editor::LightFromCamera(renderer::SceneView& view) const {
    if (m_lighting != Lighting::EditorLight) {
        return;
    }
    // The level's lights go dark (emissive surfaces still glow); one soft lamp rides above the camera, as a
    // miner's lamp, bright enough for the eye's adaptation to make the room readable. Fog would scatter it into
    // a veil in front of everything, so it goes too.
    view.lights.clear();
    view.fogVolumes.clear();
    view.atmosphere.fog.density = 0.0f;
    renderer::Light lamp;
    lamp.type = renderer::LightType::Point;
    const Pane& live = m_panes[m_active];
    lamp.position = live.kind == ViewKind::Perspective ? m_camera.position + glm::vec3(0.0f, 0.25f, 0.0f)
                                                       : live.ortho.focus - ViewAxis(live.kind) * 0.3f;
    lamp.color = glm::vec3(1.0f, 0.96f, 0.9f) * 800.0f;
    lamp.range = 80.0f;
    lamp.radius = 0.5f;
    lamp.id = 0xED170001ULL;
    view.lights.push_back(lamp);
}

void Editor::DrawLightingMenu() {
    const std::string debug = DebugViewName();
    const auto mode = [this, &debug](const char* label, const char* keys, Lighting lighting) {
        const bool shown = m_lighting == lighting && (lighting == Lighting::Unlit          ? debug == "base_color"
                                                      : lighting == Lighting::LightingOnly ? debug == "lighting_only"
                                                                                           : debug == "off");
        if (ImGui::MenuItem(label, keys, shown)) {
            SetLighting(lighting);
        }
    };
    mode("Lit", "Alt+1", Lighting::Lit);
    mode("Unlit", "Alt+2", Lighting::Unlit);
    mode("Lighting only", "Alt+3", Lighting::LightingOnly);
    mode("Editor light", "Alt+4", Lighting::EditorLight);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("The level's lights off, one lamp at the camera: to place things in the dark");
    }
    ImGui::Separator();
    if (ImGui::BeginMenu("Debug views")) {
        for (const auto& [name, label] : kDebugViews) {
            if (ImGui::MenuItem(label, nullptr, debug == name)) {
                m_lighting = Lighting::Lit;
                SetDebugView(debug == name ? "off" : name);
            }
        }
        ImGui::EndMenu();
    }
}

void Editor::DrawMenu(app::Application& app) {
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New...", Keys("file.new"))) {
            m_pathField = "Levels/New.level.json";
            m_openPopup = "New";
        }
        if (ImGui::MenuItem("Open in a new tab...", Keys("file.open_tab"))) {
            m_levelFiles = FindLevelFiles(m_assetRoot);
            m_openInNewTab = true;
            m_openPopup = "Open";
        }
        if (ImGui::MenuItem("Close the tab", Keys("file.close_tab"), false, m_tabs.size() > 1)) {
            m_pendingCloseTab = m_activeTab;
        }
        if (ImGui::MenuItem("Open...", Keys("file.open"))) {
            m_openInNewTab = false;
            m_levelFiles = FindLevelFiles(m_assetRoot);
            m_openPopup = "Open";
        }
        if (ImGui::MenuItem("Save", Keys("file.save"))) {
            Save();
        }
        if (ImGui::MenuItem("Save As...", Keys("file.save_as"))) {
            m_pathField = m_levelPath;
            m_openPopup = "Save As";
        }
        if (ImGui::MenuItem("Reload from disk")) {
            Guard(app, Pending::Reload);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Quit")) {
            Guard(app, Pending::Quit);
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        const std::string undo = m_history.CanUndo() ? std::format("Undo {}", m_history.UndoLabel()) : "Undo";
        const std::string redo = m_history.CanRedo() ? std::format("Redo {}", m_history.RedoLabel()) : "Redo";
        if (ImGui::MenuItem(undo.c_str(), "Ctrl+Z", false, m_history.CanUndo())) {
            Undo();
        }
        if (ImGui::MenuItem(redo.c_str(), "Ctrl+Y", false, m_history.CanRedo())) {
            Redo();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Cut", Keys("edit.cut"), false, !m_selection.empty())) {
            CopySelection();
            DeleteSelected();
        }
        if (ImGui::MenuItem("Copy", Keys("edit.copy"), false, !m_selection.empty())) {
            CopySelection();
        }
        if (ImGui::MenuItem("Paste", Keys("edit.paste"))) {
            Paste();
        }
        if (ImGui::MenuItem("Duplicate", Keys("edit.duplicate"), false, !m_selection.empty())) {
            Duplicate();
        }
        if (ImGui::MenuItem("Delete", Keys("edit.delete"), false, !m_selection.empty())) {
            DeleteSelected();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Select all", Keys("edit.select_all"))) {
            SelectAll();
        }
        if (ImGui::MenuItem("Select none", Keys("edit.cancel"), false, !m_selection.empty())) {
            Select(0);
        }
        ImGui::Separator();
        ImGui::Separator();
        if (ImGui::MenuItem("Hide", Keys("edit.hide"), false, !m_selection.empty())) {
            SetHidden(SelectionRoots(), true);
        }
        if (ImGui::MenuItem("Show everything", Keys("edit.show_all"))) {
            ShowEverything();
        }
        if (ImGui::MenuItem(m_isolated.empty() ? "Isolate" : "Stop isolating", Keys("edit.isolate"), false,
                            !m_selection.empty() || !m_isolated.empty())) {
            ToggleIsolate();
        }
        if (ImGui::MenuItem("Drop to the floor", Keys("edit.drop"), false, !m_selection.empty())) {
            DropToFloor();
        }
        if (ImGui::MenuItem("Lock or unlock", Keys("edit.lock"), false, !m_selection.empty())) {
            const scene::LevelEntity* selected = Selected();
            SetLocked(SelectionRoots(), selected == nullptr || !selected->editing.locked);
        }
        if (ImGui::MenuItem("Put in a folder...", nullptr, false, !m_selection.empty())) {
            const scene::LevelEntity* selected = Selected();
            m_pathField = selected != nullptr ? selected->editing.folder : std::string();
            m_openPopup = "Folder";
        }
        if (ImGui::BeginMenu("Align", m_selection.size() > 1)) {
            constexpr std::array<const char*, 3> kAxes{"X", "Y", "Z"};
            for (int axis = 0; axis < 3; ++axis) {
                if (ImGui::MenuItem(std::format("Lowest {}", kAxes[static_cast<std::size_t>(axis)]).c_str())) {
                    AlignSelection(axis, AlignTo::Min);
                }
                if (ImGui::MenuItem(std::format("Middle {}", kAxes[static_cast<std::size_t>(axis)]).c_str())) {
                    AlignSelection(axis, AlignTo::Center);
                }
                if (ImGui::MenuItem(std::format("Highest {}", kAxes[static_cast<std::size_t>(axis)]).c_str())) {
                    AlignSelection(axis, AlignTo::Max);
                }
                if (axis < 2) {
                    ImGui::Separator();
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Spread evenly", m_selection.size() > 2)) {
            if (ImGui::MenuItem("Along X")) {
                SpreadSelection(0);
            }
            if (ImGui::MenuItem("Along Y")) {
                SpreadSelection(1);
            }
            if (ImGui::MenuItem("Along Z")) {
                SpreadSelection(2);
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Make prefab...", nullptr, false, !m_selection.empty())) {
            const scene::LevelEntity* primary = Selected();
            std::string name = primary != nullptr && !primary->name.empty() ? primary->name : "Prefab";
            std::ranges::replace(name, ' ', '_');
            const std::filesystem::path directory = std::filesystem::path(m_levelPath).parent_path();
            m_pathField = (directory / "Prefabs" / (Lower(name) + std::string(kPrefabExtension))).generic_string();
            m_openPopup = "Make prefab";
        }
        if (ImGui::MenuItem("Unpack prefab", nullptr, false, Selected() != nullptr && !Selected()->prefab.empty())) {
            UnpackPrefab(m_selected);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Frame the selection", Keys("edit.frame"), false, Selected() != nullptr)) {
            FrameSelection();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Create")) {
        DrawCreateItems();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Play")) {
        if (ImGui::MenuItem("In the editor", Keys("play.editor"), false, m_scene != nullptr)) {
            StartPlaying(app, false);
        }
        if (ImGui::MenuItem("In the editor, from the camera", Keys("play.editor_camera"), false, m_scene != nullptr)) {
            StartPlaying(app, true);
        }
        if (ImGui::MenuItem(m_simulating ? "Stop simulating" : "Simulate", Keys("play.simulate"), m_simulating,
                            m_scene != nullptr)) {
            if (m_simulating) {
                StopSimulating();
            } else {
                StartSimulating();
            }
        }
        if (ImGui::MenuItem("Keep where loose objects are", Keys("play.keep"), false, m_simulating)) {
            KeepSimulation();
        }
        if (ImGui::MenuItem("Step one frame", Keys("view.step"))) {
            m_stepFrames = 1;
            m_realtime = false;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("In the sandbox, from the start", Keys("play.sandbox"))) {
            Play(false);
        }
        if (ImGui::MenuItem("In the sandbox, from the camera", Keys("play.sandbox_camera"))) {
            Play(true);
        }
        if (ImGui::MenuItem("Stop", nullptr, false, m_play.Running())) {
            m_play.Kill();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        if (ImGui::BeginMenu("Lighting")) {
            DrawLightingMenu();
            ImGui::EndMenu();
        }
        ImGui::Separator();
        constexpr std::array<std::pair<ViewKind, const char*>, 4> kKinds{{{ViewKind::Perspective, "Shift+1"},
                                                                          {ViewKind::Top, "Shift+2"},
                                                                          {ViewKind::Front, "Shift+3"},
                                                                          {ViewKind::Side, "Shift+4"}}};
        for (const auto& [kind, keys] : kKinds) {
            if (ImGui::MenuItem(std::string(ViewName(kind)).c_str(), keys, m_panes[m_active].kind == kind)) {
                SetViewKind(kind);
            }
        }
        if (ImGui::MenuItem("Four views", Keys("view.split"), m_split)) {
            SetSplit(!m_split);
        }
        ImGui::Separator();
        ImGui::MenuItem("Snap to surfaces while moving", nullptr, &m_surfaceSnap);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("The selection goes onto what is under the mouse; Ctrl+Shift does it once, V snaps to "
                              "the nearest corner");
        }
        ImGui::MenuItem("Turn to the surface", nullptr, &m_alignToSurface);
        ImGui::Separator();
        ImGui::MenuItem("Grid", Keys("view.grid"), &m_show.grid);
        ImGui::MenuItem("Snapping", "X", &m_snap);
        ImGui::SetNextItemWidth(140.0f);
        ImGui::DragFloat("Move step (m)", &m_snapSteps.x, 0.01f, 0.01f, 10.0f, "%.2f");
        ImGui::SetNextItemWidth(140.0f);
        ImGui::DragFloat("Rotate step (deg)", &m_snapSteps.y, 0.5f, 1.0f, 90.0f, "%.0f");
        ImGui::SetNextItemWidth(140.0f);
        ImGui::DragFloat("Scale step", &m_snapSteps.z, 0.01f, 0.01f, 1.0f, "%.2f");
        ImGui::Separator();
        if (ImGui::MenuItem("Read the prefabs again")) {
            ReloadPrefabs();
        }
        if (ImGui::MenuItem("Reset the layout")) {
            m_layoutBuilt = false;
            ImGui::ClearIniSettings();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Check")) {
        if (ImGui::MenuItem("Check the level", Keys("check.level"))) {
            RunMapCheck();
            ImGui::SetWindowFocus("Map check");
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Show")) {
        DrawShowMenu();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        ImGui::MenuItem("Shortcuts and keys", Keys("help.shortcuts"), &m_showHelp);
        if (ImGui::MenuItem("Search everything", Keys("help.palette"))) {
            OpenPalette();
        }
        ImGui::EndMenu();
    }
    ImGui::TextDisabled("|");
    // The tools, as icons (ADR-0118).
    const std::string moveTip = std::format("Move ({})", Keys("tool.move") != nullptr ? Keys("tool.move") : "");
    if (IconButton("##move", Icon::Move, m_operation == ImGuizmo::TRANSLATE || m_operation == 0, moveTip.c_str())) {
        m_operation = ImGuizmo::TRANSLATE;
    }
    const std::string rotateTip = std::format("Rotate ({})", Keys("tool.rotate") != nullptr ? Keys("tool.rotate") : "");
    if (IconButton("##rotate", Icon::Rotate, m_operation == ImGuizmo::ROTATE, rotateTip.c_str())) {
        m_operation = ImGuizmo::ROTATE;
    }
    const std::string scaleTip = std::format("Scale ({})", Keys("tool.scale") != nullptr ? Keys("tool.scale") : "");
    if (IconButton("##scale", Icon::Scale, m_operation == ImGuizmo::SCALE, scaleTip.c_str())) {
        m_operation = ImGuizmo::SCALE;
    }
    if (IconButton("##local", m_local ? Icon::Local : Icon::World, false,
                   m_local ? "The gizmo moves along the entity's own axes: click for the world's"
                           : "The gizmo moves along the world's axes: click for the entity's own")) {
        m_local = !m_local;
    }
    const std::string snapTip =
        std::format("Snapping ({}): moves by {:.2f} m, turns by {:.0f} degrees, scales by {:.2f}; View sets the steps",
                    Keys("tool.snap") != nullptr ? Keys("tool.snap") : "", m_snapSteps.x, m_snapSteps.y, m_snapSteps.z);
    if (IconButton("##snap", Icon::Snap, m_snap, snapTip.c_str())) {
        m_snap = !m_snap;
    }
    ImGui::TextDisabled("|");
    if (IconButton("##paint", Icon::Paint, m_painting, "Paint decals, wet patches and props on surfaces (B)")) {
        m_painting = !m_painting;
        m_measuring = false;
    }
    if (IconButton("##measure", Icon::Measure, m_measuring, "Measure between two surfaces (M)")) {
        m_measuring = !m_measuring;
        m_painting = false;
    }
    ImGui::TextDisabled("|");
    constexpr std::array<const char*, 3> kPivots{"Pivot: origin", "Pivot: center", "Pivot: bottom"};
    if (ImGui::BeginMenu(std::format("{}##pivot", kPivots[static_cast<std::size_t>(m_pivot)]).c_str())) {
        if (ImGui::MenuItem("The primary's origin", nullptr, m_pivot == Pivot::Origin)) {
            m_pivot = Pivot::Origin;
        }
        if (ImGui::MenuItem("The center of the selection", nullptr, m_pivot == Pivot::Center)) {
            m_pivot = Pivot::Center;
        }
        if (ImGui::MenuItem("The bottom of the selection", nullptr, m_pivot == Pivot::Bottom)) {
            m_pivot = Pivot::Bottom;
        }
        ImGui::EndMenu();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Where the gizmo sits, and what the selection turns and scales around");
    }
    ImGui::TextDisabled("|");
    if (IconButton("##play", Icon::Play, m_game != nullptr, "Play in the editor (Alt+P); Shift+Esc stops")) {
        StartPlaying(*m_app, false);
    }
    if (IconButton("##simulate", Icon::Simulate, m_simulating, "Simulate physics and triggers (Alt+S)")) {
        if (m_simulating) {
            StopSimulating();
        } else {
            StartSimulating();
        }
    }
    ImGui::TextDisabled("|");
    constexpr std::array<const char*, 4> kLighting{"Lit", "Unlit", "Lighting only", "Editor light"};
    const std::string debug = DebugViewName();
    const bool debugShown = debug != "off" && debug != "base_color" && debug != "lighting_only";
    const char* shownLabel = kLighting[static_cast<std::size_t>(m_lighting)];
    for (const auto& [name, label] : kDebugViews) {
        if (debugShown && debug == name) {
            shownLabel = label;
        }
    }
    const std::string lighting = std::format("{}##lighting", shownLabel);
    if (ImGui::BeginMenu(lighting.c_str())) {
        DrawLightingMenu();
        ImGui::EndMenu();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("How the view is lit (Alt+1 to Alt+4), and the renderer's debug views");
    }
    ImGui::EndMainMenuBar();
}

void Editor::DrawDialogs(app::Application& app) {
    if (!m_openPopup.empty()) {
        ImGui::OpenPopup(m_openPopup.c_str());
        m_openPopup.clear();
        m_filter.clear();
    }
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    const auto place = [&center](float width) {
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(width, 0.0f), ImGuiCond_Appearing);
    };

    place(560.0f);
    if (ImGui::BeginPopupModal("Open", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        InputString("##filter", m_filter);
        ImGui::BeginChild("files", ImVec2(0.0f, 300.0f), ImGuiChildFlags_Borders);
        for (const std::string& file : m_levelFiles) {
            if (!m_filter.empty() && file.find(m_filter) == std::string::npos) {
                continue;
            }
            if (ImGui::Selectable(file.c_str(), file == m_levelPath, ImGuiSelectableFlags_AllowDoubleClick) &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                ImGui::CloseCurrentPopup();
                if (m_openInNewTab) {
                    if (core::Result<void> opened = Open(app, file, std::nullopt, true); !opened) {
                        m_status = core::Format(opened.error());
                    }
                } else {
                    Guard(app, Pending::Open, file);
                }
            }
        }
        ImGui::EndChild();
        ImGui::Checkbox("In a new tab", &m_openInNewTab);
        ImGui::SameLine();
        ImGui::TextDisabled("Level files under assets/. Double-click one to open it.");
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Asks for a level path under assets/; mustBeNew refuses one that exists.
    const auto pathDialog = [&](const char* name, const char* action, bool mustBeNew,
                                const std::function<void(const std::string&)>& run,
                                std::string_view extension = kLevelExtension) {
        place(560.0f);
        if (!ImGui::BeginPopupModal(name, nullptr, ImGuiWindowFlags_NoSavedSettings)) {
            return;
        }
        ImGui::TextUnformatted("Path under assets/:");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        InputString("##path", m_pathField);
        const std::string path = LevelPathOf(m_pathField, extension);
        std::error_code error;
        const bool exists = !path.empty() && std::filesystem::exists(m_assetRoot / std::filesystem::path(path), error);
        if (exists) {
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
                               mustBeNew ? "%s exists already." : "%s exists and will be replaced.", path.c_str());
        } else {
            ImGui::TextDisabled("%s", path.c_str());
        }
        const bool allowed = !path.empty() && !(mustBeNew && exists);
        ImGui::BeginDisabled(!allowed);
        const bool confirmed = ImGui::Button(action);
        ImGui::EndDisabled();
        if (allowed && (confirmed || ImGui::IsKeyPressed(ImGuiKey_Enter))) {
            ImGui::CloseCurrentPopup();
            run(path);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    };
    pathDialog("Save As", "Save", false, [this](const std::string& path) { Save(path); });
    pathDialog("New", "Create", true, [this, &app](const std::string& path) { Guard(app, Pending::New, path); });
    pathDialog(
        "Make prefab", "Save the prefab", false, [this](const std::string& path) { MakePrefab(path); },
        kPrefabExtension);

    place(460.0f);
    if (ImGui::BeginPopupModal("Rename", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        InputString("##name", m_pathField);
        if (ImGui::Button("Rename") || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            ImGui::CloseCurrentPopup();
            if (const scene::LevelEntity* selected = Selected()) {
                scene::LevelEntity renamed = *selected;
                renamed.name = m_pathField;
                m_history.Commit(Snapshot());
                Apply(renamed, std::format("Rename {} to {}", EntityLabel(*selected), m_pathField));
                m_history.Commit(Snapshot());
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    place(460.0f);
    if (ImGui::BeginPopupModal("Folder", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextUnformatted("Folder in the hierarchy, such as Lights/Hall (empty for none):");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        InputString("##folder", m_pathField);
        if (ImGui::Button("Put there") || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            ImGui::CloseCurrentPopup();
            MoveToFolder(SelectionRoots(), TidyFolder(m_pathField));
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    place(460.0f);
    if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextWrapped("%s has changes that are not saved.", m_levelPath.c_str());
        std::size_t others = 0;
        for (std::size_t index = 0; index < m_tabs.size(); ++index) {
            others += index != m_activeTab && TabDirty(*m_tabs[index]) ? 1 : 0;
        }
        if (m_pending == Pending::Quit && others > 0) {
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
                               "%zu other tabs have changes too: Don't save drops them.", others);
        }
        if (ImGui::Button("Save")) {
            ImGui::CloseCurrentPopup();
            if (Save()) {
                Proceed(app);
            } else {
                m_pending = Pending::None;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't save")) {
            ImGui::CloseCurrentPopup();
            Proceed(app);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
            m_pending = Pending::None;
        }
        ImGui::EndPopup();
    }

    place(460.0f);
    if (ImGui::BeginPopupModal("Recover", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextWrapped("The last time the editor ran, it autosaved changes to %s that were never saved. "
                           "Recover them?",
                           m_levelPath.c_str());
        if (ImGui::Button("Recover")) {
            ImGui::CloseCurrentPopup();
            if (m_recovered) {
                scene::Level recovered = std::move(*m_recovered);
                m_recovered.reset();
                Change("Recover the autosave", [&recovered](scene::Level& level) { level = std::move(recovered); });
                m_history.Commit(Snapshot());
                m_status = "Recovered the autosave: save to keep it";
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard")) {
            ImGui::CloseCurrentPopup();
            m_recovered.reset();
            std::error_code error;
            std::filesystem::remove(AutosaveFile(), error);
        }
        ImGui::EndPopup();
    }
}

void Editor::DrawDockspace() {
    const ImGuiID dockspace =
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
    // The first layout: the hierarchy on the left, the inspector on the right, the status below.
    ImGuiDockNode* root = ImGui::DockBuilderGetNode(dockspace);
    if (!m_layoutBuilt && (root == nullptr || !root->IsSplitNode())) {
        ImGui::DockBuilderRemoveNodeChildNodes(dockspace);
        ImGuiID center = dockspace;
        const ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.2f, nullptr, &center);
        const ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.27f, nullptr, &center);
        const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.3f, nullptr, &center);
        ImGui::DockBuilderDockWindow("Hierarchy", left);
        ImGui::DockBuilderDockWindow("Inspector", right);
        ImGui::DockBuilderDockWindow("Environment", right);
        ImGui::DockBuilderDockWindow("Materials", right);
        ImGui::DockBuilderDockWindow("Assets", bottom);
        ImGui::DockBuilderDockWindow("Status", bottom);
        ImGui::DockBuilderDockWindow("History", bottom);
        ImGui::DockBuilderDockWindow("Map check", bottom);
        ImGui::DockBuilderDockWindow("Statistics", right);
        ImGui::DockBuilderDockWindow("Camera preview", right);
        ImGui::DockBuilderFinish(dockspace);
    }
    m_layoutBuilt = true;
    // A layout saved before the browser or the history existed: they join the status.
    for (const char* window : {"Statistics", "Camera preview"}) {
        if (ImGui::FindWindowSettingsByID(ImHashStr(window)) == nullptr) {
            if (const ImGuiWindow* inspector = ImGui::FindWindowByName("Inspector");
                inspector != nullptr && inspector->DockId != 0) {
                ImGui::DockBuilderDockWindow(window, inspector->DockId);
            }
        }
    }
    for (const char* window : {"Assets", "History", "Map check"}) {
        if (ImGui::FindWindowSettingsByID(ImHashStr(window)) == nullptr) {
            if (const ImGuiWindow* status = ImGui::FindWindowByName("Status");
                status != nullptr && status->DockId != 0) {
                ImGui::DockBuilderDockWindow(window, status->DockId);
            }
        }
    }
    if (const ImGuiDockNode* central = ImGui::DockBuilderGetCentralNode(dockspace)) {
        m_central = Rect{{central->Pos.x, central->Pos.y},
                         {central->Pos.x + central->Size.x, central->Pos.y + central->Size.y}};
        // The tabs of the open documents take a row at the top (ADR-0109).
        const float row = ImGui::GetFrameHeight() + 6.0f;
        m_tabBar = Rect{m_central.min, {m_central.max.x, m_central.min.y + row}};
        m_central.min.y += row;
    }
    LayOutPanes();
    UsePane(m_active);
}

void Editor::RowToggles(const std::string& key, bool hidden, bool locked, bool& toggleHidden, bool& toggleLocked) {
    // Two small buttons at the row's end: o shows, - hides; L is locked, . is not.
    const float width = ImGui::CalcTextSize("o").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SameLine(std::max(ImGui::GetContentRegionMax().x - width * 2.0f - ImGui::GetStyle().ItemSpacing.x, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, hidden ? ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)
                                                : ImGui::GetStyleColorVec4(ImGuiCol_Text));
    toggleHidden = ImGui::SmallButton(std::format("{}##hide{}", hidden ? "-" : "o", key).c_str());
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(hidden ? "Hidden in the editor: click to show (Alt+H shows everything)"
                                 : "Shown: click to hide in the editor (H)");
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text,
                          locked ? ImVec4(1.0f, 0.75f, 0.3f, 1.0f) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    toggleLocked = ImGui::SmallButton(std::format("{}##lock{}", locked ? "L" : ".", key).c_str());
    ImGui::PopStyleColor(2);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(locked ? "Locked: clicks in the view go through it. Click to unlock (L)"
                                 : "Click to lock against clicks in the view (L)");
    }
}

void Editor::DrawHierarchy() {
    if (!ImGui::Begin("Hierarchy")) {
        ImGui::End();
        return;
    }
    ImGui::SetNextItemWidth(-1.0f);
    InputString("##find", m_hierarchyFilter);
    if (m_hierarchyFilter.empty() && !ImGui::IsItemActive()) {
        const ImVec2 at = ImGui::GetItemRectMin();
        ImGui::GetWindowDrawList()->AddText({at.x + 6.0f, at.y + 3.0f}, ImGui::GetColorU32(ImGuiCol_TextDisabled),
                                            "Find by name, kind or folder");
    }
    if (!m_isolated.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "Isolated: %zu shown alone (Shift+H)", m_isolated.size());
    }
    ImGui::BeginChild("entities");
    const bool control = ImGui::GetIO().KeyCtrl;
    if (!m_hierarchyFilter.empty()) {
        // A flat list of what matches.
        const std::string filter = Lower(m_hierarchyFilter);
        for (const scene::LevelEntity& entity : m_level.entities) {
            const std::string name = entity.name.empty() ? std::format("Entity {}", entity.id) : entity.name;
            const std::string kind = entity.game ? entity.game->kind : std::string();
            if (Lower(name).find(filter) == std::string::npos && Lower(kind).find(filter) == std::string::npos &&
                Lower(entity.editing.folder).find(filter) == std::string::npos) {
                continue;
            }
            const std::string label = std::format("[{}] {}##{}", KindLabel(entity), name, entity.id);
            if (ImGui::Selectable(label.c_str(), IsSelected(entity.id))) {
                if (control) {
                    ToggleSelect(entity.id);
                } else {
                    Select(entity.id);
                }
                m_scrollToSelection = false;
            }
        }
        ImGui::EndChild();
        ImGui::End();
        return;
    }
    // Children by parent, in the file's order.
    std::map<std::uint64_t, std::size_t> slots;
    for (std::size_t index = 0; index < m_level.entities.size(); ++index) {
        slots.emplace(m_level.entities[index].id, index);
    }
    std::vector<std::vector<std::uint64_t>> children(m_level.entities.size() + 1);
    std::vector<std::size_t> indexOf;
    indexOf.reserve(m_level.entities.size());
    for (const scene::LevelEntity& entity : m_level.entities) {
        indexOf.push_back(slots[entity.id]);
        const auto parent = slots.find(entity.parent);
        const std::size_t slot = parent == slots.end() ? m_level.entities.size() : parent->second;
        children[slot].push_back(entity.id);
    }
    // The entities at the root, by folder (ADR-0107).
    std::map<std::string, std::vector<std::uint64_t>> byFolder;
    for (const std::uint64_t id : children.back()) {
        byFolder[TidyFolder(m_level.Find(id)->editing.folder)].push_back(id);
    }
    DrawFolder("", FoldersOf(m_level), byFolder, children, indexOf);
    // The space below the entities takes one dropped there to the root, out of its folder.
    const ImVec2 space = ImGui::GetContentRegionAvail();
    ImGui::InvisibleButton("root", ImVec2(std::max(space.x, 1.0f), std::max(space.y, 20.0f)));
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DAIDALOS_ENTITY")) {
            const std::uint64_t dropped = *static_cast<const std::uint64_t*>(payload->Data);
            if (const scene::LevelEntity* entity = m_level.Find(dropped);
                entity != nullptr && entity->parent == 0 && !entity->editing.folder.empty()) {
                MoveToFolder({dropped}, "");
            } else {
                Reparent(dropped, 0);
            }
        }
        ImGui::EndDragDropTarget();
    }
    if (ImGui::IsItemClicked()) {
        Select(0);
    }
    m_scrollToSelection = false;
    ImGui::EndChild();
    ImGui::End();
}

void Editor::DrawFolder(const std::string& folder, const std::vector<std::string>& folders,
                        const std::map<std::string, std::vector<std::uint64_t>>& byFolder,
                        const std::vector<std::vector<std::uint64_t>>& children,
                        const std::vector<std::size_t>& indexOf) {
    for (const std::string& inner : folders) {
        if (ParentFolder(inner) != folder) {
            continue;
        }
        // What the folder holds, at any depth, for its own hide and lock buttons.
        std::vector<std::uint64_t> held;
        bool allHidden = true;
        bool allLocked = true;
        for (const scene::LevelEntity& entity : m_level.entities) {
            if (entity.parent == 0 && InFolder(TidyFolder(entity.editing.folder), inner)) {
                held.push_back(entity.id);
                allHidden = allHidden && entity.editing.hidden;
                allLocked = allLocked && entity.editing.locked;
            }
        }
        // A pick in the view opens the folders down to what was picked.
        if (m_scrollToSelection && m_selected != 0) {
            std::uint64_t root = m_selected;
            for (const scene::LevelEntity* entity = m_level.Find(root); entity != nullptr && entity->parent != 0;
                 entity = m_level.Find(entity->parent)) {
                root = entity->parent;
            }
            if (const scene::LevelEntity* top = m_level.Find(root);
                top != nullptr && InFolder(TidyFolder(top->editing.folder), inner)) {
                ImGui::SetNextItemOpen(true);
            }
        }
        const std::string leaf = inner.substr(inner.rfind('/') == std::string::npos ? 0 : inner.rfind('/') + 1);
        const bool open = ImGui::TreeNodeEx(std::format("[/] {}##folder:{}", leaf, inner).c_str(),
                                            ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth |
                                                ImGuiTreeNodeFlags_AllowOverlap);
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DAIDALOS_ENTITY")) {
                MoveToFolder({*static_cast<const std::uint64_t*>(payload->Data)}, inner);
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Select what it holds")) {
                Select(0);
                for (const std::uint64_t held_ : held) {
                    ToggleSelect(held_);
                }
            }
            if (ImGui::MenuItem(allHidden ? "Show what it holds" : "Hide what it holds")) {
                SetHidden(held, !allHidden);
            }
            if (ImGui::MenuItem(allLocked ? "Unlock what it holds" : "Lock what it holds")) {
                SetLocked(held, !allLocked);
            }
            if (ImGui::MenuItem("Take everything out of it")) {
                MoveToFolder(held, ParentFolder(inner));
            }
            ImGui::EndPopup();
        }
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            // A click on a folder selects what it holds.
            Select(0);
            for (const std::uint64_t id : held) {
                ToggleSelect(id);
            }
        }
        bool toggleHidden = false;
        bool toggleLocked = false;
        RowToggles("folder:" + inner, !held.empty() && allHidden, !held.empty() && allLocked, toggleHidden,
                   toggleLocked);
        if (toggleHidden) {
            SetHidden(held, !allHidden);
        }
        if (toggleLocked) {
            SetLocked(held, !allLocked);
        }
        if (open) {
            DrawFolder(inner, folders, byFolder, children, indexOf);
            ImGui::TreePop();
        }
    }
    if (const auto found = byFolder.find(folder); found != byFolder.end()) {
        for (const std::uint64_t id : found->second) {
            DrawHierarchyNode(id, children, indexOf);
        }
    }
}

void Editor::DrawHierarchyNode(std::uint64_t id, const std::vector<std::vector<std::uint64_t>>& children,
                               const std::vector<std::size_t>& indexOf) {
    const auto found = std::ranges::find(m_level.entities, id, &scene::LevelEntity::id);
    const std::size_t slot = static_cast<std::size_t>(found - m_level.entities.begin());
    const scene::LevelEntity& entity = *found;
    const std::vector<std::uint64_t>& mine = children[slot];
    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap;
    if (mine.empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (IsSelected(id)) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    // A pick in the view opens the way to what was picked.
    if (m_scrollToSelection && id != m_selected && !mine.empty() && IsAncestor(m_level, id, m_selected)) {
        ImGui::SetNextItemOpen(true);
    }
    const bool hidden = entity.editing.hidden;
    const bool locked = entity.editing.locked;
    const bool shownElsewhere = HiddenInEditor(m_level, id) && !hidden;
    const std::string label = std::format("[{}] {}##{}", KindLabel(entity),
                                          entity.name.empty() ? std::format("Entity {}", id) : entity.name, id);
    if (hidden || shownElsewhere) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    }
    const bool open = ImGui::TreeNodeEx(label.c_str(), flags);
    if (hidden || shownElsewhere) {
        ImGui::PopStyleColor();
    }
    if (m_scrollToSelection && id == m_selected) {
        ImGui::SetScrollHereY(0.4f);
    }
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        if (ImGui::GetIO().KeyCtrl) {
            ToggleSelect(id);
        } else {
            Select(id);
        }
        m_scrollToSelection = false;
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        FrameSelection();
    }
    if (ImGui::BeginPopupContextItem()) {
        // A right click acts on the selection, which the row joins if it is not in it.
        if (!IsSelected(id)) {
            Select(id);
        }
        DrawEntityMenu();
        ImGui::EndPopup();
    }
    // Dragged onto another entity, it goes under it; onto a folder, into it.
    if (ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload("DAIDALOS_ENTITY", &id, sizeof(id));
        ImGui::Text("Move %s under...", label.substr(0, label.find("##")).c_str());
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DAIDALOS_ENTITY")) {
            Reparent(*static_cast<const std::uint64_t*>(payload->Data), id);
        }
        ImGui::EndDragDropTarget();
    }
    bool toggleHidden = false;
    bool toggleLocked = false;
    RowToggles(std::to_string(id), hidden, locked, toggleHidden, toggleLocked);
    if (toggleHidden) {
        SetHidden({id}, !hidden);
    }
    if (toggleLocked) {
        SetLocked({id}, !locked);
    }
    if (open && !mine.empty()) {
        for (const std::uint64_t child : mine) {
            DrawHierarchyNode(child, children, indexOf);
        }
        ImGui::TreePop();
    }
}

void Editor::DrawInspector() {
    if (!ImGui::Begin("Inspector")) {
        ImGui::End();
        return;
    }
    scene::LevelEntity* selected = Selected();
    if (selected == nullptr) {
        ImGui::TextDisabled("Nothing selected. Click in the viewport or in the hierarchy.");
        ImGui::End();
        return;
    }
    scene::LevelEntity edited = *selected;
    ImGui::Text("Entity %llu", static_cast<unsigned long long>(edited.id));
    if (m_selection.size() > 1) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%zu selected: the gizmo moves them all, this edits the last)", m_selection.size());
    }
    bool changed = InputString("Name", edited.name);

    if (m_simulating && m_instance != nullptr) {
        // What the simulation made of it, which the level keeps only with K.
        if (const ecs::Entity live = m_instance->Find(edited.id); !live.IsNull()) {
            const glm::vec3 at(m_scene->World().Get<ecs::WorldTransform>(live).matrix[3]);
            ImGui::TextColored(ImVec4(0.5f, 0.85f, 1.0f, 1.0f), "Simulated at %.2f %.2f %.2f", at.x, at.y, at.z);
        }
    }
    ImGui::BeginDisabled(m_simulating);
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::DragFloat3("Position (m)", &edited.transform.translation.x, 0.01f);
        if (m_eulerOf != edited.id || !ImGui::IsAnyItemActive()) {
            m_euler = glm::degrees(glm::eulerAngles(edited.transform.rotation));
            m_eulerOf = edited.id;
        }
        if (ImGui::DragFloat3("Rotation (deg)", &m_euler.x, 0.25f)) {
            edited.transform.rotation = glm::normalize(glm::quat(glm::radians(m_euler)));
            changed = true;
        }
        changed |= ImGui::DragFloat3("Scale", &edited.transform.scale.x, 0.01f);
    }
    if (!edited.prefab.empty() && ImGui::CollapsingHeader("Prefab", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= InputString("File", edited.prefab);
        if (ImGui::Button("Open the prefab")) {
            m_pendingOpenPrefab = m_instance != nullptr ? m_instance->Resolve(edited.prefab) : std::string{};
        }
        ImGui::SameLine();
        if (ImGui::Button("Unpack")) {
            m_pendingUnpack = edited.id;
        }
        ImGui::TextDisabled("The prefab's file changes every copy; the parts below change this copy only.");
        changed |= InspectPrefabParts(edited);
    }
    changed |= InspectComponents(edited);
    if ((edited.particles || edited.ribbon) && m_instance != nullptr) {
        if (ImGui::Button("Edit its effect")) {
            m_pendingEffect = m_instance->Resolve(edited.particles ? edited.particles->effect : edited.ribbon->effect);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Opens its effect file in the Effect panel: every emitter of it changes as it is edited");
        }
    }
    if (edited.trigger) {
        if (ImGui::Button(edited.trigger->sequence.empty() ? "Make its sequence" : "Edit its sequence")) {
            m_pendingSequence = edited.id;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Opens the timeline of what the trigger plays (ADR-0079)");
        }
    }
    ImGui::EndDisabled();
    if (changed) {
        Apply(edited, std::format("Edit {}", EntityLabel(edited)));
    }
    ImGui::End();
}

void Editor::DrawEnvironment() {
    if (!ImGui::Begin("Environment")) {
        ImGui::End();
        return;
    }
    scene::LevelEnvironment environment = m_level.environment;
    if (InspectEnvironment(environment)) {
        Change("Edit the environment", [&environment](scene::Level& level) { level.environment = environment; });
    }
    ImGui::End();
}

void Editor::DrawStatus(app::Application& app, const core::FrameTime& time) {
    if (ImGui::Begin("Status")) {
        const renderer::FrameStats stats = app.Renderer().Stats();
        ImGui::Text("%.1f ms   %u draws   %zu entities   camera %.1f %.1f %.1f, %.0f m/s",
                    time.unclampedDelta.count() * 1000.0, stats.draws, m_level.entities.size(), m_camera.position.x,
                    m_camera.position.y, m_camera.position.z, m_camera.speed);
        ImGui::TextUnformatted(m_status.c_str());
        ImGui::TextDisabled("Right mouse: fly with WASD, Q and E. W E R: move, rotate, scale. F: frame. "
                            "Ctrl+Z, Ctrl+Y: undo, redo. F5: play. F1: every shortcut.");
    }
    ImGui::End();
}

bool Editor::MouseInViewport() const {
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    return mouse.x >= m_viewportMin.x && mouse.y >= m_viewportMin.y && mouse.x < m_viewportMax.x &&
           mouse.y < m_viewportMax.y;
}

void Editor::UpdateCamera(app::Application& app, float seconds) {
    const input::InputState& input = app.Input();
    const ImGuiIO& io = ImGui::GetIO();
    Pane& live = m_panes[m_active];
    if (live.kind != ViewKind::Perspective) {
        // An axis view pans with the right or middle button and zooms with the wheel, about the mouse.
        m_flying = false;
        if (!MouseInViewport() || io.WantCaptureMouse) {
            return;
        }
        if (input.IsDown(platform::MouseButton::Right) || input.IsDown(platform::MouseButton::Middle)) {
            live.ortho.focus = Panned(live.kind, live.ortho, input.MouseDelta(), live.rect.Size().y);
        }
        if (input.WheelDelta().y != 0.0f) {
            const auto [origin, direction] = RayAt(glm::vec2(io.MousePos.x, io.MousePos.y));
            const glm::vec3 under = origin + direction * glm::dot(live.ortho.focus - origin, direction);
            const float before = live.ortho.halfHeight;
            live.ortho.halfHeight = Zoomed(before, input.WheelDelta().y);
            live.ortho.focus = under + (live.ortho.focus - under) * (live.ortho.halfHeight / before);
        }
        return;
    }
    if (!m_flying && input.WasPressed(platform::MouseButton::Right) && MouseInViewport() && !io.WantCaptureMouse) {
        m_flying = true;
        app.MainWindow().SetMouseCaptured(true);
    }
    if (m_flying && !input.IsDown(platform::MouseButton::Right)) {
        m_flying = false;
        app.MainWindow().SetMouseCaptured(false);
    }
    if (m_flying) {
        const glm::vec2 delta = input.MouseDelta();
        m_camera.yaw -= delta.x * kLookSpeed;
        m_camera.pitch = std::clamp(m_camera.pitch - delta.y * kLookSpeed, -1.55f, 1.55f);
        if (input.WheelDelta().y != 0.0f) {
            m_camera.speed = std::clamp(m_camera.speed * std::pow(1.2f, input.WheelDelta().y), 0.5f, 200.0f);
        }
        const glm::quat rotation = math::FromYawPitchRoll(m_camera.yaw, m_camera.pitch, 0.0f);
        glm::vec3 move{0.0f};
        move += input.IsDown(platform::Key::W) ? math::kForward : glm::vec3(0.0f);
        move -= input.IsDown(platform::Key::S) ? math::kForward : glm::vec3(0.0f);
        move += input.IsDown(platform::Key::D) ? math::kRight : glm::vec3(0.0f);
        move -= input.IsDown(platform::Key::A) ? math::kRight : glm::vec3(0.0f);
        glm::vec3 world = rotation * move;
        world += input.IsDown(platform::Key::E) ? math::kUp : glm::vec3(0.0f);
        world -= input.IsDown(platform::Key::Q) ? math::kUp : glm::vec3(0.0f);
        const float boost = input.IsDown(platform::Key::LeftShift) ? 4.0f : 1.0f;
        if (glm::length(world) > 0.0f) {
            m_camera.position += glm::normalize(world) * m_camera.speed * boost * seconds;
        }
    } else if (MouseInViewport() && !io.WantCaptureMouse) {
        // The wheel walks forward and back; the middle button pans.
        if (input.WheelDelta().y != 0.0f) {
            m_camera.position += m_camera.Forward() * input.WheelDelta().y * std::max(m_camera.speed * 0.25f, 0.5f);
        }
        if (input.IsDown(platform::MouseButton::Middle)) {
            const glm::quat rotation = math::FromYawPitchRoll(m_camera.yaw, m_camera.pitch, 0.0f);
            const glm::vec2 delta = input.MouseDelta();
            m_camera.position +=
                rotation * (math::kRight * -delta.x + math::kUp * delta.y) * (0.0015f * m_camera.speed);
        }
    }
}

glm::mat4 Editor::Projection() const {
    const glm::vec2 extent = glm::max(m_viewportMax - m_viewportMin, glm::vec2(1.0f));
    const EditorCamera eye = Eye();
    return glm::perspective(eye.verticalFov, extent.x / extent.y, eye.nearPlane, eye.farPlane);
}

std::optional<glm::vec2> Editor::Project(const glm::vec3& point) const {
    const EditorCamera eye = Eye();
    const glm::vec4 clip = Projection() * eye.View() * glm::vec4(point, 1.0f);
    if (clip.w <= eye.nearPlane) {
        return std::nullopt;
    }
    const glm::vec2 ndc = glm::vec2(clip) / clip.w;
    const glm::vec2 extent = m_viewportMax - m_viewportMin;
    return m_viewportMin + glm::vec2((ndc.x * 0.5f + 0.5f) * extent.x, (0.5f - ndc.y * 0.5f) * extent.y);
}

void Editor::Line3D(ImDrawList* draw, const glm::vec3& from, const glm::vec3& to, unsigned color,
                    float thickness) const {
    // In pieces of about half a meter, at most 16, each as faint as the scene makes its middle.
    const int pieces = std::clamp(static_cast<int>(glm::distance(from, to) * 2.0f), 1, 16);
    const auto alpha = static_cast<float>((color >> IM_COL32_A_SHIFT) & 0xFFU);
    std::optional<glm::vec2> previous = Project(from);
    for (int piece = 1; piece <= pieces; ++piece) {
        const glm::vec3 a = glm::mix(from, to, static_cast<float>(piece - 1) / static_cast<float>(pieces));
        const glm::vec3 b = glm::mix(from, to, static_cast<float>(piece) / static_cast<float>(pieces));
        const std::optional<glm::vec2> next = Project(b);
        if (previous && next) {
            const float shown = m_depth.Visibility((a + b) * 0.5f);
            const auto faded = static_cast<ImU32>(std::clamp(alpha * shown, 0.0f, 255.0f)) << IM_COL32_A_SHIFT;
            draw->AddLine({previous->x, previous->y}, {next->x, next->y}, (color & ~IM_COL32_A_MASK) | faded,
                          thickness);
        }
        previous = next;
    }
}

void Editor::Box3D(ImDrawList* draw, const glm::mat4& matrix, const glm::vec3& min, const glm::vec3& max,
                   unsigned color, float thickness) const {
    for (const auto& edge : kCubeEdges) {
        Line3D(draw, math::TransformPoint(matrix, CubeCorner(edge[0], min, max)),
               math::TransformPoint(matrix, CubeCorner(edge[1], min, max)), color, thickness);
    }
}

void Editor::DrawReach(ImDrawList* draw, const scene::LevelEntity& entity, const glm::mat4& matrix,
                       unsigned color) const {
    if (!entity.light || entity.light->type == renderer::LightType::Directional) {
        return;
    }
    const glm::vec3 position(matrix[3]);
    const float range = entity.light->range;
    constexpr int kSteps = 32;
    if (entity.light->type == renderer::LightType::Spot) {
        // The outer cone, to the range: its rim and four lines from the light.
        const glm::vec3 axis = glm::normalize(glm::vec3(matrix * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
        const glm::vec3 side = glm::normalize(std::abs(axis.y) < 0.99f ? glm::cross(axis, glm::vec3(0.0f, 1.0f, 0.0f))
                                                                       : glm::cross(axis, glm::vec3(1.0f, 0.0f, 0.0f)));
        const glm::vec3 up = glm::cross(side, axis);
        const float angle = std::clamp(entity.light->outerAngle, 0.01f, 1.55f);
        const float radius = std::sin(angle) * range;
        const glm::vec3 center = position + axis * range * std::cos(angle);
        glm::vec3 previous = center + side * radius;
        for (int step = 1; step <= kSteps; ++step) {
            const float turn = static_cast<float>(step) / kSteps * glm::two_pi<float>();
            const glm::vec3 next = center + (side * std::cos(turn) + up * std::sin(turn)) * radius;
            Line3D(draw, previous, next, color, 1.0f);
            if (step % (kSteps / 4) == 0) {
                Line3D(draw, position, next, color, 1.0f);
            }
            previous = next;
        }
        return;
    }
    for (int plane = 0; plane < 3; ++plane) {
        glm::vec3 previous{0.0f};
        for (int step = 0; step <= kSteps; ++step) {
            const float angle = static_cast<float>(step) / kSteps * glm::two_pi<float>();
            glm::vec3 offset{0.0f};
            offset[plane] = std::cos(angle) * range;
            offset[(plane + 1) % 3] = std::sin(angle) * range;
            if (step > 0) {
                Line3D(draw, position + previous, position + offset, color, 1.0f);
            }
            previous = offset;
        }
    }
}

void Editor::DrawMarkers() {
    if (m_instance == nullptr) {
        return;
    }
    ecs::World& world = m_scene->World();
    ecs::PropagateTransforms(world);
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    draw->PushClipRect({m_viewportMin.x, m_viewportMin.y}, {m_viewportMax.x, m_viewportMax.y}, true);
    for (const scene::LevelEntity& entity : m_level.entities) {
        const ecs::Entity created = m_instance->Find(entity.id);
        if (created.IsNull()) {
            continue;
        }
        const bool selected = IsSelected(entity.id);
        if (!selected && !m_show.Shows(CategoryOf(entity))) {
            continue;
        }
        const glm::mat4& matrix = world.Get<ecs::WorldTransform>(created).matrix;
        const float distance = glm::distance(glm::vec3(matrix[3]), Focus());
        const bool reaching = m_show.lightReach && entity.light.has_value();
        if (!selected && distance > kMarkerReach && !reaching) {
            continue;
        }
        const float fade = selected ? 1.0f : std::clamp(1.5f - distance / kMarkerReach * 1.5f, 0.15f, 1.0f);
        const ImU32 alpha = static_cast<ImU32>(fade * 255.0f) << 24;
        const ImU32 color = selected ? IM_COL32(255, 220, 60, 255) : ((MarkerColor(entity) & 0x00FFFFFFU) | alpha);
        if (entity.sound && selected) {
            // Where it is heard in full, and where it fades out.
            for (const float radius : {entity.sound->minDistance, entity.sound->maxDistance}) {
                glm::vec3 previous{0.0f};
                for (int step = 0; step <= 32; ++step) {
                    const float angle = static_cast<float>(step) / 32.0f * glm::two_pi<float>();
                    const glm::vec3 offset(std::cos(angle) * radius, 0.0f, std::sin(angle) * radius);
                    if (step > 0) {
                        Line3D(draw, glm::vec3(matrix[3]) + previous, glm::vec3(matrix[3]) + offset,
                               IM_COL32(150, 120, 240, 160), 1.0f);
                    }
                    previous = offset;
                }
            }
        }
        if (entity.light && (selected || m_show.lightReach)) {
            DrawReach(draw, entity, matrix,
                      selected ? IM_COL32(255, 220, 60, 110) : ((MarkerColor(entity) & 0x00FFFFFFU) | 0x50000000U));
        }
        if (!selected && distance > kMarkerReach) {
            continue;
        }
        // Volumes show their box; a light shows where it points.
        if (entity.fog || entity.wind || entity.weather || entity.lightning || entity.decal || entity.wet ||
            entity.trigger || entity.cell) {
            Box3D(draw, matrix, glm::vec3(-0.5f), glm::vec3(0.5f),
                  (color & 0x00FFFFFFU) | (selected ? 0xFF000000U : static_cast<ImU32>(fade * 90.0f) << 24),
                  selected ? 2.0f : 1.0f);
        }
        const glm::vec3 position(matrix[3]);
        if (entity.light && entity.light->type != renderer::LightType::Point) {
            const glm::vec3 axis = glm::normalize(glm::vec3(matrix * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
            Line3D(draw, position, position + axis * (selected ? std::min(entity.light->range, 5.0f) : 0.6f), color,
                   1.5f);
        }
        if (entity.model.empty()) {
            if (const std::optional<glm::vec2> at = Project(position)) {
                const float shown = selected ? 1.0f : m_depth.Visibility(position);
                const auto scaled = [shown](ImU32 value) {
                    const float a = static_cast<float>((value >> IM_COL32_A_SHIFT) & 0xFFU) * shown;
                    return (value & ~IM_COL32_A_MASK) | (static_cast<ImU32>(a) << IM_COL32_A_SHIFT);
                };
                draw->AddCircleFilled({at->x, at->y}, kMarkerRadius,
                                      scaled(IM_COL32(20, 20, 20, static_cast<int>(fade * 200.0f))));
                draw->AddCircle({at->x, at->y}, kMarkerRadius, scaled(color), 0, selected ? 2.5f : 1.5f);
                const char* label = KindLabel(entity);
                const ImVec2 size = ImGui::CalcTextSize(label);
                draw->AddText({at->x - size.x * 0.5f, at->y - size.y * 0.5f}, scaled(color), label);
            }
        } else if (selected) {
            const Bounds bounds = m_picker.WorldBounds(world, created);
            if (!bounds.Empty()) {
                Box3D(draw, glm::mat4(1.0f), bounds.min, bounds.max, color, 1.5f);
            }
        }
    }
    draw->PopClipRect();
}

void Editor::DrawShownExtras() {
    if (m_instance == nullptr || (!m_show.collision && !m_show.navigation && !m_show.portals)) {
        return;
    }
    ecs::World& world = m_scene->World();
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    draw->PushClipRect({m_viewportMin.x, m_viewportMin.y}, {m_viewportMax.x, m_viewportMax.y}, true);
    std::vector<Bounds> rooms;
    for (const scene::LevelEntity& entity : m_level.entities) {
        const ecs::Entity created = m_instance->Find(entity.id);
        if (created.IsNull()) {
            continue;
        }
        const glm::mat4& matrix = world.Get<ecs::WorldTransform>(created).matrix;
        if (m_show.collision && entity.body && glm::distance(glm::vec3(matrix[3]), Focus()) < 80.0f) {
            // Static boxes in cyan, bodies that move in magenta.
            const ImU32 color = entity.body->mass <= 0.0f ? IM_COL32(60, 230, 230, 200) : IM_COL32(230, 80, 230, 200);
            Box3D(draw, matrix, entity.body->center - entity.body->size * 0.5f,
                  entity.body->center + entity.body->size * 0.5f, color, 1.5f);
        }
        if (m_show.portals && entity.cell && entity.cell->enabled) {
            rooms.push_back(UnitBoxBounds(matrix));
        }
    }
    for (const Bounds& opening : Openings(rooms)) {
        Box3D(draw, glm::mat4(1.0f), opening.min, opening.max, IM_COL32(255, 90, 200, 230), 2.5f);
    }
    if (m_show.navigation) {
        if (!m_navigation) {
            BuildNavigation();
        }
        const glm::vec3 lift(0.0f, 0.05f, 0.0f);
        for (std::size_t edge = 0; edge + 1 < m_navigationEdges.size(); edge += 2) {
            const glm::vec3 middle = (m_navigationEdges[edge] + m_navigationEdges[edge + 1]) * 0.5f;
            if (glm::distance(middle, Focus()) < 60.0f) {
                Line3D(draw, m_navigationEdges[edge] + lift, m_navigationEdges[edge + 1] + lift,
                       IM_COL32(80, 220, 120, 170), 1.0f);
            }
        }
    }
    draw->PopClipRect();
}

void Editor::BuildNavigation() {
    m_navigationEdges.clear();
    m_navigation.reset();
    if (m_scene == nullptr) {
        return;
    }
    // Every model's triangles where it stands, as the game gives its level's to Recast (ADR-0069). Doors are not
    // known here: they count as walls.
    ecs::World& world = m_scene->World();
    ecs::PropagateTransforms(world);
    std::vector<glm::vec3> positions;
    std::vector<std::uint32_t> indices;
    world.Each<scene::ModelInstance>([&](ecs::Entity entity, const scene::ModelInstance& placed) {
        if (placed.model == nullptr) {
            return;
        }
        const glm::mat4& matrix = world.Get<ecs::WorldTransform>(entity).matrix;
        for (const scene::Model::Part& part : placed.model->parts) {
            const auto first = static_cast<std::uint32_t>(positions.size());
            for (const glm::vec3& position : part.positions) {
                positions.push_back(math::TransformPoint(matrix, position));
            }
            for (const std::uint32_t index : part.indices) {
                indices.push_back(first + index);
            }
        }
    });
    core::Result<navigation::NavMesh> mesh = navigation::NavMesh::Build(positions, indices);
    if (!mesh) {
        m_status = core::Format(mesh.error());
        return;
    }
    m_navigationEdges = mesh->Edges();
    m_status = std::format("Navigation: {} polygons in {:.0f} ms", mesh->PolygonCount(), mesh->BuildSeconds() * 1000.0);
    m_navigation = std::move(*mesh);
}

void Editor::FilterView(renderer::SceneView& view) const {
    if (!m_show.decals) {
        view.decals.clear();
    }
    if (!m_show.particles) {
        view.particleSpawns.clear();
        view.ribbons.clear();
    }
    if (!m_show.fog) {
        view.fogVolumes.clear();
        view.atmosphere.fog.density = 0.0f;
    }
}

EditorCamera Editor::Eye() const {
    const Pane& pane = m_panes[m_drawPane];
    return pane.kind == ViewKind::Perspective ? m_camera : AxisEye(pane.kind, pane.ortho);
}

glm::vec3 Editor::Focus() const {
    const Pane& pane = m_panes[m_drawPane];
    return pane.kind == ViewKind::Perspective ? m_camera.position : pane.ortho.focus;
}

glm::vec3 Editor::InFront() const {
    const Pane& pane = m_panes[m_drawPane];
    return pane.kind == ViewKind::Perspective ? m_camera.position + m_camera.Forward() * 4.0f : pane.ortho.focus;
}

void Editor::UsePane(std::size_t pane) {
    m_drawPane = pane;
    m_viewportMin = m_panes[pane].rect.min;
    m_viewportMax = m_panes[pane].rect.max;
    // Overlays of the pane drawn live meet the live depth; the others meet the depth of their last image.
    if (pane == m_active) {
        const std::optional<renderer::DepthPicture>& depth = m_app->Renderer().LatestDepth();
        m_depth = DepthTest(depth ? &*depth : nullptr);
    } else {
        m_depth = DepthTest(m_panes[pane].depth ? &*m_panes[pane].depth : nullptr);
    }
}

void Editor::LayOutPanes() {
    if (m_split) {
        const std::array<Rect, 4> rects = SplitPanes(m_central, 4.0f);
        for (std::size_t pane = 0; pane < m_panes.size(); ++pane) {
            m_panes[pane].rect = rects[pane];
        }
    } else {
        m_panes[m_active].rect = m_central;
    }
}

void Editor::SetViewKind(ViewKind kind) {
    Pane& pane = m_panes[m_active];
    if (pane.kind == kind) {
        return;
    }
    if (kind != ViewKind::Perspective && pane.kind == ViewKind::Perspective) {
        // From the camera: the axis view looks at where it stands, cut just in front of it.
        pane.ortho.focus = m_camera.position;
        pane.ortho.cut = 0.5f;
    }
    pane.kind = kind;
    m_cutView = true;
    m_status = std::format("{} view{}", ViewName(kind),
                           kind == ViewKind::Perspective
                               ? std::string()
                               : std::string(": drag with the right or middle button to pan, the wheel zooms, "
                                             "[ and ] move the cut"));
}

void Editor::SetSplit(bool split) {
    m_split = split;
    m_cutView = true;
    m_switchTo.reset();
    m_status = split ? "Four views: click one to draw it live; the others keep their last image" : "One view";
}

bool Editor::UpdatePanes(app::Application& app) {
    // A pane chosen last frame is drawn live from now on; its own last image was captured as it was drawn.
    if (m_switchTo) {
        m_active = *std::exchange(m_switchTo, std::nullopt);
        m_cutView = true;
    }
    if (std::optional<renderer::CapturedFrame> captured = app.Renderer().TakeCapture()) {
        if (m_previewCapture) {
            // The camera preview's picture, cut from the live pane (ADR-0116).
            m_previewCapture = false;
            const rhi::TextureHandle old = m_panes[m_active].snapshot;
            m_panes[m_active].snapshot = {};
            TakeSnapshot(*captured, m_active);
            if (!m_previewTexture.IsNull()) {
                app.Renderer().Device().DestroyTexture(m_previewTexture);
            }
            m_previewTexture = std::exchange(m_panes[m_active].snapshot, old);
            m_previewSize = m_panes[m_active].rect.Size();
        } else if (m_captureFor) {
            TakeSnapshot(*captured, *std::exchange(m_captureFor, std::nullopt));
        }
    }
    if (!m_split) {
        return false;
    }
    // A press in another pane makes it the live one: this frame still draws the live pane, which is captured.
    const ImGuiIO& io = ImGui::GetIO();
    const glm::vec2 mouse(io.MousePos.x, io.MousePos.y);
    const input::InputState& input = app.Input();
    const bool pressed = input.WasPressed(platform::MouseButton::Left) ||
                         input.WasPressed(platform::MouseButton::Right) ||
                         input.WasPressed(platform::MouseButton::Middle);
    if (!pressed || io.WantCaptureMouse || m_switchTo || m_flying) {
        return false;
    }
    for (std::size_t pane = 0; pane < m_panes.size(); ++pane) {
        if (pane != m_active && m_panes[pane].rect.Contains(mouse)) {
            const std::optional<renderer::DepthPicture>& depth = app.Renderer().LatestDepth();
            m_panes[m_active].depth = depth;
            app.Renderer().RequestCapture();
            m_captureFor = m_active;
            m_switchTo = pane;
            return true;
        }
    }
    return false;
}

void Editor::TakeSnapshot(const renderer::CapturedFrame& frame, std::size_t pane) {
    if (frame.width == 0 || frame.height == 0 || frame.display.empty() || m_app == nullptr) {
        return;
    }
    // The pane's rectangle in the captured window, in pixels.
    const platform::Extent size = m_app->MainWindow().Size();
    const float scaleX = size.width > 0 ? static_cast<float>(frame.width) / static_cast<float>(size.width) : 1.0f;
    const float scaleY = size.height > 0 ? static_cast<float>(frame.height) / static_cast<float>(size.height) : 1.0f;
    const Rect& rect = m_panes[pane].rect;
    const auto left =
        static_cast<std::uint32_t>(std::clamp(rect.min.x * scaleX, 0.0f, static_cast<float>(frame.width)));
    const auto top =
        static_cast<std::uint32_t>(std::clamp(rect.min.y * scaleY, 0.0f, static_cast<float>(frame.height)));
    const auto right =
        static_cast<std::uint32_t>(std::clamp(rect.max.x * scaleX, 0.0f, static_cast<float>(frame.width)));
    const auto bottom =
        static_cast<std::uint32_t>(std::clamp(rect.max.y * scaleY, 0.0f, static_cast<float>(frame.height)));
    if (right <= left + 1 || bottom <= top + 1) {
        return;
    }
    const std::uint32_t width = right - left;
    const std::uint32_t height = bottom - top;
    std::vector<std::uint8_t> pixels(std::size_t{width} * height * 4);
    for (std::uint32_t row = 0; row < height; ++row) {
        const std::size_t from = (std::size_t{top + row} * frame.width + left) * 4;
        std::copy_n(frame.display.begin() + static_cast<std::ptrdiff_t>(from), std::size_t{width} * 4,
                    pixels.begin() + static_cast<std::ptrdiff_t>(std::size_t{row} * width * 4));
    }
    rhi::Device& device = m_app->Renderer().Device();
    core::Result<rhi::TextureHandle> texture =
        device.CreateTexture({.extent = {width, height},
                              .format = rhi::Format::Rgba8Unorm,
                              .usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDst,
                              .name = "Editor pane"});
    if (!texture) {
        DAI_LOG_WARNING(kModule, "{}", core::Format(texture.error()));
        return;
    }
    if (core::Result<void> uploaded = device.UploadTexture(*texture, std::as_bytes(std::span(pixels))); !uploaded) {
        DAI_LOG_WARNING(kModule, "{}", core::Format(uploaded.error()));
        device.DestroyTexture(*texture);
        return;
    }
    if (!m_panes[pane].snapshot.IsNull()) {
        device.DestroyTexture(m_panes[pane].snapshot);
    }
    m_panes[pane].snapshot = *texture;
}

void Editor::DrawPaneFrame(std::size_t pane) {
    const Pane& shown = m_panes[pane];
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    if (pane != m_active) {
        if (!shown.snapshot.IsNull()) {
            draw->AddImage(
                static_cast<ImTextureID>(debugui::TextureId(m_app->Renderer().Device().BindlessIndex(shown.snapshot))),
                {shown.rect.min.x, shown.rect.min.y}, {shown.rect.max.x, shown.rect.max.y});
        } else {
            draw->AddRectFilled({shown.rect.min.x, shown.rect.min.y}, {shown.rect.max.x, shown.rect.max.y},
                                IM_COL32(14, 14, 16, 255));
            const char* hint = "Click to draw this view";
            const ImVec2 size = ImGui::CalcTextSize(hint);
            const glm::vec2 middle = (shown.rect.min + shown.rect.max) * 0.5f;
            draw->AddText({middle.x - size.x * 0.5f, middle.y - size.y * 0.5f}, IM_COL32(150, 150, 160, 255), hint);
        }
    }
    // The pane's name in its corner, with the cut of an axis view; the live pane's border is lit.
    std::string label(ViewName(shown.kind));
    if (shown.kind != ViewKind::Perspective) {
        label += std::format("  {:.0f} m across, cut {:.2f} m", shown.ortho.halfHeight * 2.0f, shown.ortho.cut);
    }
    if (m_split && pane != m_active) {
        label += "  (last image)";
    }
    draw->AddRectFilled({shown.rect.min.x + 6.0f, shown.rect.min.y + 6.0f},
                        {shown.rect.min.x + 14.0f + ImGui::CalcTextSize(label.c_str()).x,
                         shown.rect.min.y + 8.0f + ImGui::GetTextLineHeight()},
                        IM_COL32(0, 0, 0, 150), 3.0f);
    draw->AddText({shown.rect.min.x + 10.0f, shown.rect.min.y + 7.0f}, IM_COL32(220, 220, 230, 255), label.c_str());
    if (m_split) {
        draw->AddRect({shown.rect.min.x, shown.rect.min.y}, {shown.rect.max.x, shown.rect.max.y},
                      pane == m_active ? IM_COL32(255, 200, 60, 200) : IM_COL32(60, 60, 70, 255), 0.0f, 0, 1.5f);
    }
}

void Editor::DrawAxisGrid() {
    const Pane& pane = m_panes[m_drawPane];
    if (!m_show.grid || m_show.gameView || pane.kind == ViewKind::Perspective) {
        return;
    }
    // Lines across the pane in the plane through the focus, at least 10 pixels apart: a step of 1, 10 or 100 m,
    // brighter every ten steps.
    const glm::vec2 size = pane.rect.Size();
    const float metersPerPixel = 2.0f * pane.ortho.halfHeight / std::max(size.y, 1.0f);
    float step = 1.0f;
    while (step / metersPerPixel < 10.0f && step < 1000.0f) {
        step *= 10.0f;
    }
    const glm::vec3 right = ViewRight(pane.kind);
    const glm::vec3 up = ViewUp(pane.kind);
    const glm::vec2 half(size.x * 0.5f * metersPerPixel, pane.ortho.halfHeight);
    const float across = glm::dot(pane.ortho.focus, right);
    const float along = glm::dot(pane.ortho.focus, up);
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    draw->PushClipRect({pane.rect.min.x, pane.rect.min.y}, {pane.rect.max.x, pane.rect.max.y}, true);
    const auto screen = [&](float x, float y) {
        return ImVec2(pane.rect.min.x + (x - (across - half.x)) / metersPerPixel,
                      pane.rect.min.y + ((along + half.y) - y) / metersPerPixel);
    };
    for (float x = std::floor((across - half.x) / step) * step; x <= across + half.x; x += step) {
        const bool major = std::fmod(std::abs(std::round(x / step)), 10.0f) < 0.5f;
        draw->AddLine(screen(x, along - half.y), screen(x, along + half.y), IM_COL32(200, 200, 210, major ? 70 : 28),
                      major ? 1.5f : 1.0f);
    }
    for (float y = std::floor((along - half.y) / step) * step; y <= along + half.y; y += step) {
        const bool major = std::fmod(std::abs(std::round(y / step)), 10.0f) < 0.5f;
        draw->AddLine(screen(across - half.x, y), screen(across + half.x, y), IM_COL32(200, 200, 210, major ? 70 : 28),
                      major ? 1.5f : 1.0f);
    }
    draw->PopClipRect();
}

void Editor::DropToFloor() {
    if (m_instance == nullptr) {
        return;
    }
    ecs::World& world = m_scene->World();
    ecs::PropagateTransforms(world);
    const auto selected = [this](ecs::Entity entity) {
        for (std::uint64_t id = m_instance->IdOf(entity); id != 0;) {
            if (IsSelected(id)) {
                return true;
            }
            const scene::LevelEntity* above = m_level.Find(id);
            id = above != nullptr ? above->parent : 0;
        }
        return false;
    };
    std::vector<std::pair<std::uint64_t, glm::vec3>> offsets;
    for (const std::uint64_t id : SelectionRoots()) {
        const ecs::Entity entity = m_instance->Find(id);
        if (entity.IsNull() || LockedInEditor(m_level, id)) {
            continue;
        }
        Bounds bounds = m_picker.WorldBounds(world, entity);
        if (bounds.Empty()) {
            bounds.Add(glm::vec3(world.Get<ecs::WorldTransform>(entity).matrix[3]));
        }
        const glm::vec3 from((bounds.min.x + bounds.max.x) * 0.5f, bounds.min.y + 0.05f,
                             (bounds.min.z + bounds.max.z) * 0.5f);
        if (const std::optional<PickHit> hit = m_picker.Cast(world, from, glm::vec3(0.0f, -1.0f, 0.0f), selected);
            hit && hit->distance < 100.0f) {
            offsets.emplace_back(id, glm::vec3(0.0f, hit->position.y - bounds.min.y, 0.0f));
        }
    }
    if (offsets.empty()) {
        m_status = "Nothing under the selection to drop onto";
        return;
    }
    MoveBy(offsets, std::format("Drop {} entities to the floor", offsets.size()));
    m_status = std::format("Dropped {} entities onto what is under them", offsets.size());
}

Document Editor::Snapshot() const {
    return Document{.level = m_level,
                    .materials = m_materialValues,
                    .sequencePath = m_sequencePath,
                    .sequence = m_sequenceShadow,
                    .effectPath = m_effectPath,
                    .effectText = m_effectShadow};
}

void Editor::TrackPanelEdits() {
    // The material panel's edits of the frame: the state before them opens a step, unless one is open (ADR-0109).
    for (const MaterialPanel::Edit& edit : m_materials.TakeEdits()) {
        m_materialFirst.try_emplace(edit.key, edit.before);
        m_materialValues.try_emplace(edit.key, edit.before);
        m_history.Begin(Snapshot(), "Edit a material");
        m_materialValues[edit.key] = edit.after;
    }
    // The sequence panel's: compared with what was last seen of the same file.
    if (m_sequences.Path() != m_sequencePath) {
        m_sequencePath = m_sequences.Path();
        m_sequenceShadow = m_sequences.Sequence();
    } else if (m_sequences.Sequence() != m_sequenceShadow) {
        m_history.Begin(Snapshot(), "Edit the sequence");
        m_sequenceShadow = m_sequences.Sequence();
    }
    // The effect panel's, as its file was written (ADR-0114).
    if (m_effects.Path() != m_effectPath) {
        m_effectPath = m_effects.Path();
        m_effectShadow = m_effects.Text();
    } else if (m_effects.Text() != m_effectShadow) {
        m_history.Begin(Snapshot(), "Edit the effect");
        m_effectShadow = m_effects.Text();
    }
}

void Editor::GoTo(Document document) {
    const scene::Level before = std::move(m_level);
    m_level = std::move(document.level);
    SyncShown(before, m_isolated);
    std::erase_if(m_selection, [this](std::uint64_t id) { return m_level.Find(id) == nullptr; });
    m_selected = m_selection.empty() ? 0 : m_selection.back();
    m_eulerOf = 0;
    // Materials: each one edited takes the state's value, or what it had before its first edit.
    if (m_scene != nullptr) {
        std::set<MaterialKey> keys;
        for (const auto& [key, values] : m_materialValues) {
            keys.insert(key);
        }
        for (const auto& [key, values] : document.materials) {
            keys.insert(key);
        }
        for (const MaterialKey& key : keys) {
            const auto wanted = document.materials.find(key);
            const auto first = m_materialFirst.find(key);
            if (wanted != document.materials.end()) {
                m_materials.Restore(m_scene->Assets(), key, wanted->second);
            } else if (first != m_materialFirst.end()) {
                m_materials.Restore(m_scene->Assets(), key, first->second);
            }
        }
    }
    m_materialValues = std::move(document.materials);
    // What Restore did is the history's own move, not an edit to record.
    static_cast<void>(m_materials.TakeEdits());
    // The sequence: the state's file and cues, opened again if another is open now.
    if (!document.sequencePath.empty()) {
        if (document.sequencePath != m_sequences.Path()) {
            m_sequences.Open(m_assetRoot, document.sequencePath);
        }
        if (document.sequence != m_sequences.Sequence()) {
            m_sequences.SetSequence(document.sequence);
        }
    }
    m_sequencePath = m_sequences.Path();
    m_sequenceShadow = m_sequences.Sequence();
    // The effect: its text written back, the file opened again first if another is open now.
    if (!document.effectPath.empty()) {
        if (document.effectPath != m_effects.Path()) {
            m_effects.Open(m_assetRoot, document.effectPath);
        }
        if (document.effectText != m_effects.Text()) {
            m_effects.SetText(m_assetRoot, document.effectText);
        }
    }
    m_effectPath = m_effects.Path();
    m_effectShadow = m_effects.Text();
}

void Editor::DrawHistory() {
    if (!ImGui::Begin("History")) {
        ImGui::End();
        return;
    }
    // Every step that can be undone, oldest first; a click goes back to just after it.
    const std::vector<std::string> labels = m_history.UndoLabels();
    if (ImGui::Selectable("(as opened)", labels.empty())) {
        while (m_history.CanUndo()) {
            Undo();
        }
    }
    std::optional<std::size_t> backTo;
    for (std::size_t step = 0; step < labels.size(); ++step) {
        const std::string label = std::format("{}##step{}", labels[step], step);
        if (ImGui::Selectable(label.c_str(), step + 1 == labels.size())) {
            backTo = step;
        }
    }
    if (labels.empty()) {
        ImGui::TextDisabled("Every edit of the level, its materials and its sequence shows here.");
    }
    if (m_history.CanRedo()) {
        ImGui::TextDisabled("Redo (Ctrl+Y): %s", m_history.RedoLabel().c_str());
    }
    ImGui::End();
    if (backTo) {
        for (std::size_t undone = labels.size() - 1 - *backTo; undone > 0; --undone) {
            Undo();
        }
    }
}

bool Editor::TabDirty(const Tab& tab) const {
    return tab.history.Current() != tab.savedRevision;
}

void Editor::StashTab(Tab& tab) {
    tab.levelPath = std::move(m_levelPath);
    tab.level = std::move(m_level);
    tab.instance = std::move(m_instance);
    tab.cameraEntity = m_cameraEntity;
    tab.camera = m_camera;
    tab.history = std::move(m_history);
    tab.savedRevision = m_savedRevision;
    tab.autosavedRevision = m_autosavedRevision;
    tab.selection = std::move(m_selection);
    tab.selected = m_selected;
    tab.isolated = std::move(m_isolated);
    tab.materialValues = std::move(m_materialValues);
    tab.materialFirst = std::move(m_materialFirst);
    tab.panes = m_panes;
    for (Pane& pane : m_panes) {
        // A pane's last image belongs to the document it showed.
        pane.snapshot = {};
        pane.depth.reset();
    }
    m_history = History{};
    m_level = {};
    m_selection.clear();
    m_selected = 0;
    m_scene = nullptr;
}

void Editor::RestoreTab(Tab& tab) {
    m_levelPath = std::move(tab.levelPath);
    m_level = std::move(tab.level);
    m_instance = std::move(tab.instance);
    m_cameraEntity = tab.cameraEntity;
    m_camera = tab.camera;
    m_history = std::move(tab.history);
    m_savedRevision = tab.savedRevision;
    m_autosavedRevision = tab.autosavedRevision;
    m_selection = std::move(tab.selection);
    m_selected = tab.selected;
    m_isolated = std::move(tab.isolated);
    m_materialValues = std::move(tab.materialValues);
    m_materialFirst = std::move(tab.materialFirst);
    m_panes = tab.panes;
    tab.panes = {};
    m_scene = m_app->CurrentScene();
    if (m_scene != nullptr) {
        m_picker.Rebuild(m_scene->World());
    }
    m_navigation.reset();
    m_navigationEdges.clear();
    m_materials.Clear();
    m_followedSelection = ~std::uint64_t{0};
    m_eulerOf = 0;
    m_stroking = false;
    m_decalImages.clear();
    m_cutView = true;
    m_sinceAutosave = 0.0f;
}

void Editor::SwitchTab(std::size_t index) {
    if (index == m_activeTab || index >= m_tabs.size() || m_loading != nullptr) {
        return;
    }
    StopSimulating();
    m_history.Commit(Snapshot());
    Tab& from = *m_tabs[m_activeTab];
    Tab& to = *m_tabs[index];
    StashTab(from);
    from.scene = m_app->ExchangeScene(std::move(to.scene));
    if (from.scene != nullptr) {
        from.scene->Sounds().SetPaused(true);
    }
    m_activeTab = index;
    m_selectTab = true;
    RestoreTab(to);
    if (m_browser != nullptr) {
        m_browser->Open(std::filesystem::path(m_levelPath).parent_path().generic_string());
    }
    m_status = std::format("{}", m_levelPath);
    UpdateTitle(*m_app);
}

void Editor::CloseTab(std::size_t index) {
    if (index >= m_tabs.size() || m_tabs.size() < 2 || m_loading != nullptr) {
        return;
    }
    if (index == m_activeTab) {
        SwitchTab(index == 0 ? 1 : index - 1);
    }
    // What the tab holds goes: its entities before its scene.
    Tab& closed = *m_tabs[index];
    const std::string path = closed.levelPath;
    for (Pane& pane : closed.panes) {
        if (!pane.snapshot.IsNull()) {
            m_app->Renderer().Device().DestroyTexture(pane.snapshot);
        }
    }
    closed.instance.reset();
    closed.scene.reset();
    m_tabs.erase(m_tabs.begin() + static_cast<std::ptrdiff_t>(index));
    if (m_activeTab > index) {
        --m_activeTab;
    }
    m_status = std::format("Closed {}", path);
}

void Editor::DrawTabs() {
    // A row of tabs over the view, one per open document.
    ImGui::SetNextWindowPos({m_tabBar.min.x, m_tabBar.min.y});
    ImGui::SetNextWindowSize({m_tabBar.Size().x, m_tabBar.Size().y});
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 2.0f));
    if (ImGui::Begin("##documents", nullptr, flags) &&
        ImGui::BeginTabBar("documents", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyScroll)) {
        std::optional<std::size_t> chosen;
        std::optional<std::size_t> closing;
        for (std::size_t index = 0; index < m_tabs.size(); ++index) {
            const bool active = index == m_activeTab;
            const std::string& path = active ? m_levelPath : m_tabs[index]->levelPath;
            const bool dirty = active ? Dirty() : TabDirty(*m_tabs[index]);
            const std::string name = std::filesystem::path(path).filename().generic_string();
            const std::string label = std::format("{}{}###tab{}", name.empty() ? "(untitled)" : name, dirty ? " *" : "",
                                                  static_cast<const void*>(m_tabs[index].get()));
            bool open = true;
            ImGuiTabItemFlags itemFlags = ImGuiTabItemFlags_None;
            if (active && m_selectTab) {
                itemFlags |= ImGuiTabItemFlags_SetSelected;
            }
            if (ImGui::BeginTabItem(label.c_str(), m_tabs.size() > 1 ? &open : nullptr, itemFlags)) {
                if (!active && !m_selectTab) {
                    chosen = index;
                }
                ImGui::EndTabItem();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", path.c_str());
            }
            if (!open) {
                closing = index;
            }
        }
        m_selectTab = false;
        ImGui::EndTabBar();
        if (closing) {
            m_pendingCloseTab = *closing;
        } else if (chosen) {
            SwitchTab(*chosen);
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
    if (m_pendingCloseTab) {
        const std::size_t index = *std::exchange(m_pendingCloseTab, std::nullopt);
        // Unsaved changes are asked about in the tab being closed.
        SwitchTab(index);
        m_closingTab = index;
        Guard(*m_app, Pending::CloseTab);
    }
}

void Editor::StartSimulating() {
    if (m_scene == nullptr || m_simulating || m_game != nullptr) {
        return;
    }
    m_history.Commit(Snapshot());
    m_simulating = true;
    m_realtime = true;
    // Physics runs, and the camera stands in for the player: triggers it walks into play their scares.
    m_scene->SetPhysicsPaused(false);
    if (!m_cameraEntity.IsNull() && !m_scene->World().Has<scene::TriggerActivator>(m_cameraEntity)) {
        m_scene->World().Add<scene::TriggerActivator>(m_cameraEntity);
    }
    m_status = "Simulating: physics, effects and triggers run. Alt+S or Esc stops, K keeps where loose objects are, "
               "Ctrl+R pauses, F10 steps a frame";
}

void Editor::StopSimulating() {
    if (!m_simulating || m_scene == nullptr) {
        return;
    }
    m_simulating = false;
    m_stepFrames = 0;
    m_scene->SetPhysicsPaused(true);
    m_scene->World().Remove<scene::TriggerActivator>(m_cameraEntity);
    m_scene->StopCinematic();
    // Everything goes back as the level has it.
    RemakeScene();
    m_status = "Stopped simulating: the level is as it was";
}

void Editor::KeepSimulation() {
    if (!m_simulating || m_instance == nullptr) {
        return;
    }
    // Where physics left the loose objects, relative to what holds them, kept as one step (Unreal's K).
    ecs::World& world = m_scene->World();
    ecs::PropagateTransforms(world);
    std::vector<std::pair<std::uint64_t, math::Transform>> kept;
    for (const scene::LevelEntity& entity : m_level.entities) {
        if (!entity.body || entity.body->mass <= 0.0f) {
            continue;
        }
        const ecs::Entity created = m_instance->Find(entity.id);
        if (created.IsNull()) {
            continue;
        }
        const ecs::Entity parent = world.Parent(created);
        const glm::mat4 parentWorld = parent.IsNull() ? glm::mat4(1.0f) : world.Get<ecs::WorldTransform>(parent).matrix;
        kept.emplace_back(entity.id,
                          Decompose(glm::inverse(parentWorld) * world.Get<ecs::WorldTransform>(created).matrix));
    }
    StopSimulating();
    m_history.Commit(Snapshot());
    Change(std::format("Keep where {} loose objects fell", kept.size()), [&kept](scene::Level& level) {
        for (const auto& [id, transform] : kept) {
            if (scene::LevelEntity* entity = level.Find(id)) {
                entity->transform = transform;
            }
        }
    });
    m_history.Commit(Snapshot());
    m_status = std::format("Kept where {} loose objects came to rest", kept.size());
}

void Editor::StartPlaying(app::Application& app, bool fromCamera) {
    if (m_scene == nullptr || m_game != nullptr || m_loading != nullptr) {
        return;
    }
    StopSimulating();
    m_history.Commit(Snapshot());
    // The level as it is, saved or not, beside its file so that its paths resolve, as F5 writes it.
    std::string name = std::filesystem::path(m_levelPath).filename().generic_string();
    if (name.ends_with(kLevelExtension)) {
        name.resize(name.size() - kLevelExtension.size());
    }
    const std::string playPath =
        (std::filesystem::path(m_levelPath).parent_path() / (name + ".play.level.json")).generic_string();
    m_gameFile = m_assetRoot / std::filesystem::path(playPath);
    if (core::Result<void> saved = scene::SaveLevelFile(m_gameFile, m_level); !saved) {
        m_status = core::Format(saved.error());
        return;
    }
    // The game reads its settings when it starts: the level, no menu, and where to start.
    const auto set = [&app](std::string_view setting, std::string_view value) {
        static_cast<void>(app.CVars().Set(setting, value));
    };
    set("sandbox.scene", playPath);
    set("sandbox.menu", "false");
    set("sandbox.calibrated", "true");
    set("sandbox.continue", "false");
    set("sandbox.start",
        fromCamera ? std::format("{} {} {} {} {}", m_camera.position.x, m_camera.position.y, m_camera.position.z,
                                 glm::degrees(m_camera.yaw), glm::degrees(m_camera.pitch))
                   : std::string());
    // The editor's scene waits aside, alive, while the game plays its own (ADR-0110).
    m_stashedScene = app.ExchangeScene(nullptr);
    if (m_stashedScene != nullptr) {
        m_stashedScene->Sounds().SetPaused(true);
    }
    m_stopGame = false;
    m_game = sandbox::Game::Create(sandbox::GameOptions{.embedded = true, .quit = [this] { m_stopGame = true; }});
    if (core::Result<void> started = m_game->OnStart(app); !started) {
        m_status = core::Format(started.error());
        DAI_LOG_ERROR(kModule, "{}", m_status);
        m_game.reset();
        std::unique_ptr<scene::Scene> failed = app.ExchangeScene(std::move(m_stashedScene));
        failed.reset();
        return;
    }
    m_status = "Playing in the editor: Shift+Esc stops";
    DAI_LOG_INFO(kModule, "Playing {} in the editor", playPath);
}

void Editor::StopPlaying(app::Application& app) {
    if (m_game == nullptr) {
        return;
    }
    m_game->OnStop(app);
    // The game's scene goes, after the game that drew it; the editor's comes back as it was left.
    std::unique_ptr<scene::Scene> played = app.ExchangeScene(std::move(m_stashedScene));
    m_game.reset();
    played.reset();
    app.MainWindow().SetMouseCaptured(false);
    app.Rumble().Stop();
    m_shownTitle.clear();
    m_cutView = true;
    m_flying = false;
    std::error_code error;
    std::filesystem::remove(m_gameFile, error);
    m_status = "Stopped playing";
}

bool Editor::UpdateGame(app::Application& app, const core::FrameTime& time) {
    if (m_game == nullptr) {
        return false;
    }
    const input::InputState& input = app.Input();
    if (m_stopGame || (input.WasPressed(platform::Key::Escape) && input.IsDown(platform::Key::LeftShift))) {
        StopPlaying(app);
        return false;
    }
    m_game->OnUpdate(app, time);
    if (m_game->Failure()) {
        m_status = core::Format(*m_game->Failure());
        StopPlaying(app);
        return false;
    }
    // A reminder in a corner: the game has the whole window.
    const char* reminder = "Playing in the editor - Shift+Esc stops";
    const ImVec2 size = ImGui::CalcTextSize(reminder);
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const ImVec2 corner(ImGui::GetMainViewport()->Size.x - size.x - 16.0f, 10.0f);
    draw->AddRectFilled({corner.x - 6.0f, corner.y - 3.0f}, {corner.x + size.x + 6.0f, corner.y + size.y + 3.0f},
                        IM_COL32(0, 0, 0, 140), 3.0f);
    draw->AddText(corner, IM_COL32(255, 200, 60, 230), reminder);
    return true;
}

void Editor::RunMapCheck() {
    const std::filesystem::path root = m_assetRoot;
    CheckSources sources{.exists =
                             [&root](const std::string& path) {
                                 std::error_code error;
                                 return std::filesystem::exists(root / std::filesystem::path(path), error);
                             },
                         .sequence = [&root](const std::string& path) -> std::optional<scene::Sequence> {
                             std::ifstream file(root / std::filesystem::path(path), std::ios::binary);
                             if (!file) {
                                 return std::nullopt;
                             }
                             std::stringstream text;
                             text << file.rdbuf();
                             core::Result<scene::Sequence> sequence = scene::ParseSequence(text.str(), path);
                             return sequence ? std::optional<scene::Sequence>(std::move(*sequence)) : std::nullopt;
                         }};
    m_problems = CheckLevel(m_level, m_levelPath, sources);
    // What only the scene knows: a light inside a wall, closed in on almost every side within a hand's width.
    if (m_instance != nullptr) {
        ecs::World& world = m_scene->World();
        ecs::PropagateTransforms(world);
        constexpr std::array<glm::vec3, 6> kAround{glm::vec3(1, 0, 0),  glm::vec3(-1, 0, 0), glm::vec3(0, 1, 0),
                                                   glm::vec3(0, -1, 0), glm::vec3(0, 0, 1),  glm::vec3(0, 0, -1)};
        for (const scene::LevelEntity& entity : m_level.entities) {
            const ecs::Entity created = entity.light ? m_instance->Find(entity.id) : ecs::Entity{};
            if (created.IsNull()) {
                continue;
            }
            const glm::vec3 at(world.Get<ecs::WorldTransform>(created).matrix[3]);
            // A lamp's light sits inside its own shade or cage: what holds the light is passed through.
            const auto ownLamp = [this, &entity](ecs::Entity drawn) {
                const std::uint64_t id = m_instance->IdOf(drawn);
                return id == entity.id || (id != 0 && IsAncestor(m_level, id, entity.id));
            };
            int closed = 0;
            for (const glm::vec3& direction : kAround) {
                const std::optional<PickHit> hit = m_picker.Cast(world, at, direction, ownLamp);
                closed += hit && hit->distance < 0.25f ? 1 : 0;
            }
            if (closed >= 5) {
                m_problems.push_back(
                    {Problem::Severity::Warning, entity.id,
                     std::format("{} is closed in on {} sides: inside a wall?", EntityLabel(entity), closed)});
            }
        }
    }
    m_checkedRevision = m_history.Current();
    m_checkedLevel = m_levelPath;
    const auto errors = std::ranges::count_if(
        m_problems, [](const Problem& problem) { return problem.severity == Problem::Severity::Error; });
    const auto warnings = std::ranges::count_if(
        m_problems, [](const Problem& problem) { return problem.severity == Problem::Severity::Warning; });
    // Notes after the rest, as CheckLevel sorts them; the scene's own findings are warnings.
    std::ranges::stable_sort(m_problems, [](const Problem& a, const Problem& b) { return a.severity < b.severity; });
    m_status = std::format("Map check: {} errors, {} warnings", errors, warnings);
}

void Editor::DrawMapCheck() {
    if (!ImGui::Begin("Map check")) {
        ImGui::End();
        return;
    }
    if (m_checkedLevel != m_levelPath) {
        RunMapCheck();
    }
    if (ImGui::Button("Check again")) {
        RunMapCheck();
    }
    ImGui::SameLine();
    const auto count = [this](Problem::Severity severity) {
        return std::ranges::count_if(m_problems,
                                     [severity](const Problem& problem) { return problem.severity == severity; });
    };
    const auto errors = count(Problem::Severity::Error);
    const auto warnings = count(Problem::Severity::Warning);
    const auto notes = count(Problem::Severity::Note);
    if (errors + warnings == 0) {
        ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.0f), "No problems found");
    } else {
        ImGui::Text("%td errors, %td warnings", errors, warnings);
    }
    ImGui::SameLine();
    ImGui::Checkbox(std::format("{} notes", notes).c_str(), &m_showNotes);
    if (m_checkedRevision != m_history.Current()) {
        ImGui::SameLine();
        ImGui::TextDisabled("(the level changed since)");
    }
    ImGui::Separator();
    ImGui::BeginChild("problems");
    for (std::size_t index = 0; index < m_problems.size(); ++index) {
        const Problem& problem = m_problems[index];
        if (problem.severity == Problem::Severity::Note && !m_showNotes) {
            continue;
        }
        constexpr std::array<const char*, 3> kSeverities{"Error", "Warning", "Note"};
        const std::array<ImVec4, 3> colors{ImVec4(1.0f, 0.45f, 0.4f, 1.0f), ImVec4(1.0f, 0.8f, 0.35f, 1.0f),
                                           ImVec4(0.6f, 0.75f, 0.9f, 1.0f)};
        const auto severity = static_cast<std::size_t>(problem.severity);
        ImGui::PushStyleColor(ImGuiCol_Text, colors[severity]);
        ImGui::TextUnformatted(kSeverities[severity]);
        ImGui::PopStyleColor();
        ImGui::SameLine(80.0f);
        const std::string label = std::format("{}##problem{}", problem.message, index);
        if (ImGui::Selectable(label.c_str(), problem.entity != 0 && IsSelected(problem.entity)) &&
            problem.entity != 0 && m_level.Find(problem.entity) != nullptr) {
            // A click goes there.
            Select(problem.entity);
            FrameSelection();
        }
        if (problem.entity != 0 && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Click to select it and look at it");
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

void Editor::DrawStatistics(app::Application& app) {
    if (!ImGui::Begin("Statistics")) {
        ImGui::End();
        return;
    }
    // Green within budget, amber near it, red over it.
    const auto meter = [](const char* label, double value, double budget, const char* unit, int decimals = 1) {
        const double share = budget > 0.0 ? value / budget : 0.0;
        const ImVec4 color = share > 1.0   ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f)
                             : share > 0.8 ? ImVec4(1.0f, 0.8f, 0.35f, 1.0f)
                                           : ImVec4(0.5f, 0.9f, 0.5f, 1.0f);
        ImGui::TextUnformatted(label);
        ImGui::SameLine(190.0f);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);
        const std::string text = std::format("{:.{}f} / {:.0f} {}", value, decimals, budget, unit);
        ImGui::ProgressBar(static_cast<float>(std::min(share, 1.0)), ImVec2(-1.0f, 0.0f), text.c_str());
        ImGui::PopStyleColor();
    };
    const renderer::FrameStats stats = app.Renderer().Stats();
    if (ImGui::CollapsingHeader("Frame", ImGuiTreeNodeFlags_DefaultOpen)) {
        double gpu = 0.0;
        std::vector<renderer::PassTime> passes;
        for (const renderer::PassTime& pass : app.Renderer().GpuTimes()) {
            if (!pass.beside) {
                gpu += pass.milliseconds;
                passes.push_back(pass);
            }
        }
        meter("GPU frame", gpu, 1000.0 / 60.0, "ms");
        ImGui::TextDisabled("The budget is 60 frames a second on this machine, at %ux%u (%.0f%%)", stats.renderWidth,
                            stats.renderHeight, stats.renderScale * 100.0f);
        std::ranges::sort(passes, [](const renderer::PassTime& a, const renderer::PassTime& b) {
            return a.milliseconds > b.milliseconds;
        });
        for (std::size_t index = 0; index < std::min<std::size_t>(passes.size(), 6); ++index) {
            ImGui::BulletText("%.*s: %.2f ms", static_cast<int>(passes[index].name.size()), passes[index].name.data(),
                              passes[index].milliseconds);
        }
        ImGui::Text("Draws: %u in view, %u drawn, %llu triangles", stats.draws, stats.drawnDraws,
                    static_cast<unsigned long long>(stats.triangles));
        ImGui::Text("Lights: %u (lamps and glowing triangles)", stats.lights);
        ImGui::Text("Particles: %u alive", stats.particlesAlive);
    }
    if (ImGui::CollapsingHeader("Level", ImGuiTreeNodeFlags_DefaultOpen)) {
        std::array<std::size_t, kCategoryCount> counts{};
        std::size_t points = 0;
        std::size_t spots = 0;
        std::size_t decals = 0;
        std::size_t wet = 0;
        for (const scene::LevelEntity& entity : m_level.entities) {
            ++counts[static_cast<std::size_t>(CategoryOf(entity))];
            points += entity.light && entity.light->type == renderer::LightType::Point ? 1 : 0;
            spots += entity.light && entity.light->type == renderer::LightType::Spot ? 1 : 0;
            decals += entity.decal ? 1 : 0;
            wet += entity.wet ? 1 : 0;
        }
        ImGui::Text("%zu entities", m_level.entities.size());
        for (std::size_t category = 0; category < kCategoryCount; ++category) {
            ImGui::BulletText("%s: %zu", std::string(CategoryName(static_cast<Category>(category))).c_str(),
                              counts[category]);
        }
        ImGui::Text("Lights: %zu point, %zu spot", points, spots);
        // The renderer draws the 64 nearest decals and the 16 nearest wet areas (ADR-0060, ADR-0078).
        meter("Decals (64 at once)", static_cast<double>(decals), 64.0, "", 0);
        meter("Wet areas (16 at once)", static_cast<double>(wet), 16.0, "", 0);
        ImGui::TextDisabled("Beyond those, the nearest to the camera are drawn.");
    }
    if (ImGui::CollapsingHeader("Memory", ImGuiTreeNodeFlags_DefaultOpen)) {
        const rhi::MemoryStats memory = app.Renderer().Device().Memory();
        constexpr double kMegabyte = 1024.0 * 1024.0;
        if (memory.budgetBytes > 0) {
            meter("GPU memory", static_cast<double>(memory.usedBytes) / kMegabyte,
                  static_cast<double>(memory.budgetBytes) / kMegabyte, "MB");
        } else {
            ImGui::Text("GPU memory: %.0f MB", static_cast<double>(memory.usedBytes) / kMegabyte);
        }
        ImGui::Text("Ray tracing structures: %.1f MB",
                    static_cast<double>(memory.accelerationStructureBytes) / kMegabyte);
        ImGui::Text("%zu textures, %zu buffers, %zu meshes, %zu materials", memory.textures, memory.buffers,
                    stats.meshes, stats.materials);
        if (m_scene != nullptr) {
            std::size_t models = 0;
            std::size_t textures = 0;
            for (const scene::AssetLibrary::AssetInfo& asset : m_scene->Assets().Describe()) {
                models += asset.kind == scene::AssetLibrary::AssetInfo::Kind::Model ? 1 : 0;
                textures += asset.kind == scene::AssetLibrary::AssetInfo::Kind::Texture ? 1 : 0;
            }
            ImGui::Text("Loaded for the level: %zu models, %zu images", models, textures);
        }
    }
    ImGui::End();
}

void Editor::DrawCreateItems() {
    if (ImGui::MenuItem("Empty")) {
        Create("Empty", {});
    }
    // Plain geometry to try a room with before modelling it (ADR-0115).
    if (ImGui::BeginMenu("Block out")) {
        const auto block = [this](const char* name, scene::LevelBlock shape) {
            scene::LevelEntity entity;
            entity.block = shape;
            Create(name, std::move(entity));
        };
        if (ImGui::MenuItem("Box")) {
            block("Box", scene::LevelBlock{});
        }
        if (ImGui::MenuItem("Wall")) {
            block("Wall", scene::LevelBlock{.size = {4.0f, 3.0f, 0.2f}});
        }
        if (ImGui::MenuItem("Floor")) {
            block("Floor", scene::LevelBlock{.size = {4.0f, 0.1f, 4.0f}});
        }
        if (ImGui::MenuItem("Stairs")) {
            block("Stairs",
                  scene::LevelBlock{.shape = scene::BlockShape::Stairs, .size = {1.2f, 1.6f, 2.8f}, .steps = 9});
        }
        if (ImGui::MenuItem("Ramp")) {
            block("Ramp", scene::LevelBlock{.shape = scene::BlockShape::Ramp, .size = {2.0f, 1.0f, 4.0f}});
        }
        if (ImGui::MenuItem("Pillar")) {
            block("Pillar", scene::LevelBlock{.shape = scene::BlockShape::Cylinder, .size = {0.5f, 3.0f, 0.5f}});
        }
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Point light")) {
        scene::LevelEntity entity;
        entity.light = scene::Light{.type = renderer::LightType::Point, .intensity = 50.0f, .range = 8.0f};
        Create("Point light", std::move(entity));
    }
    if (ImGui::MenuItem("Spot light")) {
        scene::LevelEntity entity;
        entity.light = scene::Light{.type = renderer::LightType::Spot, .intensity = 200.0f, .range = 12.0f};
        Create("Spot light", std::move(entity));
    }
    // Decals: the box faces away from the camera, onto what is in front of it (ADR-0060).
    if (ImGui::BeginMenu("Decals")) {
        const auto decal = [this](const char* name, const char* texture, glm::vec3 size, float roughness) {
            scene::LevelEntity entity;
            entity.decal = scene::Decal{.texture = texture, .roughness = roughness};
            entity.transform.scale = size;
            entity.transform.rotation = math::FromYawPitchRoll(m_camera.yaw, 0.0f, 0.0f);
            Create(name, std::move(entity));
        };
        if (ImGui::MenuItem("Blood splatter")) {
            decal("Blood splatter", "Decals/blood_splatter.png", {1.2f, 1.2f, 0.6f}, 0.25f);
        }
        if (ImGui::MenuItem("Blood drips")) {
            decal("Blood drips", "Decals/blood_drips.png", {0.9f, 1.6f, 0.6f}, 0.3f);
        }
        if (ImGui::MenuItem("Grime")) {
            decal("Grime", "Decals/grime.png", {2.0f, 2.0f, 1.0f}, 0.9f);
        }
        if (ImGui::MenuItem("Mold")) {
            decal("Mold", "Decals/mold.png", {1.5f, 1.5f, 0.8f}, 0.85f);
        }
        if (ImGui::MenuItem("Scorch mark")) {
            decal("Scorch mark", "Decals/scorch.png", {1.4f, 1.4f, 0.8f}, 0.95f);
        }
        ImGui::EndMenu();
    }
    // Presets: lights a horror level keeps asking for (ADR-0057).
    if (ImGui::BeginMenu("Light presets")) {
        const auto preset = [this](const char* name, scene::Light light) {
            scene::LevelEntity entity;
            entity.light = std::move(light);
            Create(name, std::move(entity));
        };
        if (ImGui::MenuItem("Candle")) {
            preset("Candle", {.intensity = 1.0f,
                              .temperature = 1850.0f,
                              .range = 4.0f,
                              .radius = 0.01f,
                              .animation = {.pattern = scene::LightPattern::Flicker}});
        }
        if (ImGui::MenuItem("Lighter")) {
            preset("Lighter", {.intensity = 0.6f,
                               .temperature = 1900.0f,
                               .range = 3.0f,
                               .radius = 0.008f,
                               .animation = {.pattern = scene::LightPattern::Flicker, .rate = 1.6f}});
        }
        if (ImGui::MenuItem("Buzzing tube")) {
            preset("Buzzing tube", {.intensity = 120.0f,
                                    .temperature = 5200.0f,
                                    .range = 9.0f,
                                    .radius = 0.3f,
                                    .profile = "Lights/downlight.ies",
                                    .animation = {.pattern = scene::LightPattern::Buzz}});
        }
        if (ImGui::MenuItem("Failing lamp")) {
            preset("Failing lamp", {.intensity = 80.0f,
                                    .temperature = 2900.0f,
                                    .range = 8.0f,
                                    .profile = "Lights/bare_bulb.ies",
                                    .animation = {.pattern = scene::LightPattern::Failing}});
        }
        if (ImGui::MenuItem("Emergency light")) {
            preset("Emergency light",
                   {.type = renderer::LightType::Spot,
                    .color = {1.0f, 0.08f, 0.04f},
                    .intensity = 400.0f,
                    .range = 14.0f,
                    .innerAngle = glm::radians(15.0f),
                    .outerAngle = glm::radians(30.0f),
                    .animation = {.pattern = scene::LightPattern::Pulse, .rate = 0.8f, .circuit = "emergency"}});
        }
        if (ImGui::MenuItem("Hazard strobe")) {
            preset("Hazard strobe", {.color = {1.0f, 0.55f, 0.1f},
                                     .intensity = 300.0f,
                                     .range = 10.0f,
                                     .animation = {.pattern = scene::LightPattern::Strobe, .rate = 1.5f}});
        }
        if (ImGui::MenuItem("Lamp behind a fan")) {
            preset("Lamp behind a fan", {.type = renderer::LightType::Spot,
                                         .intensity = 600.0f,
                                         .temperature = 3200.0f,
                                         .range = 16.0f,
                                         .innerAngle = glm::radians(30.0f),
                                         .outerAngle = glm::radians(40.0f),
                                         .cookie = "Lights/fan_cookie.png"});
        }
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Fog volume")) {
        scene::LevelEntity entity;
        entity.fog = scene::FogVolume{};
        entity.transform.scale = glm::vec3(4.0f, 2.0f, 4.0f);
        Create("Fog volume", std::move(entity));
    }
    if (ImGui::MenuItem("Trigger")) {
        scene::LevelEntity entity;
        entity.trigger = scene::Trigger{.sequence = "Sequences/scare.sequence.json"};
        entity.transform.scale = glm::vec3(3.0f, 2.5f, 3.0f);
        Create("Trigger", std::move(entity));
    }
    if (ImGui::MenuItem("Wet area")) {
        scene::LevelEntity entity;
        entity.wet = scene::WetVolume{.puddles = 0.4f, .puddleHeight = 0.6f, .rain = 0.0f, .streaks = 0.3f};
        entity.transform.scale = glm::vec3(6.0f, 3.0f, 6.0f);
        Create("Wet area", std::move(entity));
    }
    if (ImGui::MenuItem("Wind volume")) {
        scene::LevelEntity entity;
        entity.wind = scene::WindVolume{};
        entity.transform.scale = glm::vec3(4.0f);
        Create("Wind volume", std::move(entity));
    }
}

void Editor::RegisterCommands() {
    m_commands.clear();
    const auto add = [this](std::string id, std::string name, std::string_view chord, std::function<void()> run,
                            std::function<bool()> enabled = {}, bool repeats = false) {
        m_commands.push_back(Command{.id = std::move(id),
                                     .name = std::move(name),
                                     .defaultChord = ParseChord(chord).value_or(KeyChord{}),
                                     .run = std::move(run),
                                     .enabled = std::move(enabled),
                                     .repeats = repeats});
    };
    const auto selection = [this] { return !m_selection.empty(); };
    // Files and tabs.
    add("file.new", "New level...", "Ctrl+N", [this] {
        m_pathField = "Levels/New.level.json";
        m_openPopup = "New";
    });
    add("file.open", "Open a level...", "Ctrl+O", [this] {
        m_levelFiles = FindLevelFiles(m_assetRoot);
        m_openInNewTab = false;
        m_openPopup = "Open";
    });
    add("file.open_tab", "Open in a new tab...", "Ctrl+Shift+O", [this] {
        m_levelFiles = FindLevelFiles(m_assetRoot);
        m_openInNewTab = true;
        m_openPopup = "Open";
    });
    add("file.save", "Save", "Ctrl+S", [this] { Save(); });
    add("file.save_as", "Save as...", "Ctrl+Shift+S", [this] {
        m_pathField = m_levelPath;
        m_openPopup = "Save As";
    });
    add("file.reload", "Reload from disk", "", [this] { Guard(*m_app, Pending::Reload); });
    add(
        "file.close_tab", "Close the tab", "Ctrl+W", [this] { m_pendingCloseTab = m_activeTab; },
        [this] { return m_tabs.size() > 1; });
    add(
        "file.next_tab", "Next tab", "Ctrl+Tab",
        [this] {
            SwitchTab((m_activeTab + 1) % m_tabs.size());
            m_selectTab = true;
        },
        [this] { return m_tabs.size() > 1; });
    add(
        "file.previous_tab", "Previous tab", "Ctrl+Shift+Tab",
        [this] {
            SwitchTab((m_activeTab + m_tabs.size() - 1) % m_tabs.size());
            m_selectTab = true;
        },
        [this] { return m_tabs.size() > 1; });
    // Editing.
    add("edit.undo", "Undo", "Ctrl+Z", [this] { Undo(); }, [this] { return m_history.CanUndo(); }, true);
    add("edit.redo", "Redo", "Ctrl+Y", [this] { Redo(); }, [this] { return m_history.CanRedo(); }, true);
    add(
        "edit.redo_also", "Redo (second key)", "Ctrl+Shift+Z", [this] { Redo(); },
        [this] { return m_history.CanRedo(); }, true);
    add("edit.duplicate", "Duplicate", "Ctrl+D", [this] { Duplicate(); }, selection);
    add("edit.copy", "Copy", "Ctrl+C", [this] { CopySelection(); }, selection);
    add(
        "edit.cut", "Cut", "Ctrl+X",
        [this] {
            CopySelection();
            DeleteSelected();
        },
        selection);
    add("edit.paste", "Paste", "Ctrl+V", [this] { Paste(); });
    add("edit.delete", "Delete", "Delete", [this] { DeleteSelected(); }, selection);
    add("edit.select_all", "Select everything", "Ctrl+A", [this] { SelectAll(); });
    add("edit.cancel", "Select nothing, or stop the tool or the simulation", "Escape", [this] {
        if (m_pilotReturn) {
            m_camera = *std::exchange(m_pilotReturn, std::nullopt);
            m_status = "Back where the camera was";
        } else if (m_simulating) {
            StopSimulating();
        } else if (m_painting) {
            m_painting = false;
        } else if (m_measuring) {
            m_measuring = false;
        } else {
            Select(0);
        }
    });
    add("edit.frame", "Frame the selection", "F", [this] { FrameSelection(); }, selection);
    add(
        "edit.rename", "Rename...", "F2",
        [this] {
            m_pathField = Selected() != nullptr ? Selected()->name : std::string();
            m_openPopup = "Rename";
        },
        [this] { return Selected() != nullptr; });
    add("edit.hide", "Hide", "H", [this] { SetHidden(SelectionRoots(), true); }, selection);
    add("edit.isolate", "Isolate, or show the rest again", "Shift+H", [this] { ToggleIsolate(); });
    add("edit.show_all", "Show everything", "Alt+H", [this] { ShowEverything(); });
    add(
        "edit.lock", "Lock or unlock", "L",
        [this] {
            const scene::LevelEntity* selected = Selected();
            SetLocked(SelectionRoots(), selected == nullptr || !selected->editing.locked);
        },
        selection);
    add("edit.drop", "Drop to the floor", "End", [this] { DropToFloor(); }, selection);
    add(
        "edit.folder", "Put in a folder...", "",
        [this] {
            m_pathField = Selected() != nullptr ? Selected()->editing.folder : std::string();
            m_openPopup = "Folder";
        },
        selection);
    add(
        "edit.make_prefab", "Make a prefab...", "",
        [this] {
            m_pathField = "Prefabs/New.prefab.json";
            m_openPopup = "Make prefab";
        },
        selection);
    // Tools.
    add("tool.move", "Move", "W", [this] { m_operation = ImGuizmo::TRANSLATE; });
    add("tool.rotate", "Rotate", "E", [this] { m_operation = ImGuizmo::ROTATE; });
    add("tool.scale", "Scale", "R", [this] { m_operation = ImGuizmo::SCALE; });
    add("tool.paint", "Paint decals and wet patches", "B", [this] {
        m_painting = !m_painting;
        m_measuring = false;
    });
    add("tool.measure", "Measure", "M", [this] {
        m_measuring = !m_measuring;
        m_painting = false;
        m_measureFrom.reset();
        m_measureTo.reset();
        m_status = m_measuring ? "Measuring: drag between two surfaces; M or Esc stops" : "";
    });
    add("tool.snap", "Snapping", "X", [this] {
        m_snap = !m_snap;
        m_status = m_snap ? "Snapping on" : "Snapping off";
    });
    add("tool.local", "Local or world axes", "", [this] { m_local = !m_local; });
    add("tool.surface_snap", "Snap to surfaces while moving", "", [this] { m_surfaceSnap = !m_surfaceSnap; });
    // Views.
    add("view.game", "Game view", "G", [this] {
        m_show.gameView = !m_show.gameView;
        m_status = m_show.gameView ? "Game view: G shows the editor's drawing again" : "Editor view";
    });
    add("view.grid", "Grid", "Shift+G", [this] { m_show.grid = !m_show.grid; });
    add("view.realtime", "Real time", "Ctrl+R", [this] {
        m_realtime = !m_realtime;
        m_status = m_realtime ? "Real time: the scene moves" : "The scene holds still: Ctrl+R lets it move";
    });
    add("view.sound", "Hear the level", "", [this] {
        m_hear = !m_hear;
        m_status = m_hear ? "Hearing the level" : "The level is silent";
    });
    add(
        "view.look_through", "Look through the selection", "", [this] { LookThrough(); },
        [this] { return Selected() != nullptr; });
    add(
        "view.preview", "Take a picture from the selection", "", [this] { StartPreview(); },
        [this] { return Selected() != nullptr; });
    add("view.step", "Step one frame", "F10", [this] {
        m_stepFrames = 1;
        m_realtime = false;
    });
    add("view.lit", "Lit", "Alt+1", [this] { SetLighting(Lighting::Lit); });
    add("view.unlit", "Unlit", "Alt+2", [this] { SetLighting(Lighting::Unlit); });
    add("view.lighting_only", "Lighting only", "Alt+3", [this] { SetLighting(Lighting::LightingOnly); });
    add("view.editor_light", "Editor light", "Alt+4", [this] { SetLighting(Lighting::EditorLight); });
    add("view.perspective", "Perspective view", "Shift+1", [this] { SetViewKind(ViewKind::Perspective); });
    add("view.top", "Top view", "Shift+2", [this] { SetViewKind(ViewKind::Top); });
    add("view.front", "Front view", "Shift+3", [this] { SetViewKind(ViewKind::Front); });
    add("view.side", "Side view", "Shift+4", [this] { SetViewKind(ViewKind::Side); });
    add("view.split", "Four views", "Shift+5", [this] { SetSplit(!m_split); });
    add("view.perspective_keypad", "Perspective view (keypad)", "Keypad5",
        [this] { SetViewKind(ViewKind::Perspective); });
    add("view.top_keypad", "Top view (keypad)", "Keypad7", [this] { SetViewKind(ViewKind::Top); });
    add("view.front_keypad", "Front view (keypad)", "Keypad1", [this] { SetViewKind(ViewKind::Front); });
    add("view.side_keypad", "Side view (keypad)", "Keypad3", [this] { SetViewKind(ViewKind::Side); });
    const auto axis = [this] { return m_panes[m_active].kind != ViewKind::Perspective; };
    const auto cut = [this](float step) {
        OrthoView& view = m_panes[m_active].ortho;
        view.cut = std::clamp(view.cut + step, kMinCut, kMaxCut);
    };
    add("view.cut_nearer", "Cut nearer", "LeftBracket", [cut] { cut(-0.25f); }, axis, true);
    add("view.cut_farther", "Cut farther", "RightBracket", [cut] { cut(0.25f); }, axis, true);
    add("view.cut_nearer_more", "Cut a meter nearer", "Shift+LeftBracket", [cut] { cut(-1.0f); }, axis, true);
    add("view.cut_farther_more", "Cut a meter farther", "Shift+RightBracket", [cut] { cut(1.0f); }, axis, true);
    // Playing.
    add("play.sandbox", "Play in the sandbox", "F5", [this] { Play(false); });
    add("play.sandbox_camera", "Play in the sandbox, from the camera", "Ctrl+F5", [this] { Play(true); });
    add("play.editor", "Play in the editor", "Alt+P", [this] { StartPlaying(*m_app, false); });
    add("play.editor_camera", "Play in the editor, from the camera", "Alt+Shift+P",
        [this] { StartPlaying(*m_app, true); });
    add("play.simulate", "Simulate, or stop", "Alt+S", [this] {
        if (m_simulating) {
            StopSimulating();
        } else {
            StartSimulating();
        }
    });
    add(
        "play.keep", "Keep where loose objects are", "K", [this] { KeepSimulation(); },
        [this] { return m_simulating; });
    // The rest.
    add("check.level", "Check the level", "Ctrl+Shift+C", [this] {
        RunMapCheck();
        ImGui::SetWindowFocus("Map check");
    });
    add("help.shortcuts", "Shortcuts and keys", "F1", [this] { m_showHelp = !m_showHelp; });
    add("help.palette", "Search everything", "Ctrl+P", [this] { OpenPalette(); });
    for (int slot = 1; slot <= 9; ++slot) {
        add(std::format("bookmark.go{}", slot), std::format("Go to camera bookmark {}", slot), std::format("{}", slot),
            [this, slot] { Bookmark(slot, false); });
        add(std::format("bookmark.set{}", slot), std::format("Set camera bookmark {}", slot),
            std::format("Ctrl+{}", slot), [this, slot] { Bookmark(slot, true); });
    }
    // Every Dear ImGui key by its name, for the keymap.
    m_keysByName.clear();
    for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; ++key) {
        m_keysByName.emplace(ImGui::GetKeyName(static_cast<ImGuiKey>(key)), static_cast<ImGuiKey>(key));
    }
    LoadKeymap();
}

void Editor::LoadKeymap() {
    std::ifstream file(platform::ExecutableDirectory() / "editor_keys.txt");
    std::stringstream text;
    text << file.rdbuf();
    m_keymap = ParseKeymap(text.str());
    m_chordTexts.clear();
}

void Editor::SaveKeymap() {
    std::ofstream file(platform::ExecutableDirectory() / "editor_keys.txt");
    file << WriteKeymap(m_keymap);
    m_chordTexts.clear();
}

const char* Editor::Keys(std::string_view id) {
    if (m_chordTexts.empty()) {
        for (const Command& command : m_commands) {
            m_chordTexts[command.id] = FormatChord(ChordOf(command, m_keymap));
        }
    }
    const auto found = m_chordTexts.find(std::string(id));
    return found != m_chordTexts.end() && !found->second.empty() ? found->second.c_str() : nullptr;
}

void Editor::Run(std::string_view id) {
    for (const Command& command : m_commands) {
        if (command.id == id && (!command.enabled || command.enabled())) {
            command.run();
            return;
        }
    }
}

bool Editor::ChordPressed(const KeyChord& chord, bool repeats) const {
    const auto key = m_keysByName.find(chord.key);
    if (chord.Empty() || key == m_keysByName.end()) {
        return false;
    }
    const ImGuiIO& io = ImGui::GetIO();
    return io.KeyCtrl == chord.ctrl && io.KeyShift == chord.shift && io.KeyAlt == chord.alt &&
           ImGui::IsKeyPressed(static_cast<ImGuiKey>(key->second), repeats);
}

void Editor::HandleShortcuts(app::Application& /*app*/) {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput || m_flying || !m_openPopup.empty() || m_capturing ||
        ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
        return;
    }
    // The first command whose keys were pressed runs (ADR-0112).
    for (const Command& command : m_commands) {
        if (ChordPressed(ChordOf(command, m_keymap), command.repeats) && (!command.enabled || command.enabled())) {
            command.run();
            return;
        }
    }
}

void Editor::DrawHelp() {
    if (!m_showHelp) {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(620.0f, 560.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Shortcuts", &m_showHelp, ImGuiWindowFlags_NoDocking)) {
        ImGui::End();
        return;
    }
    ImGui::TextDisabled("Right mouse + WASD, Q, E: fly (Shift faster, the wheel sets the speed). Middle mouse: pan.");
    ImGui::TextDisabled("Click, Ctrl+click: select. Right click without moving: what can be done there.");
    ImGui::TextDisabled("Keys of other editors' habits can be given here; editor_keys.txt keeps them.");
    ImGui::SetNextItemWidth(-1.0f);
    InputString("##find keys", m_keyFilter);
    const auto clashes = Clashes(m_commands, m_keymap);
    // Waiting for a key: the next one pressed with its modifiers is the command's, Backspace takes it away, Escape
    // leaves it.
    if (m_capturing) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.35f, 1.0f),
                           "Press the keys for \"%s\" (Backspace: none, Escape: leave it)", m_capturing->c_str());
        const ImGuiIO& io = ImGui::GetIO();
        for (const auto& [name, code] : m_keysByName) {
            const auto key = static_cast<ImGuiKey>(code);
            const bool modifier = key == ImGuiKey_LeftCtrl || key == ImGuiKey_RightCtrl || key == ImGuiKey_LeftShift ||
                                  key == ImGuiKey_RightShift || key == ImGuiKey_LeftAlt || key == ImGuiKey_RightAlt ||
                                  key == ImGuiKey_LeftSuper || key == ImGuiKey_RightSuper || key == ImGuiMod_Ctrl ||
                                  key == ImGuiMod_Shift || key == ImGuiMod_Alt || key == ImGuiMod_Super ||
                                  key >= ImGuiKey_MouseLeft;
            if (modifier || !ImGui::IsKeyPressed(key, false)) {
                continue;
            }
            if (key == ImGuiKey_Escape) {
                m_capturing.reset();
            } else if (key == ImGuiKey_Backspace) {
                m_keymap[*m_capturing] = KeyChord{};
                SaveKeymap();
                m_capturing.reset();
            } else {
                m_keymap[*m_capturing] =
                    KeyChord{.key = name, .ctrl = io.KeyCtrl, .shift = io.KeyShift, .alt = io.KeyAlt};
                SaveKeymap();
                m_capturing.reset();
            }
            break;
        }
    }
    if (ImGui::BeginTable("keys", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
                          ImVec2(0.0f, -1.0f))) {
        ImGui::TableSetupColumn("What", ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableSetupColumn("Keys", ImGuiTableColumnFlags_WidthStretch, 1.4f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        for (const Command& command : m_commands) {
            if (!m_keyFilter.empty() && !FuzzyScore(command.name, m_keyFilter) &&
                !FuzzyScore(command.id, m_keyFilter)) {
                continue;
            }
            const std::string chord = FormatChord(ChordOf(command, m_keymap));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(command.name.c_str());
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", command.id.c_str());
            }
            ImGui::TableNextColumn();
            const bool clash = !chord.empty() && clashes.contains(chord);
            if (clash) {
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", chord.c_str());
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Another command has these keys too: the first in this list runs");
                }
            } else {
                ImGui::TextUnformatted(chord.empty() ? "-" : chord.c_str());
            }
            ImGui::TableNextColumn();
            ImGui::PushID(command.id.c_str());
            if (ImGui::SmallButton("Change")) {
                m_capturing = command.id;
            }
            if (m_keymap.contains(command.id)) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Reset")) {
                    m_keymap.erase(command.id);
                    SaveKeymap();
                }
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void Editor::OpenPalette() {
    m_paletteOpen = true;
    m_paletteFocus = true;
    m_paletteQuery.clear();
    m_paletteChoice = 0;
    m_paletteSetting.reset();
    // The assets under the root, walked once a minute at most.
    const auto now = std::chrono::steady_clock::now();
    if (m_paletteAssets.empty() || now - m_paletteAssetsAt > std::chrono::minutes(1)) {
        m_paletteAssets.clear();
        std::vector<std::string> folders{""};
        while (!folders.empty() && m_paletteAssets.size() < 20000) {
            const std::string folder = std::move(folders.back());
            folders.pop_back();
            for (AssetEntry& entry : ListFolder(m_assetRoot, folder)) {
                if (entry.kind == AssetKind::Folder) {
                    if (entry.name != "Source") {
                        folders.push_back(entry.path);
                    }
                } else if (entry.kind != AssetKind::Other) {
                    m_paletteAssets.push_back(std::move(entry));
                }
            }
        }
        m_paletteAssetsAt = now;
    }
}

void Editor::DrawPalette(app::Application& app) {
    if (!m_paletteOpen) {
        return;
    }
    // Search everything (Ctrl+P): commands, entities, assets and settings, at the top of the window.
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + 60.0f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(std::min(720.0f, viewport->Size.x - 40.0f), 0.0f), ImGuiCond_Always);
    if (m_paletteFocus) {
        ImGui::SetNextWindowFocus();
    }
    if (!ImGui::Begin("##palette", nullptr,
                      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
                          ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    if (m_paletteSetting) {
        // A setting chosen: its new value.
        ImGui::Text("%s", m_paletteSetting->c_str());
        if (const core::CVarEntry* entry = app.CVars().Find(*m_paletteSetting)) {
            ImGui::TextDisabled("%s", entry->description.c_str());
        }
        ImGui::SetNextItemWidth(-1.0f);
        if (std::exchange(m_paletteFocus, false)) {
            ImGui::SetKeyboardFocusHere();
        }
        InputString("##value", m_paletteQuery);
        if (ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            if (core::Result<void> set = app.CVars().Set(*m_paletteSetting, m_paletteQuery); !set) {
                m_status = core::Format(set.error());
            } else {
                m_status = std::format("{} = {}", *m_paletteSetting, m_paletteQuery);
            }
            m_paletteOpen = false;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            m_paletteOpen = false;
        }
        ImGui::End();
        return;
    }
    ImGui::SetNextItemWidth(-1.0f);
    if (std::exchange(m_paletteFocus, false)) {
        ImGui::SetKeyboardFocusHere();
    }
    if (InputString("##search", m_paletteQuery)) {
        m_paletteChoice = 0;
    }
    if (m_paletteQuery.empty()) {
        ImGui::TextDisabled("Commands, entities, assets and settings. Up and Down choose, Enter does it.");
    }
    struct Result {
        int score = 0;
        enum class Kind { Command, Entity, Asset, Setting } kind = Kind::Command;
        std::string label;
        std::string detail;
        std::size_t index = 0;
        std::uint64_t entity = 0;
        std::string setting;
    };
    std::vector<Result> results;
    for (std::size_t index = 0; index < m_commands.size(); ++index) {
        const Command& command = m_commands[index];
        if (command.enabled && !command.enabled()) {
            continue;
        }
        if (const std::optional<int> score = FuzzyScore(command.name, m_paletteQuery)) {
            results.push_back({*score + 4,
                               Result::Kind::Command,
                               command.name,
                               FormatChord(ChordOf(command, m_keymap)),
                               index,
                               0,
                               {}});
        }
    }
    if (!m_paletteQuery.empty()) {
        for (const scene::LevelEntity& entity : m_level.entities) {
            const std::string label = EntityLabel(entity);
            if (const std::optional<int> score = FuzzyScore(label, m_paletteQuery)) {
                results.push_back({*score, Result::Kind::Entity, label, KindLabel(entity), 0, entity.id, {}});
            }
        }
        for (std::size_t index = 0; index < m_paletteAssets.size(); ++index) {
            if (const std::optional<int> score = FuzzyScore(m_paletteAssets[index].path, m_paletteQuery)) {
                results.push_back(
                    {*score - 2, Result::Kind::Asset, m_paletteAssets[index].path, "asset", index, 0, {}});
            }
        }
        app.CVars().ForEach([&](const core::CVarEntry& entry) {
            if (const std::optional<int> score = FuzzyScore(entry.name, m_paletteQuery)) {
                const std::string value = std::visit(
                    [](const auto& held) {
                        if constexpr (std::is_same_v<std::decay_t<decltype(held)>, std::string>) {
                            return held;
                        } else if constexpr (std::is_same_v<std::decay_t<decltype(held)>, bool>) {
                            return std::string(held ? "true" : "false");
                        } else {
                            return std::format("{}", held);
                        }
                    },
                    entry.value);
                results.push_back({*score - 3, Result::Kind::Setting, entry.name, "= " + value, 0, 0, entry.name});
            }
        });
    }
    std::ranges::stable_sort(results, [](const Result& a, const Result& b) { return a.score > b.score; });
    if (results.size() > 40) {
        results.resize(40);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) {
        m_paletteChoice = std::min(m_paletteChoice + 1, results.empty() ? 0 : results.size() - 1);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true) && m_paletteChoice > 0) {
        --m_paletteChoice;
    }
    std::optional<std::size_t> chosen;
    constexpr std::array<const char*, 4> kKinds{"command", "entity", "asset", "setting"};
    for (std::size_t row = 0; row < results.size(); ++row) {
        const Result& result = results[row];
        const std::string label = std::format("{}##result{}", result.label, row);
        if (ImGui::Selectable(label.c_str(), row == m_paletteChoice)) {
            chosen = row;
        }
        if (row == m_paletteChoice) {
            ImGui::SetScrollHereY();
        }
        ImGui::SameLine(ImGui::GetWindowWidth() - 220.0f);
        ImGui::TextDisabled("%s  %s", kKinds[static_cast<std::size_t>(result.kind)], result.detail.c_str());
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Enter) && !results.empty()) {
        chosen = m_paletteChoice;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        m_paletteOpen = false;
    }
    ImGui::End();
    if (chosen && *chosen < results.size()) {
        const Result result = results[*chosen];
        m_paletteOpen = false;
        switch (result.kind) {
        case Result::Kind::Command:
            m_commands[result.index].run();
            break;
        case Result::Kind::Entity:
            Select(result.entity);
            FrameSelection();
            break;
        case Result::Kind::Asset:
            PlaceAsset(app, m_paletteAssets[result.index], std::nullopt);
            break;
        case Result::Kind::Setting:
            m_paletteOpen = true;
            m_paletteSetting = result.setting;
            m_paletteQuery = result.detail.substr(2);
            m_paletteFocus = true;
            break;
        }
    }
}

void Editor::DrawEntityMenu() {
    // What can be done to the selection, from a right click in the view or the hierarchy (ADR-0112).
    const auto item = [this](const char* label, std::string_view id) {
        bool enabled = true;
        for (const Command& command : m_commands) {
            if (command.id == id && command.enabled) {
                enabled = command.enabled();
            }
        }
        if (ImGui::MenuItem(label, Keys(id), false, enabled)) {
            Run(id);
        }
    };
    if (const scene::LevelEntity* selected = Selected()) {
        ImGui::TextDisabled("%s", EntityLabel(*selected).c_str());
        ImGui::Separator();
    }
    item("Frame", "edit.frame");
    item("Rename...", "edit.rename");
    ImGui::Separator();
    item("Duplicate", "edit.duplicate");
    item("Copy", "edit.copy");
    item("Cut", "edit.cut");
    item("Paste", "edit.paste");
    item("Delete", "edit.delete");
    ImGui::Separator();
    item("Hide", "edit.hide");
    item("Isolate", "edit.isolate");
    item(Selected() != nullptr && Selected()->editing.locked ? "Unlock" : "Lock", "edit.lock");
    item("Drop to the floor", "edit.drop");
    item("Look through it", "view.look_through");
    item("Take a picture from it", "view.preview");
    item("Put in a folder...", "edit.folder");
    item("Make a prefab...", "edit.make_prefab");
    if (const scene::LevelEntity* selected = Selected(); selected != nullptr && !selected->prefab.empty()) {
        if (ImGui::MenuItem("Open the prefab")) {
            m_pendingOpenPrefab = m_instance != nullptr ? m_instance->Resolve(selected->prefab) : std::string{};
        }
        if (ImGui::MenuItem("Unpack the prefab")) {
            m_pendingUnpack = selected->id;
        }
    }
    if (const scene::LevelEntity* selected = Selected(); selected != nullptr && selected->trigger) {
        if (ImGui::MenuItem("Edit its sequence")) {
            m_pendingSequence = selected->id;
        }
    }
}

void Editor::ViewportContextMenu(app::Application& app) {
    const input::InputState& input = app.Input();
    const ImGuiIO& io = ImGui::GetIO();
    // A right click that does not move opens the menu; a drag flies or pans, as before.
    if (input.WasPressed(platform::MouseButton::Right) && MouseInViewport() && !io.WantCaptureMouse) {
        m_rightPress = glm::vec2(io.MousePos.x, io.MousePos.y);
        m_rightTravel = 0.0f;
        m_rightSeconds = 0.0f;
    }
    if (m_rightPress && input.IsDown(platform::MouseButton::Right)) {
        m_rightTravel += glm::length(input.MouseDelta());
        m_rightSeconds += io.DeltaTime;
    }
    if (m_rightPress && !input.IsDown(platform::MouseButton::Right)) {
        const glm::vec2 at = *std::exchange(m_rightPress, std::nullopt);
        if (m_rightTravel < 4.0f && m_rightSeconds < 0.35f) {
            // What is under the click becomes the selection, unless it is selected already, and its surface the
            // place to create or paste at.
            const auto [origin, direction] = RayAt(at);
            ecs::World& world = m_scene->World();
            ecs::PropagateTransforms(world);
            const std::optional<PickHit> hit = m_picker.Cast(world, origin, direction);
            m_contextPoint = hit ? hit->position : origin + direction * 6.0f;
            if (hit) {
                if (const std::uint64_t id = m_instance->IdOf(hit->entity); id != 0 && !IsSelected(id)) {
                    Select(id);
                }
            }
            ImGui::OpenPopup("ViewportMenu");
        }
    }
    if (ImGui::BeginPopup("ViewportMenu")) {
        if (!m_selection.empty()) {
            DrawEntityMenu();
            ImGui::Separator();
        }
        if (ImGui::MenuItem("Paste here", nullptr)) {
            Paste();
            // The pasted entities go where the click was, together.
            const std::vector<std::uint64_t> roots = SelectionRoots();
            const std::vector<glm::vec3> positions = RootPositions(roots);
            if (!positions.empty()) {
                std::vector<std::pair<std::uint64_t, glm::vec3>> offsets;
                for (std::size_t index = 0; index < roots.size(); ++index) {
                    offsets.emplace_back(roots[index], m_contextPoint - positions.front());
                }
                MoveBy(offsets, "Paste here");
            }
        }
        if (ImGui::BeginMenu("Create here")) {
            m_createAt = m_contextPoint;
            DrawCreateItems();
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Measure from here", Keys("tool.measure"))) {
            m_measuring = true;
            m_painting = false;
            m_measureFrom = m_contextPoint;
            m_measureTo = m_contextPoint;
        }
        ImGui::EndPopup();
    }
}

std::optional<EditorCamera> Editor::CameraOfSelection() {
    const scene::LevelEntity* selected = Selected();
    const ecs::Entity entity =
        selected != nullptr && m_instance != nullptr ? m_instance->Find(m_selected) : ecs::Entity{};
    if (entity.IsNull()) {
        return std::nullopt;
    }
    ecs::PropagateTransforms(m_scene->World());
    const glm::mat4& world = m_scene->World().Get<ecs::WorldTransform>(entity).matrix;
    const glm::vec3 forward = glm::normalize(-glm::vec3(world[2]));
    EditorCamera camera = m_camera;
    camera.position = glm::vec3(world[3]);
    // Yaw 0 looks along -Z and turns left as it grows; pitch looks up as it grows.
    camera.yaw = std::atan2(-forward.x, -forward.z);
    camera.pitch = std::asin(std::clamp(forward.y, -1.0f, 1.0f));
    return camera;
}

void Editor::LookThrough() {
    if (const std::optional<EditorCamera> camera = CameraOfSelection()) {
        if (!m_pilotReturn) {
            m_pilotReturn = m_camera;
        }
        m_camera = *camera;
        m_cutView = true;
        m_status = std::format("Looking through {}: Escape goes back", EntityLabel(*Selected()));
    }
}

void Editor::StartPreview() {
    const std::optional<EditorCamera> camera = CameraOfSelection();
    if (!camera || m_previewFrames > 0 || m_panes[m_active].kind != ViewKind::Perspective) {
        return;
    }
    // A dozen frames from there, so that the denoiser and the eye settle, then the last one is captured.
    m_previewReturn = m_camera;
    m_camera = *camera;
    m_previewFrames = 12;
    m_previewOf = EntityLabel(*Selected());
    m_cutView = true;
}

void Editor::UpdatePreview(app::Application& app) {
    if (m_previewFrames > 0 && --m_previewFrames == 0) {
        app.Renderer().RequestCapture();
        m_previewCapture = true;
        return;
    }
    if (m_previewReturn && m_previewFrames == 0 && m_previewCapture) {
        // The frame that was captured is drawn: the camera goes back.
        m_camera = *std::exchange(m_previewReturn, std::nullopt);
        m_cutView = true;
    }
}

void Editor::DrawCameraPreview() {
    if (!ImGui::Begin("Camera preview")) {
        ImGui::End();
        return;
    }
    ImGui::BeginDisabled(Selected() == nullptr || m_previewFrames > 0);
    if (ImGui::Button("Take a picture from the selection")) {
        StartPreview();
    }
    ImGui::SameLine();
    if (ImGui::Button("Look through it")) {
        LookThrough();
    }
    ImGui::EndDisabled();
    if (m_previewTexture.IsNull()) {
        ImGui::TextWrapped("What a security camera, a spot light or any entity sees, looking along its -Z. "
                           "Select it, then take a picture.");
    } else {
        ImGui::TextDisabled("From %s", m_previewOf.c_str());
        const float width = ImGui::GetContentRegionAvail().x;
        const float height = m_previewSize.x > 0.0f ? width * m_previewSize.y / m_previewSize.x : width * 0.56f;
        ImGui::Image(
            static_cast<ImTextureID>(debugui::TextureId(m_app->Renderer().Device().BindlessIndex(m_previewTexture))),
            ImVec2(width, height));
    }
    ImGui::End();
}

void Editor::MoveAssetTo(const std::string& from, const std::string& to) {
    if (m_tabs.size() > 1) {
        m_status = "Close the other tabs first: their levels would keep the old path";
        return;
    }
    core::Result<std::vector<std::string>> rewritten = MoveAsset(m_assetRoot, from, to, m_levelPath);
    if (!rewritten) {
        m_status = core::Format(rewritten.error());
        return;
    }
    // The level open names it through the editor, as a step to save.
    scene::Level repointed = m_level;
    const std::size_t changed = RepointLevel(repointed, m_levelPath, from, to);
    if (changed > 0) {
        m_history.Commit(Snapshot());
        Change(std::format("Point at {}", to), [&repointed](scene::Level& level) { level = repointed; });
        m_history.Commit(Snapshot());
    }
    if (m_browser != nullptr) {
        m_browser->Open(std::filesystem::path(to).parent_path().generic_string());
    }
    m_paletteAssets.clear();
    m_status = std::format("Moved {} to {}: {} files rewritten{}", from, to, rewritten->size(),
                           changed > 0 ? ", and this level's paths (save to keep them)" : "");
    DAI_LOG_INFO(kModule, "{}", m_status);
}

void Editor::DrawAssetDialogs() {
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(620.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("What uses it", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        const std::string& asset = m_assetRequest ? m_assetRequest->asset.path : std::string();
        ImGui::Text("%s", asset.c_str());
        if (m_references.empty()) {
            ImGui::TextDisabled("Nothing under assets/ names it: no level, prefab, effect or sequence.");
        }
        ImGui::BeginChild("references",
                          ImVec2(0.0f, std::min(360.0f, 24.0f + 20.0f * static_cast<float>(m_references.size()))),
                          ImGuiChildFlags_Borders);
        for (const AssetReference& reference : m_references) {
            ImGui::TextUnformatted(reference.file.c_str());
            ImGui::SameLine(320.0f);
            ImGui::TextDisabled("%s", reference.where.c_str());
        }
        ImGui::EndChild();
        if (ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(560.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Rename or move", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        const std::string from = m_assetRequest ? m_assetRequest->asset.path : std::string();
        ImGui::TextWrapped("%s: its new path under assets/. Every level, prefab, effect and sequence that names it "
                           "is rewritten to name the new one.",
                           from.c_str());
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        InputString("##to", m_pathField);
        std::string to = m_pathField;
        std::ranges::replace(to, '\\', '/');
        if (ImGui::Button("Move") || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            ImGui::CloseCurrentPopup();
            MoveAssetTo(from, to);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(480.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Import settings", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        const std::string model = m_assetRequest ? m_assetRequest->asset.path : std::string();
        ImGui::Text("%s", model.c_str());
        ImGui::DragFloat("Scale", &m_importSettings.scale, 0.001f, 0.0001f, 1000.0f, "%.4f",
                         ImGuiSliderFlags_Logarithmic);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("0.01 for a model made in centimeters");
        }
        ImGui::Checkbox("Walls and floors in the game", &m_importSettings.levelCollision);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("The game walks on all of it and shoots at it, as level geometry (ADR-0083)");
        }
        ImGui::TextDisabled("Kept beside the model as %s.import.json; levels opened next show it.",
                            std::filesystem::path(model).filename().generic_string().c_str());
        if (ImGui::Button("Save")) {
            ImGui::CloseCurrentPopup();
            const std::filesystem::path file = scene::ImportSettingsPath(m_assetRoot / std::filesystem::path(model));
            std::error_code error;
            if (m_importSettings == scene::ModelImportSettings{}) {
                std::filesystem::remove(file, error);
            } else {
                std::ofstream(file, std::ios::binary) << scene::WriteImportSettings(m_importSettings);
            }
            m_status = std::format("Import settings of {} saved: reopen the level to see them", model);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void Editor::ApplyTheme(app::Application& app) {
    // A dark theme with one warm accent, the color the editor already gives the selection (ADR-0118).
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.ChildRounding = 3.0f;
    style.FrameRounding = 3.0f;
    style.PopupRounding = 4.0f;
    style.TabRounding = 3.0f;
    style.GrabRounding = 3.0f;
    style.ScrollbarRounding = 6.0f;
    style.WindowPadding = ImVec2(10.0f, 8.0f);
    style.FramePadding = ImVec2(8.0f, 4.0f);
    style.ItemSpacing = ImVec2(8.0f, 5.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    style.IndentSpacing = 16.0f;
    style.ScrollbarSize = 12.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.TabBorderSize = 0.0f;
    style.SeparatorTextBorderSize = 1.0f;
    const auto rgb = [](float r, float g, float b, float a = 1.0f) { return ImVec4(r, g, b, a); };
    const ImVec4 accent = rgb(1.0f, 0.72f, 0.28f);
    const ImVec4 accentDim = rgb(0.62f, 0.44f, 0.17f);
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = rgb(0.89f, 0.90f, 0.91f);
    colors[ImGuiCol_TextDisabled] = rgb(0.50f, 0.52f, 0.55f);
    colors[ImGuiCol_WindowBg] = rgb(0.105f, 0.11f, 0.12f);
    colors[ImGuiCol_ChildBg] = rgb(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_PopupBg] = rgb(0.125f, 0.13f, 0.14f, 0.98f);
    colors[ImGuiCol_Border] = rgb(0.20f, 0.21f, 0.23f);
    colors[ImGuiCol_FrameBg] = rgb(0.165f, 0.17f, 0.185f);
    colors[ImGuiCol_FrameBgHovered] = rgb(0.22f, 0.23f, 0.25f);
    colors[ImGuiCol_FrameBgActive] = rgb(0.27f, 0.28f, 0.31f);
    colors[ImGuiCol_TitleBg] = rgb(0.085f, 0.09f, 0.095f);
    colors[ImGuiCol_TitleBgActive] = rgb(0.12f, 0.125f, 0.135f);
    colors[ImGuiCol_MenuBarBg] = rgb(0.085f, 0.09f, 0.095f);
    colors[ImGuiCol_ScrollbarBg] = rgb(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_ScrollbarGrab] = rgb(0.25f, 0.26f, 0.28f);
    colors[ImGuiCol_ScrollbarGrabHovered] = rgb(0.32f, 0.33f, 0.36f);
    colors[ImGuiCol_ScrollbarGrabActive] = accentDim;
    colors[ImGuiCol_CheckMark] = accent;
    colors[ImGuiCol_SliderGrab] = accentDim;
    colors[ImGuiCol_SliderGrabActive] = accent;
    colors[ImGuiCol_Button] = rgb(0.18f, 0.19f, 0.21f);
    colors[ImGuiCol_ButtonHovered] = rgb(0.25f, 0.26f, 0.29f);
    colors[ImGuiCol_ButtonActive] = accentDim;
    colors[ImGuiCol_Header] = rgb(0.20f, 0.21f, 0.23f);
    colors[ImGuiCol_HeaderHovered] = rgb(0.26f, 0.27f, 0.30f);
    colors[ImGuiCol_HeaderActive] = rgb(0.30f, 0.31f, 0.34f);
    colors[ImGuiCol_Separator] = rgb(0.20f, 0.21f, 0.23f);
    colors[ImGuiCol_SeparatorHovered] = accentDim;
    colors[ImGuiCol_SeparatorActive] = accent;
    colors[ImGuiCol_ResizeGrip] = rgb(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_ResizeGripHovered] = accentDim;
    colors[ImGuiCol_ResizeGripActive] = accent;
    colors[ImGuiCol_Tab] = rgb(0.105f, 0.11f, 0.12f);
    colors[ImGuiCol_TabHovered] = rgb(0.24f, 0.25f, 0.28f);
    colors[ImGuiCol_TabSelected] = rgb(0.17f, 0.175f, 0.19f);
    colors[ImGuiCol_TabSelectedOverline] = accent;
    colors[ImGuiCol_TabDimmed] = rgb(0.095f, 0.10f, 0.11f);
    colors[ImGuiCol_TabDimmedSelected] = rgb(0.14f, 0.145f, 0.16f);
    colors[ImGuiCol_TabDimmedSelectedOverline] = accentDim;
    colors[ImGuiCol_DockingPreview] = rgb(1.0f, 0.72f, 0.28f, 0.35f);
    colors[ImGuiCol_DockingEmptyBg] = rgb(0.06f, 0.06f, 0.07f);
    colors[ImGuiCol_PlotHistogram] = accentDim;
    colors[ImGuiCol_PlotHistogramHovered] = accent;
    colors[ImGuiCol_TableHeaderBg] = rgb(0.14f, 0.145f, 0.16f);
    colors[ImGuiCol_TableBorderStrong] = rgb(0.20f, 0.21f, 0.23f);
    colors[ImGuiCol_TableBorderLight] = rgb(0.16f, 0.17f, 0.18f);
    colors[ImGuiCol_TableRowBgAlt] = rgb(1.0f, 1.0f, 1.0f, 0.025f);
    colors[ImGuiCol_TextSelectedBg] = rgb(1.0f, 0.72f, 0.28f, 0.30f);
    colors[ImGuiCol_DragDropTarget] = accent;
    colors[ImGuiCol_NavCursor] = accent;
    colors[ImGuiCol_ModalWindowDimBg] = rgb(0.0f, 0.0f, 0.0f, 0.55f);
    // The system's interface font, unless editor.font names another; Dear ImGui's own when there is none.
    const std::string named =
        app.CVars()
            .Register<std::string>(
                "editor.font", "",
                "A .ttf file for the editor's text; empty takes the system's (Segoe UI, DejaVu Sans)")
            .Get();
    const float size =
        static_cast<float>(app.CVars().Register<double>("editor.font_size", 16.0, "Pixels of the editor's text").Get());
    std::vector<std::filesystem::path> fonts;
    if (!named.empty()) {
        fonts.emplace_back(named);
    } else {
        fonts = {"C:/Windows/Fonts/segoeui.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                 "/usr/share/fonts/TTF/DejaVuSans.ttf"};
    }
    if (debugui::DebugUi* overlay = app.DebugOverlay()) {
        for (const std::filesystem::path& font : fonts) {
            if (overlay->UseFont(font, std::clamp(size, 10.0f, 32.0f))) {
                break;
            }
        }
    }
}

bool Editor::IconButton(const char* id, Icon icon, bool active, const char* tip) {
    // A square button with a drawn icon: the toolbar's tools (ADR-0118).
    const float size = ImGui::GetFrameHeight();
    const ImVec2 corner = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(size, size));
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 end(corner.x + size, corner.y + size);
    if (active || hovered) {
        draw->AddRectFilled(corner, end,
                            ImGui::GetColorU32(active ? ImVec4(1.0f, 0.72f, 0.28f, 0.22f)
                                                      : ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered)),
                            3.0f);
    }
    const ImU32 color = ImGui::GetColorU32(active ? ImGui::GetStyleColorVec4(ImGuiCol_CheckMark)
                                                  : ImGui::GetStyleColorVec4(ImGuiCol_Text));
    const ImVec2 c((corner.x + end.x) * 0.5f, (corner.y + end.y) * 0.5f);
    const float r = size * 0.30f;
    const float line = std::max(1.5f, size / 14.0f);
    const auto arrowhead = [&](ImVec2 tip, ImVec2 direction) {
        const ImVec2 side(-direction.y, direction.x);
        const float head = r * 0.38f;
        draw->AddTriangleFilled(tip,
                                ImVec2(tip.x - direction.x * head + side.x * head * 0.7f,
                                       tip.y - direction.y * head + side.y * head * 0.7f),
                                ImVec2(tip.x - direction.x * head - side.x * head * 0.7f,
                                       tip.y - direction.y * head - side.y * head * 0.7f),
                                color);
    };
    switch (icon) {
    case Icon::Move:
        draw->AddLine(ImVec2(c.x - r, c.y), ImVec2(c.x + r, c.y), color, line);
        draw->AddLine(ImVec2(c.x, c.y - r), ImVec2(c.x, c.y + r), color, line);
        arrowhead(ImVec2(c.x + r, c.y), ImVec2(1, 0));
        arrowhead(ImVec2(c.x - r, c.y), ImVec2(-1, 0));
        arrowhead(ImVec2(c.x, c.y - r), ImVec2(0, -1));
        arrowhead(ImVec2(c.x, c.y + r), ImVec2(0, 1));
        break;
    case Icon::Rotate:
        draw->PathArcTo(c, r * 0.85f, 0.5f, 5.3f, 20);
        draw->PathStroke(color, ImDrawFlags_None, line);
        arrowhead(ImVec2(c.x + std::cos(5.3f) * r * 0.85f, c.y + std::sin(5.3f) * r * 0.85f), ImVec2(0.6f, 0.8f));
        break;
    case Icon::Scale:
        draw->AddRect(ImVec2(c.x - r, c.y - r * 0.2f), ImVec2(c.x + r * 0.2f, c.y + r), color, 1.0f, 0, line);
        draw->AddLine(ImVec2(c.x - r * 0.1f, c.y + r * 0.1f), ImVec2(c.x + r, c.y - r), color, line);
        arrowhead(ImVec2(c.x + r, c.y - r), ImVec2(0.707f, -0.707f));
        break;
    case Icon::Paint:
        draw->AddLine(ImVec2(c.x + r, c.y - r), ImVec2(c.x - r * 0.2f, c.y + r * 0.2f), color, line * 1.6f);
        draw->AddCircleFilled(ImVec2(c.x - r * 0.45f, c.y + r * 0.45f), r * 0.42f, color);
        break;
    case Icon::Measure:
        draw->AddLine(ImVec2(c.x - r, c.y + r * 0.6f), ImVec2(c.x + r, c.y - r * 0.6f), color, line);
        for (int tick = 0; tick <= 4; ++tick) {
            const float t = static_cast<float>(tick) / 4.0f;
            const ImVec2 at(c.x - r + 2.0f * r * t, c.y + r * 0.6f - 1.2f * r * t);
            const float length = tick % 2 == 0 ? r * 0.45f : r * 0.25f;
            draw->AddLine(at, ImVec2(at.x + length * 0.5f, at.y + length * 0.85f), color, line * 0.8f);
        }
        break;
    case Icon::Snap:
        // A magnet.
        draw->PathArcTo(ImVec2(c.x, c.y + r * 0.1f), r * 0.75f, 0.0f, 3.14159f, 16);
        draw->PathStroke(color, ImDrawFlags_None, line * 1.8f);
        draw->AddLine(ImVec2(c.x - r * 0.75f, c.y + r * 0.1f), ImVec2(c.x - r * 0.75f, c.y - r * 0.8f), color,
                      line * 1.8f);
        draw->AddLine(ImVec2(c.x + r * 0.75f, c.y + r * 0.1f), ImVec2(c.x + r * 0.75f, c.y - r * 0.8f), color,
                      line * 1.8f);
        break;
    case Icon::World:
        draw->AddCircle(c, r, color, 20, line);
        draw->AddEllipse(c, ImVec2(r * 0.45f, r), color, 0.0f, 20, line * 0.8f);
        draw->AddLine(ImVec2(c.x - r, c.y), ImVec2(c.x + r, c.y), color, line * 0.8f);
        break;
    case Icon::Local:
        draw->AddLine(c, ImVec2(c.x + r, c.y + r * 0.35f), color, line);
        draw->AddLine(c, ImVec2(c.x, c.y - r), color, line);
        draw->AddLine(c, ImVec2(c.x - r * 0.8f, c.y + r * 0.6f), color, line);
        draw->AddRect(ImVec2(c.x - r * 0.35f, c.y - r * 0.35f), ImVec2(c.x + r * 0.35f, c.y + r * 0.35f), color, 0.0f,
                      0, line * 0.7f);
        break;
    case Icon::Play:
        draw->AddTriangleFilled(ImVec2(c.x - r * 0.65f, c.y - r), ImVec2(c.x + r, c.y),
                                ImVec2(c.x - r * 0.65f, c.y + r), color);
        break;
    case Icon::Simulate:
        draw->AddCircle(c, r, color, 20, line);
        draw->AddCircleFilled(ImVec2(c.x, c.y - r * 0.15f), r * 0.32f, color);
        draw->AddLine(ImVec2(c.x - r * 0.6f, c.y + r * 0.55f), ImVec2(c.x + r * 0.6f, c.y + r * 0.55f), color, line);
        break;
    }
    if (hovered) {
        ImGui::SetTooltip("%s", tip);
    }
    return pressed;
}

void Editor::DrawShowMenu() {
    ImGui::MenuItem("Game view", Keys("view.game"), &m_show.gameView);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Only the game's image: no grid, markers, boxes or gizmo");
    }
    ImGui::MenuItem("Real time", Keys("view.realtime"), &m_realtime);
    ImGui::MenuItem("Hear the level", Keys("view.sound"), &m_hear);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Its ambient sounds near the camera, and what a sequence's preview plays");
    }
    ImGui::Separator();
    ImGui::TextDisabled("Markers");
    for (std::size_t category = 0; category < kCategoryCount; ++category) {
        bool shown = m_show.categories[category];
        if (ImGui::MenuItem(std::string(CategoryName(static_cast<Category>(category))).c_str(), nullptr, &shown)) {
            m_show.categories[category] = shown;
        }
    }
    ImGui::Separator();
    ImGui::TextDisabled("Over the view");
    ImGui::MenuItem("Grid", Keys("view.grid"), &m_show.grid);
    ImGui::MenuItem("Every light's reach", nullptr, &m_show.lightReach);
    ImGui::MenuItem("Collision boxes", nullptr, &m_show.collision);
    if (ImGui::MenuItem("Navigation mesh", nullptr, &m_show.navigation) && m_show.navigation) {
        m_navigation.reset();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Where zombies can walk, built from the level's models when turned on");
    }
    ImGui::MenuItem("Openings between rooms", nullptr, &m_show.portals);
    ImGui::Separator();
    ImGui::TextDisabled("In the scene");
    ImGui::MenuItem("Decals", nullptr, &m_show.decals);
    ImGui::MenuItem("Particles and ribbons", nullptr, &m_show.particles);
    ImGui::MenuItem("Fog", nullptr, &m_show.fog);
}

void Editor::DrawGizmo() {
    if (m_simulating) {
        m_gizmoUsing = false;
        return;
    }
    scene::LevelEntity* selected = Selected();
    const ecs::Entity entity =
        selected != nullptr && m_instance != nullptr ? m_instance->Find(m_selected) : ecs::Entity{};
    if (entity.IsNull() || LockedInEditor(m_level, m_selected)) {
        m_gizmoUsing = false;
        return;
    }
    ecs::World& world = m_scene->World();
    ecs::PropagateTransforms(world);
    if (!ImGuizmo::IsUsing()) {
        // The gizmo sits at the pivot (ADR-0107), turned as the primary is, without its scale.
        const glm::mat4 primary = world.Get<ecs::WorldTransform>(entity).matrix;
        const glm::vec3 pivot =
            PivotPoint(m_pivot, glm::vec3(primary[3]), m_pivot == Pivot::Origin ? Bounds{} : SelectionBounds());
        m_gizmoStart = glm::translate(glm::mat4(1.0f), pivot) * glm::mat4_cast(Decompose(primary).rotation);
        m_gizmoMatrix = m_gizmoStart;
        m_dragStart.clear();
    }
    glm::mat4 matrix = m_gizmoMatrix;
    const glm::vec2 extent = m_viewportMax - m_viewportMin;
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());
    ImGuizmo::SetRect(m_viewportMin.x, m_viewportMin.y, extent.x, extent.y);
    const glm::mat4 view = Eye().View();
    const glm::mat4 projection = Projection();
    const auto operation = static_cast<ImGuizmo::OPERATION>(m_operation == 0 ? ImGuizmo::TRANSLATE : m_operation);
    const ImGuizmo::MODE mode = m_local || operation == ImGuizmo::SCALE ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
    // Snapping, or its opposite while Ctrl is held.
    const bool snap = m_snap != ImGui::GetIO().KeyCtrl;
    const float step = operation == ImGuizmo::ROTATE  ? m_snapSteps.y
                       : operation == ImGuizmo::SCALE ? m_snapSteps.z
                                                      : m_snapSteps.x;
    const float steps[3] = {step, step, step};
    const bool changed = ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection), operation, mode,
                                              glm::value_ptr(matrix), nullptr, snap ? steps : nullptr);
    if (ImGuizmo::IsUsing() && m_dragStart.empty()) {
        // Where each selected entity was when the drag began: the gizmo's motion since then is applied to it, so
        // that nothing drifts or compounds over a long drag.
        for (const std::uint64_t id : SelectionRoots()) {
            const ecs::Entity created = m_instance->Find(id);
            if (!created.IsNull() && !LockedInEditor(m_level, id)) {
                m_dragStart.emplace_back(id, world.Get<ecs::WorldTransform>(created).matrix);
            }
        }
    }
    m_gizmoUsing = ImGuizmo::IsUsing();
    if (!changed) {
        return;
    }
    // Onto the surface under the mouse, or its nearest corner with V, past the selection itself (ADR-0108).
    const ImGuiIO& io = ImGui::GetIO();
    const bool vertex = ImGui::IsKeyDown(ImGuiKey_V);
    if (operation == ImGuizmo::TRANSLATE && (m_surfaceSnap || (io.KeyCtrl && io.KeyShift) || vertex)) {
        const auto selectedPart = [this](ecs::Entity drawn) {
            for (std::uint64_t id = m_instance->IdOf(drawn); id != 0;) {
                if (IsSelected(id)) {
                    return true;
                }
                const scene::LevelEntity* above = m_level.Find(id);
                id = above != nullptr ? above->parent : 0;
            }
            return false;
        };
        const auto [origin, direction] = RayAt(glm::vec2(io.MousePos.x, io.MousePos.y));
        if (const std::optional<PickHit> hit = m_picker.Cast(world, origin, direction, selectedPart)) {
            glm::vec3 target = hit->position;
            if (vertex) {
                target = *std::ranges::min_element(
                    hit->corners, {}, [&](const glm::vec3& corner) { return glm::distance(corner, hit->position); });
            }
            glm::quat turn = Decompose(m_gizmoStart).rotation;
            if (m_alignToSurface && !vertex) {
                // Up turned to the surface's normal, keeping the rest of the turn.
                turn = TurnBetween(glm::vec3(0.0f, 1.0f, 0.0f), hit->normal) * turn;
            }
            matrix = glm::translate(glm::mat4(1.0f), target) * glm::mat4_cast(turn);
        }
    }
    m_gizmoMatrix = matrix;
    const glm::mat4 delta = matrix * glm::inverse(m_gizmoStart);
    std::vector<std::pair<std::uint64_t, math::Transform>> moved;
    for (const auto& [id, start] : m_dragStart) {
        const ecs::Entity created = m_instance->Find(id);
        if (created.IsNull()) {
            continue;
        }
        const ecs::Entity parent = world.Parent(created);
        const glm::mat4 parentWorld = parent.IsNull() ? glm::mat4(1.0f) : world.Get<ecs::WorldTransform>(parent).matrix;
        moved.emplace_back(id, Decompose(glm::inverse(parentWorld) * delta * start));
    }
    const char* label = operation == ImGuizmo::ROTATE ? "Rotate" : operation == ImGuizmo::SCALE ? "Scale" : "Move";
    Change(label, [&moved](scene::Level& level) {
        for (const auto& [id, transform] : moved) {
            if (scene::LevelEntity* found = level.Find(id)) {
                found->transform = transform;
            }
        }
    });
}

void Editor::DrawGrid() {
    if (!m_show.grid || m_show.gameView) {
        return;
    }
    if (m_panes[m_drawPane].kind != ViewKind::Perspective) {
        DrawAxisGrid();
        return;
    }
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    draw->PushClipRect({m_viewportMin.x, m_viewportMin.y}, {m_viewportMax.x, m_viewportMax.y}, true);
    constexpr int kReach = 40;     // Meters around the camera.
    constexpr float kPiece = 2.0f; // Lines are drawn in pieces, each faded by its distance.
    const glm::vec2 center(std::round(m_camera.position.x), std::round(m_camera.position.z));
    const float height = std::abs(m_camera.position.y);
    for (int axis = 0; axis < 2; ++axis) {
        for (int line = -kReach; line <= kReach; ++line) {
            const float across = (axis == 0 ? center.x : center.y) + static_cast<float>(line);
            const bool major = static_cast<int>(std::round(across)) % 10 == 0;
            for (float along = -static_cast<float>(kReach); along < static_cast<float>(kReach); along += kPiece) {
                const float a = (axis == 0 ? center.y : center.x) + along;
                const glm::vec3 from = axis == 0 ? glm::vec3(across, 0.0f, a) : glm::vec3(a, 0.0f, across);
                const glm::vec3 to =
                    axis == 0 ? glm::vec3(across, 0.0f, a + kPiece) : glm::vec3(a + kPiece, 0.0f, across);
                const float distance = glm::length(glm::vec2((from.x + to.x) * 0.5f - m_camera.position.x,
                                                             (from.z + to.z) * 0.5f - m_camera.position.z));
                const float fade =
                    std::clamp(1.0f - distance / (static_cast<float>(kReach) * 0.9f + height), 0.0f, 1.0f);
                if (fade <= 0.0f) {
                    continue;
                }
                const std::optional<glm::vec2> a2 = Project(from);
                const std::optional<glm::vec2> b2 = Project(to);
                if (a2 && b2) {
                    const auto alpha =
                        static_cast<int>(fade * (major ? 110.0f : 45.0f) * m_depth.Visibility((from + to) * 0.5f));
                    draw->AddLine({a2->x, a2->y}, {b2->x, b2->y}, IM_COL32(200, 200, 210, alpha), major ? 1.5f : 1.0f);
                }
            }
        }
    }
    draw->PopClipRect();
}

void Editor::Pick(app::Application& app) {
    const ImGuiIO& io = ImGui::GetIO();
    if (!app.Input().WasPressed(platform::MouseButton::Left) || !MouseInViewport() || io.WantCaptureMouse ||
        ImGuizmo::IsOver() || m_gizmoUsing || m_instance == nullptr) {
        return;
    }
    const glm::vec2 mouse(io.MousePos.x, io.MousePos.y);
    // Markers first: they stand for what has nothing to click on.
    std::uint64_t nearest = 0;
    ecs::Entity drawn;
    float nearestDistance = kPickDistance;
    ecs::World& world = m_scene->World();
    for (const scene::LevelEntity& entity : m_level.entities) {
        if (!entity.model.empty() || (!IsSelected(entity.id) && !m_show.Shows(CategoryOf(entity))) ||
            LockedInEditor(m_level, entity.id)) {
            continue;
        }
        const ecs::Entity created = m_instance->Find(entity.id);
        if (created.IsNull()) {
            continue;
        }
        if (const std::optional<glm::vec2> at = Project(glm::vec3(world.Get<ecs::WorldTransform>(created).matrix[3]))) {
            const float distance = glm::distance(*at, mouse);
            if (distance < nearestDistance) {
                nearestDistance = distance;
                nearest = entity.id;
            }
        }
    }
    if (nearest == 0) {
        const auto [origin, direction] = RayAt(mouse);
        // Locked entities let the click through to what is behind them (ADR-0107).
        const auto locked = [this](ecs::Entity entity) {
            const std::uint64_t id = m_instance->IdOf(entity);
            return id != 0 && LockedInEditor(m_level, id);
        };
        if (const std::optional<PickHit> hit = m_picker.Cast(world, origin, direction, locked)) {
            nearest = m_instance->IdOf(hit->entity);
            drawn = hit->entity;
            // A prefab's part: the inspector opens it, to change it in this copy (ADR-0113).
            m_selectedPart = m_instance->PartOf(nearest, hit->entity);
            m_openPart = m_selectedPart != 0;
        }
    }
    if (io.KeyCtrl) {
        ToggleSelect(nearest);
    } else {
        Select(nearest);
    }
    if (!drawn.IsNull()) {
        ShowMaterialOf(drawn);
    }
}

std::pair<glm::vec3, glm::vec3> Editor::RayAt(const glm::vec2& point) const {
    const glm::vec2 extent = glm::max(m_viewportMax - m_viewportMin, glm::vec2(1.0f));
    const glm::vec2 ndc((point.x - m_viewportMin.x) / extent.x * 2.0f - 1.0f,
                        1.0f - (point.y - m_viewportMin.y) / extent.y * 2.0f);
    const glm::mat4 inverse = glm::inverse(Projection() * Eye().View());
    const glm::vec4 near = inverse * glm::vec4(ndc, -1.0f, 1.0f);
    const glm::vec4 far = inverse * glm::vec4(ndc, 1.0f, 1.0f);
    const glm::vec3 origin = glm::vec3(near) / near.w;
    return {origin, glm::normalize(glm::vec3(far) / far.w - origin)};
}

glm::vec3 Editor::PlaceAt(const glm::vec2& point) {
    const auto [origin, direction] = RayAt(point);
    if (m_scene != nullptr) {
        ecs::PropagateTransforms(m_scene->World());
        if (const std::optional<PickHit> hit = m_picker.Cast(m_scene->World(), origin, direction);
            hit && hit->distance < 200.0f) {
            return hit->position;
        }
    }
    return origin + direction * 6.0f;
}

} // namespace editor
