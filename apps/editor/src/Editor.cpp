// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "Editor.hpp"

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
    // A mirrored matrix keeps a proper rotation and a negative scale.
    if (glm::dot(glm::cross(axes[0], axes[1]), axes[2]) < 0.0f) {
        transform.scale.x = -transform.scale.x;
        axes[0] = -axes[0];
    }
    transform.rotation = glm::normalize(glm::quat_cast(glm::mat3(axes[0], axes[1], axes[2])));
    return transform;
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

} // namespace

glm::mat4 EditorCamera::World() const {
    math::Transform transform;
    transform.translation = position;
    transform.rotation = math::FromYawPitchRoll(yaw, pitch, 0.0f);
    return math::ToMatrix(transform);
}

glm::mat4 EditorCamera::View() const {
    return glm::inverse(World());
}

glm::vec3 EditorCamera::Forward() const {
    return math::FromYawPitchRoll(yaw, pitch, 0.0f) * math::kForward;
}

core::Result<void> Editor::OnStart(app::Application& app) {
    m_app = &app;
    m_assetRoot = FindAssetRoot();
    const std::string level =
        app.CVars()
            .Register<std::string>("editor.level", "Factory/Factory.level.json", "Level file to open, under assets/")
            .Get();
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
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 2.0f;
    style.FrameRounding = 2.0f;
    style.TabRounding = 2.0f;
    style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    app.MainWindow().SetTextInput(true);
    m_browser = std::make_unique<AssetBrowser>(m_assetRoot, app.Renderer().Device(), app.Workers());
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
    LoadBookmarks();
    if (core::Result<void> opened = Open(app, level); !opened) {
        // An editor with no level still opens, on an empty one.
        DAI_LOG_WARNING(kModule, "{}", core::Format(opened.error()));
        m_status = core::Format(opened.error());
        m_level = {};
        m_levelPath = level;
    }
    return {};
}

core::Result<void> Editor::Open(app::Application& app, const std::string& levelPath) {
    core::Result<scene::Level> level = scene::LoadLevelFile(m_assetRoot / std::filesystem::path(levelPath));
    if (!level) {
        return tl::unexpected(level.error());
    }
    // The editor is silent: effects and lights play, the game's sounds do not.
    auto scene = std::make_unique<scene::Scene>(app.Renderer(), m_assetRoot, nullptr);
    scene::Scene& target = *scene;
    for (const std::string& model : scene::LevelModels(*level, levelPath)) {
        if (core::Result<const scene::Model*> loaded = target.Assets().LoadModel(model); !loaded) {
            DAI_LOG_WARNING(kModule, "{}", core::Format(loaded.error()));
        }
    }
    m_instance = std::make_unique<scene::LevelInstance>(target, levelPath);
    m_instance->Build(*level);
    m_cameraEntity = target.World().Create();
    target.World().Add<ecs::LocalTransform>(m_cameraEntity);
    target.World().Add<scene::Camera>(
        m_cameraEntity, scene::Camera{.verticalFov = m_camera.verticalFov, .nearPlane = m_camera.nearPlane});
    target.SetActiveCamera(m_cameraEntity);
    m_picker.Rebuild(target.World());

    for (const scene::LevelEntity& entity : level->entities) {
        if (entity.game && entity.game->kind == "player") {
            m_camera.position = entity.transform.translation + glm::vec3(0.0f, 0.3f, 0.0f);
            m_camera.yaw = 0.0f;
            m_camera.pitch = -0.1f;
            break;
        }
    }
    m_level = std::move(*level);
    m_levelPath = levelPath;
    m_decalImages.clear();
    m_stroking = false;
    Select(0);
    m_history.Clear();
    m_savedRevision = m_history.Current();
    m_autosavedRevision = m_history.Current();
    m_sinceAutosave = 0.0f;
    m_scene = &target;
    app.SetScene(std::move(scene));
    if (m_browser != nullptr) {
        m_browser->Open(std::filesystem::path(levelPath).parent_path().generic_string());
    }
    m_status = std::format("Opened {}: {} entities", levelPath, m_level.entities.size());
    DAI_LOG_INFO(kModule, "{}", m_status);
    UpdateTitle(app);
    CheckAutosave();
    return {};
}

bool Editor::Save(const std::string& levelPath) {
    m_history.Commit(m_level);
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
        const EditorCamera camera = m_camera;
        if (core::Result<void> opened = Open(*m_app, target); !opened) {
            m_status = core::Format(opened.error());
            return false;
        }
        m_camera = camera;
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
    m_history.Begin(m_level, label);
    const scene::Level before = m_level;
    edit(m_level);
    if (m_instance != nullptr && m_instance->Sync(before, m_level).models) {
        m_picker.Rebuild(m_scene->World());
    }
}

void Editor::Apply(const scene::LevelEntity& after, std::string_view label) {
    const scene::LevelEntity* before = m_level.Find(after.id);
    if (before == nullptr || *before == after) {
        return;
    }
    Change(label, [&after](scene::Level& level) { *level.Find(after.id) = after; });
}

void Editor::GoTo(scene::Level level) {
    const scene::Level before = std::move(m_level);
    m_level = std::move(level);
    if (m_instance != nullptr && m_instance->Sync(before, m_level).models) {
        m_picker.Rebuild(m_scene->World());
    }
    std::erase_if(m_selection, [this](std::uint64_t id) { return m_level.Find(id) == nullptr; });
    m_selected = m_selection.empty() ? 0 : m_selection.back();
    m_eulerOf = 0;
}

void Editor::Undo() {
    m_history.Commit(m_level);
    const std::string label = m_history.UndoLabel();
    if (std::optional<scene::Level> level = m_history.Undo(m_level)) {
        GoTo(std::move(*level));
        m_status = std::format("Undid: {}", label);
    }
}

void Editor::Redo() {
    m_history.Commit(m_level);
    const std::string label = m_history.RedoLabel();
    if (std::optional<scene::Level> level = m_history.Redo(m_level)) {
        GoTo(std::move(*level));
        m_status = std::format("Redid: {}", label);
    }
}

void Editor::Create(std::string_view label, scene::LevelEntity entity, std::optional<glm::vec3> position) {
    m_history.Commit(m_level);
    entity.id = m_level.NextId();
    entity.parent = 0;
    if (entity.name.empty()) {
        entity.name = std::string(label);
    }
    entity.transform.translation = position.value_or(m_camera.position + m_camera.Forward() * 4.0f);
    Change(std::format("Create {}", label), [&entity](scene::Level& level) { level.entities.push_back(entity); });
    m_history.Commit(m_level);
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
        m_history.Commit(m_level);
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
    m_history.Commit(m_level);
    Change(label, [&doomed](scene::Level& level) {
        std::erase_if(level.entities,
                      [&doomed](const scene::LevelEntity& entity) { return doomed.contains(entity.id); });
    });
    m_history.Commit(m_level);
    m_status = std::format("{}: {} entities gone", label, doomed.size());
    Select(0);
}

void Editor::Duplicate() {
    if (m_selection.empty()) {
        return;
    }
    const std::vector<scene::LevelEntity> copies = CopyEntities(m_level, SelectionRoots());
    std::vector<std::uint64_t> roots;
    m_history.Commit(m_level);
    Change("Duplicate", [&](scene::Level& level) { roots = PasteEntities(level, copies); });
    m_history.Commit(m_level);
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
    m_history.Commit(m_level);
    Change("Paste", [&](scene::Level& level) { roots = PasteEntities(level, std::move(clip->entities)); });
    m_history.Commit(m_level);
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
    m_history.Commit(m_level);
    Apply(moved, std::format("Move {} under {}", EntityLabel(*entity),
                             target != nullptr ? EntityLabel(*target) : std::string("the root")));
    m_history.Commit(m_level);
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
    m_history.Commit(m_level);
    Change(std::format("Make the prefab {}", placed.name), [&](scene::Level& level) {
        std::erase_if(level.entities, [&gone](const scene::LevelEntity& entity) { return gone.contains(entity.id); });
        level.entities.push_back(placed);
    });
    m_history.Commit(m_level);
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
    scene::Relocate(*prefab, path, m_levelPath);
    const std::string label = EntityLabel(*entity);
    m_history.Commit(m_level);
    Change(std::format("Unpack {}", label), [&](scene::Level& level) { Unpack(level, id, std::move(*prefab)); });
    m_history.Commit(m_level);
    m_status = std::format("Unpacked {}: its entities are the level's now", label);
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
    const float spin = m_brush.randomSpin ? unit(m_random) * glm::two_pi<float>() : 0.0f;
    const float scale = 1.0f + (unit(m_random) * 2.0f - 1.0f) * std::clamp(m_brush.sizeJitter, 0.0f, 0.9f);
    scene::LevelEntity stamp = Stamp(m_brush, position, normal, spin, scale);
    const std::string kind = m_brush.kind == PaintBrush::Kind::Wet
                                 ? std::string("wet")
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
        m_history.Commit(m_level);
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
        m_history.Commit(m_level);
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
    } else {
        ImGui::SliderFloat("Wetness", &m_brush.wetness, 0.0f, 1.0f);
        ImGui::SliderFloat("Puddles", &m_brush.puddles, 0.0f, 1.0f);
    }
    ImGui::End();
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
    m_history.Commit(m_level);
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
    m_history.Commit(m_level);
    if (!Dirty()) {
        return true;
    }
    Guard(app, Pending::Quit);
    return false;
}

void Editor::OnStop(app::Application& /*app*/) {
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
    m_history.Commit(m_level);
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
    ImGuizmo::BeginFrame();
    DrawMenu(app);
    DrawDockspace();
    DrawHierarchy();
    DrawInspector();
    DrawEnvironment();
    DrawPaint();
    DrawStatus(app, time);
    DrawDialogs(app);
    if (m_browser != nullptr) {
        if (const std::optional<AssetEntry> chosen = m_browser->Draw()) {
            PlaceAsset(app, *chosen, std::nullopt);
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

    if (m_scene != nullptr) {
        UpdateCamera(app, static_cast<float>(time.unclampedDelta.count()));
        DrawGrid();
        DrawMarkers();
        if (m_painting) {
            DrawBrush();
            PaintStrokes(app);
        } else {
            if (m_stroking) {
                m_stroking = false;
                m_history.Commit(m_level);
            }
            DrawGizmo();
            Pick(app);
        }
    }
    HandleShortcuts(app);
    DrawHelp();

    // What the inspector asked for, once it is drawn.
    if (!m_pendingOpenPrefab.empty()) {
        Guard(app, Pending::Open, std::exchange(m_pendingOpenPrefab, {}));
    }
    if (m_pendingUnpack != 0) {
        UnpackPrefab(std::exchange(m_pendingUnpack, 0));
    }

    // An edit ends when the mouse lets go of the widget or the gizmo that made it.
    if (m_history.IsOpen() && !ImGui::IsAnyItemActive() && !ImGuizmo::IsUsing() && !m_stroking) {
        m_history.Commit(m_level);
    }
    Autosave(static_cast<float>(time.unclampedDelta.count()));
    if (!m_playFile.empty() && !m_play.Running()) {
        std::error_code error;
        std::filesystem::remove(m_playFile, error);
        m_playFile.clear();
    }
    UpdateTitle(app);
    if (m_scene != nullptr && !m_cameraEntity.IsNull()) {
        static_cast<math::Transform&>(m_scene->World().Get<ecs::LocalTransform>(m_cameraEntity)) =
            Decompose(m_camera.World());
    }
}

void Editor::HandleShortcuts(app::Application& /*app*/) {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput || m_flying || !m_openPopup.empty() ||
        ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
        return;
    }
    const auto pressed = [](ImGuiKey key) { return ImGui::IsKeyPressed(key, false); };
    if (io.KeyCtrl) {
        if (pressed(ImGuiKey_S)) {
            if (io.KeyShift) {
                m_pathField = m_levelPath;
                m_openPopup = "Save As";
            } else {
                Save();
            }
        } else if (pressed(ImGuiKey_O)) {
            m_levelFiles = FindLevelFiles(m_assetRoot);
            m_openPopup = "Open";
        } else if (pressed(ImGuiKey_N)) {
            m_pathField = "Levels/New.level.json";
            m_openPopup = "New";
        } else if (ImGui::IsKeyPressed(ImGuiKey_Z, true)) {
            if (io.KeyShift) {
                Redo();
            } else {
                Undo();
            }
        } else if (ImGui::IsKeyPressed(ImGuiKey_Y, true)) {
            Redo();
        } else if (pressed(ImGuiKey_F5)) {
            Play(true);
        } else if (pressed(ImGuiKey_D)) {
            Duplicate();
        } else if (pressed(ImGuiKey_C)) {
            CopySelection();
        } else if (pressed(ImGuiKey_X)) {
            CopySelection();
            DeleteSelected();
        } else if (pressed(ImGuiKey_V)) {
            Paste();
        } else if (pressed(ImGuiKey_A)) {
            SelectAll();
        }
        for (int slot = 1; slot <= 9; ++slot) {
            if (pressed(static_cast<ImGuiKey>(ImGuiKey_1 + slot - 1))) {
                Bookmark(slot, true);
            }
        }
        return;
    }
    for (int slot = 1; slot <= 9; ++slot) {
        if (pressed(static_cast<ImGuiKey>(ImGuiKey_1 + slot - 1))) {
            Bookmark(slot, false);
        }
    }
    if (pressed(ImGuiKey_G)) {
        m_grid = !m_grid;
    }
    if (pressed(ImGuiKey_X)) {
        m_snap = !m_snap;
        m_status = m_snap ? "Snapping on" : "Snapping off";
    }
    if (pressed(ImGuiKey_F1)) {
        m_showHelp = !m_showHelp;
    }
    if (pressed(ImGuiKey_F5)) {
        Play(false);
    }
    if (pressed(ImGuiKey_W)) {
        m_operation = ImGuizmo::TRANSLATE;
    }
    if (pressed(ImGuiKey_E)) {
        m_operation = ImGuizmo::ROTATE;
    }
    if (pressed(ImGuiKey_R)) {
        m_operation = ImGuizmo::SCALE;
    }
    if (pressed(ImGuiKey_B)) {
        m_painting = !m_painting;
    }
    if (pressed(ImGuiKey_Escape)) {
        if (m_painting) {
            m_painting = false;
        } else {
            Select(0);
        }
    }
    if (pressed(ImGuiKey_Delete)) {
        DeleteSelected();
    }
    if (pressed(ImGuiKey_F)) {
        FrameSelection();
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
    const glm::vec3 center = (bounds.min + bounds.max) * 0.5f;
    const float radius = glm::length(bounds.max - bounds.min) * 0.5f;
    const float distance = std::max(radius / std::tan(m_camera.verticalFov * 0.5f), 1.0f);
    m_camera.position = center - m_camera.Forward() * distance;
}
void Editor::OnRender(app::Application& app, renderer::SceneView& view) {
    // The scene fills the dockspace's central node, in pixels (ADR-0055).
    const platform::Extent size = app.MainWindow().Size();
    const platform::Extent pixels = app.MainWindow().PixelSize();
    const float scaleX = size.width > 0 ? static_cast<float>(pixels.width) / static_cast<float>(size.width) : 1.0f;
    const float scaleY = size.height > 0 ? static_cast<float>(pixels.height) / static_cast<float>(size.height) : 1.0f;
    const glm::vec2 extent = m_viewportMax - m_viewportMin;
    if (extent.x >= 8.0f && extent.y >= 8.0f) {
        view.viewport = renderer::ViewRect{.x = static_cast<std::uint32_t>(std::max(m_viewportMin.x, 0.0f) * scaleX),
                                           .y = static_cast<std::uint32_t>(std::max(m_viewportMin.y, 0.0f) * scaleY),
                                           .width = static_cast<std::uint32_t>(extent.x * scaleX),
                                           .height = static_cast<std::uint32_t>(extent.y * scaleY)};
    }
}

void Editor::DrawMenu(app::Application& app) {
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New...", "Ctrl+N")) {
            m_pathField = "Levels/New.level.json";
            m_openPopup = "New";
        }
        if (ImGui::MenuItem("Open...", "Ctrl+O")) {
            m_levelFiles = FindLevelFiles(m_assetRoot);
            m_openPopup = "Open";
        }
        if (ImGui::MenuItem("Save", "Ctrl+S")) {
            Save();
        }
        if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) {
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
        if (ImGui::MenuItem("Cut", "Ctrl+X", false, !m_selection.empty())) {
            CopySelection();
            DeleteSelected();
        }
        if (ImGui::MenuItem("Copy", "Ctrl+C", false, !m_selection.empty())) {
            CopySelection();
        }
        if (ImGui::MenuItem("Paste", "Ctrl+V")) {
            Paste();
        }
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, !m_selection.empty())) {
            Duplicate();
        }
        if (ImGui::MenuItem("Delete", "Del", false, !m_selection.empty())) {
            DeleteSelected();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Select all", "Ctrl+A")) {
            SelectAll();
        }
        if (ImGui::MenuItem("Select none", "Esc", false, !m_selection.empty())) {
            Select(0);
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
        if (ImGui::MenuItem("Frame the selection", "F", false, Selected() != nullptr)) {
            FrameSelection();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Create")) {
        if (ImGui::MenuItem("Empty")) {
            Create("Empty", {});
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
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Play")) {
        if (ImGui::MenuItem("From the start", "F5")) {
            Play(false);
        }
        if (ImGui::MenuItem("From the camera", "Ctrl+F5")) {
            Play(true);
        }
        if (ImGui::MenuItem("Stop", nullptr, false, m_play.Running())) {
            m_play.Kill();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Grid", "G", &m_grid);
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
    if (ImGui::BeginMenu("Help")) {
        ImGui::MenuItem("Shortcuts", "F1", &m_showHelp);
        ImGui::EndMenu();
    }
    ImGui::TextDisabled("|");
    const auto tool = [this](const char* label, int operation, const char* tip) {
        const bool active = m_operation == operation;
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_CheckMark));
        }
        if (ImGui::MenuItem(label)) {
            m_operation = operation;
        }
        if (active) {
            ImGui::PopStyleColor();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tip);
        }
    };
    tool("Move", ImGuizmo::TRANSLATE, "W");
    tool("Rotate", ImGuizmo::ROTATE, "E");
    tool("Scale", ImGuizmo::SCALE, "R");
    if (m_painting) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_CheckMark));
    }
    if (ImGui::MenuItem("Paint")) {
        m_painting = !m_painting;
    }
    if (m_painting) {
        ImGui::PopStyleColor();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("B: paint decals and wet patches on surfaces");
    }
    if (ImGui::MenuItem(m_local ? "Local" : "World")) {
        m_local = !m_local;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("The axes the gizmo moves along: the entity's own, or the world's");
    }
    if (m_snap) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_CheckMark));
    }
    if (ImGui::MenuItem("Snap")) {
        m_snap = !m_snap;
    }
    if (m_snap) {
        ImGui::PopStyleColor();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("X: gizmos move by %.2f m, turn by %.0f degrees and scale by %.2f (View sets the steps)",
                          m_snapSteps.x, m_snapSteps.y, m_snapSteps.z);
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
                Guard(app, Pending::Open, file);
            }
        }
        ImGui::EndChild();
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
    if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextWrapped("%s has changes that are not saved.", m_levelPath.c_str());
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
                m_history.Commit(m_level);
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
        ImGui::DockBuilderDockWindow("Assets", bottom);
        ImGui::DockBuilderDockWindow("Status", bottom);
        ImGui::DockBuilderFinish(dockspace);
    }
    m_layoutBuilt = true;
    // A layout saved before the browser existed: it joins the status.
    if (ImGui::FindWindowSettingsByID(ImHashStr("Assets")) == nullptr) {
        if (const ImGuiWindow* status = ImGui::FindWindowByName("Status"); status != nullptr && status->DockId != 0) {
            ImGui::DockBuilderDockWindow("Assets", status->DockId);
        }
    }
    if (const ImGuiDockNode* central = ImGui::DockBuilderGetCentralNode(dockspace)) {
        m_viewportMin = {central->Pos.x, central->Pos.y};
        m_viewportMax = {central->Pos.x + central->Size.x, central->Pos.y + central->Size.y};
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
                                            "Find by name or kind");
    }
    ImGui::BeginChild("entities");
    const bool control = ImGui::GetIO().KeyCtrl;
    if (!m_hierarchyFilter.empty()) {
        // A flat list of what matches.
        const std::string filter = Lower(m_hierarchyFilter);
        for (const scene::LevelEntity& entity : m_level.entities) {
            const std::string name = entity.name.empty() ? std::format("Entity {}", entity.id) : entity.name;
            const std::string kind = entity.game ? entity.game->kind : std::string();
            if (Lower(name).find(filter) == std::string::npos && Lower(kind).find(filter) == std::string::npos) {
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
    for (const std::uint64_t id : children.back()) {
        DrawHierarchyNode(id, children, indexOf);
    }
    // The space below the entities takes one dropped there to the root.
    const ImVec2 space = ImGui::GetContentRegionAvail();
    ImGui::InvisibleButton("root", ImVec2(std::max(space.x, 1.0f), std::max(space.y, 20.0f)));
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DAIDALOS_ENTITY")) {
            Reparent(*static_cast<const std::uint64_t*>(payload->Data), 0);
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
void Editor::DrawHierarchyNode(std::uint64_t id, const std::vector<std::vector<std::uint64_t>>& children,
                               const std::vector<std::size_t>& indexOf) {
    const auto found = std::ranges::find(m_level.entities, id, &scene::LevelEntity::id);
    const std::size_t slot = static_cast<std::size_t>(found - m_level.entities.begin());
    const scene::LevelEntity& entity = *found;
    const std::vector<std::uint64_t>& mine = children[slot];
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
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
    const std::string label = std::format("[{}] {}##{}", KindLabel(entity),
                                          entity.name.empty() ? std::format("Entity {}", id) : entity.name, id);
    const bool open = ImGui::TreeNodeEx(label.c_str(), flags);
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
    // Dragged onto another entity, it goes under it.
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
        ImGui::TextDisabled("Its entities are edited in the prefab's file, and change everywhere it is placed.");
    }
    changed |= InspectComponents(edited);
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
    return glm::perspective(m_camera.verticalFov, extent.x / extent.y, m_camera.nearPlane, m_camera.farPlane);
}

std::optional<glm::vec2> Editor::Project(const glm::vec3& point) const {
    const glm::vec4 clip = Projection() * m_camera.View() * glm::vec4(point, 1.0f);
    if (clip.w <= m_camera.nearPlane) {
        return std::nullopt;
    }
    const glm::vec2 ndc = glm::vec2(clip) / clip.w;
    const glm::vec2 extent = m_viewportMax - m_viewportMin;
    return m_viewportMin + glm::vec2((ndc.x * 0.5f + 0.5f) * extent.x, (0.5f - ndc.y * 0.5f) * extent.y);
}

void Editor::DrawMarkers() {
    if (m_instance == nullptr) {
        return;
    }
    ecs::World& world = m_scene->World();
    ecs::PropagateTransforms(world);
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    draw->PushClipRect({m_viewportMin.x, m_viewportMin.y}, {m_viewportMax.x, m_viewportMax.y}, true);
    const auto line = [&](const glm::vec3& from, const glm::vec3& to, ImU32 color, float thickness) {
        const std::optional<glm::vec2> a = Project(from);
        const std::optional<glm::vec2> b = Project(to);
        if (a && b) {
            draw->AddLine({a->x, a->y}, {b->x, b->y}, color, thickness);
        }
    };
    const auto box = [&](const glm::mat4& matrix, const glm::vec3& min, const glm::vec3& max, ImU32 color,
                         float thickness) {
        for (const auto& edge : kCubeEdges) {
            line(math::TransformPoint(matrix, CubeCorner(edge[0], min, max)),
                 math::TransformPoint(matrix, CubeCorner(edge[1], min, max)), color, thickness);
        }
    };
    for (const scene::LevelEntity& entity : m_level.entities) {
        const ecs::Entity created = m_instance->Find(entity.id);
        if (created.IsNull()) {
            continue;
        }
        const glm::mat4& matrix = world.Get<ecs::WorldTransform>(created).matrix;
        const bool selected = IsSelected(entity.id);
        const float distance = glm::distance(glm::vec3(matrix[3]), m_camera.position);
        if (!selected && distance > kMarkerReach) {
            continue;
        }
        const float fade = selected ? 1.0f : std::clamp(1.5f - distance / kMarkerReach * 1.5f, 0.15f, 1.0f);
        const ImU32 alpha = static_cast<ImU32>(fade * 255.0f) << 24;
        const ImU32 color = selected ? IM_COL32(255, 220, 60, 255) : ((MarkerColor(entity) & 0x00FFFFFFU) | alpha);
        // Volumes show their box; a selected light shows where it points and how far it reaches.
        if (entity.fog || entity.wind || entity.weather || entity.lightning || entity.decal || entity.wet ||
            entity.trigger) {
            box(matrix, glm::vec3(-0.5f), glm::vec3(0.5f),
                (color & 0x00FFFFFFU) | (selected ? 0xFF000000U : static_cast<ImU32>(fade * 90.0f) << 24),
                selected ? 2.0f : 1.0f);
        }
        const glm::vec3 position(matrix[3]);
        if (entity.light && entity.light->type != renderer::LightType::Point) {
            const glm::vec3 axis = glm::normalize(glm::vec3(matrix * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
            line(position, position + axis * (selected ? std::min(entity.light->range, 5.0f) : 0.6f), color, 1.5f);
        }
        if (selected && entity.light && entity.light->type != renderer::LightType::Directional) {
            // The range, on three circles.
            for (int plane = 0; plane < 3; ++plane) {
                glm::vec3 previous{0.0f};
                for (int step = 0; step <= 32; ++step) {
                    const float angle = static_cast<float>(step) / 32.0f * glm::two_pi<float>();
                    glm::vec3 offset{0.0f};
                    offset[plane] = std::cos(angle) * entity.light->range;
                    offset[(plane + 1) % 3] = std::sin(angle) * entity.light->range;
                    if (step > 0) {
                        line(position + previous, position + offset, IM_COL32(255, 220, 60, 90), 1.0f);
                    }
                    previous = offset;
                }
            }
        }
        if (entity.model.empty()) {
            if (const std::optional<glm::vec2> at = Project(position)) {
                draw->AddCircleFilled({at->x, at->y}, kMarkerRadius,
                                      IM_COL32(20, 20, 20, static_cast<int>(fade * 200.0f)));
                draw->AddCircle({at->x, at->y}, kMarkerRadius, color, 0, selected ? 2.5f : 1.5f);
                const char* label = KindLabel(entity);
                const ImVec2 size = ImGui::CalcTextSize(label);
                draw->AddText({at->x - size.x * 0.5f, at->y - size.y * 0.5f}, color, label);
            }
        } else if (selected) {
            const Bounds bounds = m_picker.WorldBounds(world, created);
            if (!bounds.Empty()) {
                box(glm::mat4(1.0f), bounds.min, bounds.max, color, 1.5f);
            }
        }
    }
    draw->PopClipRect();
}

void Editor::DrawGizmo() {
    scene::LevelEntity* selected = Selected();
    const ecs::Entity entity =
        selected != nullptr && m_instance != nullptr ? m_instance->Find(m_selected) : ecs::Entity{};
    if (entity.IsNull()) {
        m_gizmoUsing = false;
        return;
    }
    ecs::World& world = m_scene->World();
    ecs::PropagateTransforms(world);
    const glm::mat4 before = world.Get<ecs::WorldTransform>(entity).matrix;
    glm::mat4 matrix = before;
    const glm::vec2 extent = m_viewportMax - m_viewportMin;
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());
    ImGuizmo::SetRect(m_viewportMin.x, m_viewportMin.y, extent.x, extent.y);
    const glm::mat4 view = m_camera.View();
    const glm::mat4 projection = Projection();
    const auto operation = static_cast<ImGuizmo::OPERATION>(m_operation == 0 ? ImGuizmo::TRANSLATE : m_operation);
    const ImGuizmo::MODE mode = m_local || operation == ImGuizmo::SCALE ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
    // Snapping, or its opposite while Ctrl is held.
    const bool snap = m_snap != ImGui::GetIO().KeyCtrl;
    const float step = operation == ImGuizmo::ROTATE  ? m_snapSteps.y
                       : operation == ImGuizmo::SCALE ? m_snapSteps.z
                                                      : m_snapSteps.x;
    const float steps[3] = {step, step, step};
    if (!ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection), operation, mode, glm::value_ptr(matrix),
                              nullptr, snap ? steps : nullptr)) {
        m_gizmoUsing = ImGuizmo::IsUsing();
        return;
    }
    // The primary selection moved by delta: the others move with it, each where it is.
    const glm::mat4 delta = matrix * glm::inverse(before);
    std::vector<std::pair<std::uint64_t, math::Transform>> moved;
    for (const std::uint64_t id : SelectionRoots()) {
        const ecs::Entity created = m_instance->Find(id);
        if (created.IsNull()) {
            continue;
        }
        const ecs::Entity parent = world.Parent(created);
        const glm::mat4 parentWorld = parent.IsNull() ? glm::mat4(1.0f) : world.Get<ecs::WorldTransform>(parent).matrix;
        const glm::mat4 target = id == m_selected ? matrix : delta * world.Get<ecs::WorldTransform>(created).matrix;
        moved.emplace_back(id, Decompose(glm::inverse(parentWorld) * target));
    }
    const char* label = operation == ImGuizmo::ROTATE ? "Rotate" : operation == ImGuizmo::SCALE ? "Scale" : "Move";
    Change(label, [&moved](scene::Level& level) {
        for (const auto& [id, transform] : moved) {
            if (scene::LevelEntity* found = level.Find(id)) {
                found->transform = transform;
            }
        }
    });
    m_gizmoUsing = ImGuizmo::IsUsing();
}

void Editor::DrawGrid() {
    if (!m_grid) {
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
                    const auto alpha = static_cast<int>(fade * (major ? 110.0f : 45.0f));
                    draw->AddLine({a2->x, a2->y}, {b2->x, b2->y}, IM_COL32(200, 200, 210, alpha), major ? 1.5f : 1.0f);
                }
            }
        }
    }
    draw->PopClipRect();
}

void Editor::DrawHelp() {
    if (!m_showHelp) {
        return;
    }
    if (ImGui::Begin("Shortcuts", &m_showHelp, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize)) {
        constexpr std::array<std::array<const char*, 2>, 24> kKeys{{
            {"Right mouse + WASD, Q, E", "Fly; Shift goes faster, the wheel sets the speed"},
            {"Wheel, middle mouse", "Walk forward and back, pan"},
            {"Click, Ctrl+click", "Select, add to the selection"},
            {"W, E, R", "Move, rotate, scale"},
            {"X, or Ctrl while dragging", "Snapping on and off"},
            {"G", "The grid"},
            {"B", "Paint decals and wet patches (drag over surfaces)"},
            {"F, double-click in the hierarchy", "Frame the selection"},
            {"Esc, Ctrl+A", "Select nothing, everything"},
            {"Del", "Delete the selection"},
            {"Ctrl+D", "Duplicate"},
            {"Ctrl+C, Ctrl+X, Ctrl+V", "Copy, cut, paste (between levels too)"},
            {"Ctrl+Z, Ctrl+Y", "Undo, redo"},
            {"1 to 9", "Go to a camera bookmark"},
            {"Ctrl+1 to Ctrl+9", "Set a camera bookmark"},
            {"Ctrl+N, Ctrl+O", "New level, open a level"},
            {"Ctrl+S, Ctrl+Shift+S", "Save, save as"},
            {"F5, Ctrl+F5", "Play from the start, from the camera"},
            {"F1", "This window"},
            {"Drag in the hierarchy", "Move an entity under another, or to the root below"},
            {"Double-click an asset", "Place it in front of the camera"},
            {"Drag an asset into the view", "Place it where it is let go"},
            {"Edit > Make prefab", "Save the selection as a prefab, placed where it was"},
            {"Drop files on the window", "Import glTF models and skies"},
        }};
        if (ImGui::BeginTable("keys", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
            for (const auto& [key, action] : kKeys) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(key);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(action);
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
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
    float nearestDistance = kPickDistance;
    ecs::World& world = m_scene->World();
    for (const scene::LevelEntity& entity : m_level.entities) {
        if (!entity.model.empty()) {
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
        if (const std::optional<PickHit> hit = m_picker.Cast(world, origin, direction)) {
            nearest = m_instance->IdOf(hit->entity);
        }
    }
    if (io.KeyCtrl) {
        ToggleSelect(nearest);
    } else {
        Select(nearest);
    }
}

std::pair<glm::vec3, glm::vec3> Editor::RayAt(const glm::vec2& point) const {
    const glm::vec2 extent = glm::max(m_viewportMax - m_viewportMin, glm::vec2(1.0f));
    const glm::vec2 ndc((point.x - m_viewportMin.x) / extent.x * 2.0f - 1.0f,
                        1.0f - (point.y - m_viewportMin.y) / extent.y * 2.0f);
    const glm::mat4 inverse = glm::inverse(Projection() * m_camera.View());
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
