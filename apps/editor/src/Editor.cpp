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
#include <glm/geometric.hpp>
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
#include <limits>
#include <map>
#include <set>
#include <utility>
#include <vector>

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

/// The level files under the asset root, as paths under it, sorted: what File > Open offers.
std::vector<std::string> FindLevelFiles(const std::filesystem::path& root) {
    std::vector<std::string> files;
    std::error_code error;
    for (auto entry = std::filesystem::recursive_directory_iterator(root, error);
         !error && entry != std::filesystem::recursive_directory_iterator(); entry.increment(error)) {
        const std::string name = entry->path().filename().generic_string();
        if (entry->is_regular_file(error) && name.ends_with(kLevelExtension) && !name.ends_with(".play.level.json")) {
            files.push_back(entry->path().lexically_relative(root).generic_string());
        }
    }
    std::ranges::sort(files);
    return files;
}

/// A level path under the asset root, as typed, with the extension added when missing.
std::string LevelPathOf(std::string path) {
    std::ranges::replace(path, '\\', '/');
    while (!path.empty() && (path.back() == ' ' || path.back() == '/')) {
        path.pop_back();
    }
    if (!path.empty() && !path.ends_with(kLevelExtension)) {
        path += kLevelExtension;
    }
    return path;
}

/// The sandbox, which plays levels: beside the editor once installed, else where the build put it.
std::filesystem::path SandboxExecutable() {
#ifdef _WIN32
    const std::filesystem::path beside = platform::ExecutableDirectory() / "sandbox.exe";
#else
    const std::filesystem::path beside = platform::ExecutableDirectory() / "sandbox";
#endif
#ifdef DAI_SANDBOX_PATH
    std::error_code error;
    if (!std::filesystem::exists(beside, error)) {
        return DAI_SANDBOX_PATH;
    }
#endif
    return beside;
}

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
    m_selected = 0;
    m_eulerOf = 0;
    m_history.Clear();
    m_savedRevision = m_history.Current();
    m_autosavedRevision = m_history.Current();
    m_sinceAutosave = 0.0f;
    m_scene = &target;
    app.SetScene(std::move(scene));
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
    m_eulerOf = 0;
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
    if (m_selected != 0 && m_level.Find(m_selected) == nullptr) {
        Select(0);
    }
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

void Editor::Create(std::string_view label, scene::LevelEntity entity) {
    m_history.Commit(m_level);
    entity.id = m_level.NextId();
    entity.parent = 0;
    if (entity.name.empty()) {
        entity.name = std::string(label);
    }
    entity.transform.translation = m_camera.position + m_camera.Forward() * 4.0f;
    Change(std::format("Create {}", label), [&entity](scene::Level& level) { level.entities.push_back(entity); });
    m_history.Commit(m_level);
    Select(entity.id);
    m_status = std::format("Created {}", EntityLabel(entity));
}

void Editor::DeleteSelected() {
    const scene::LevelEntity* selected = Selected();
    if (selected == nullptr) {
        return;
    }
    // The entity and everything under it.
    std::set<std::uint64_t> doomed{selected->id};
    for (bool grew = true; grew;) {
        grew = false;
        for (const scene::LevelEntity& entity : m_level.entities) {
            if (doomed.contains(entity.parent) && doomed.insert(entity.id).second) {
                grew = true;
            }
        }
    }
    const std::string label = std::format("Delete {}", EntityLabel(*selected));
    m_history.Commit(m_level);
    Change(label, [&doomed](scene::Level& level) {
        std::erase_if(level.entities,
                      [&doomed](const scene::LevelEntity& entity) { return doomed.contains(entity.id); });
    });
    m_history.Commit(m_level);
    m_status = doomed.size() > 1 ? std::format("{}, and {} entities under it", label, doomed.size() - 1) : label;
    Select(0);
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
    DrawStatus(app, time);
    DrawDialogs(app);

    if (m_scene != nullptr) {
        UpdateCamera(app, static_cast<float>(time.unclampedDelta.count()));
        DrawMarkers();
        DrawGizmo();
        Pick(app);
    }
    HandleShortcuts(app);

    // An edit ends when the mouse lets go of the widget or the gizmo that made it.
    if (m_history.IsOpen() && !ImGui::IsAnyItemActive() && !ImGuizmo::IsUsing()) {
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
        }
        return;
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
    if (pressed(ImGuiKey_Escape)) {
        Select(0);
    }
    if (pressed(ImGuiKey_Delete)) {
        DeleteSelected();
    }
    if (pressed(ImGuiKey_F)) {
        FrameSelection();
    }
}

void Editor::FrameSelection() {
    if (Selected() == nullptr || m_instance == nullptr) {
        return;
    }
    const ecs::Entity entity = m_instance->Find(m_selected);
    if (entity.IsNull()) {
        return;
    }
    ecs::PropagateTransforms(m_scene->World());
    const Bounds bounds = m_picker.WorldBounds(m_scene->World(), entity);
    const glm::vec3 center = bounds.Empty() ? glm::vec3(m_scene->World().Get<ecs::WorldTransform>(entity).matrix[3])
                                            : (bounds.min + bounds.max) * 0.5f;
    const float radius = bounds.Empty() ? 1.0f : glm::length(bounds.max - bounds.min) * 0.5f;
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
        if (ImGui::MenuItem("Delete", "Del", false, Selected() != nullptr)) {
            DeleteSelected();
        }
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
        ImGui::Separator();
        if (ImGui::MenuItem("Fog volume")) {
            scene::LevelEntity entity;
            entity.fog = scene::FogVolume{};
            entity.transform.scale = glm::vec3(4.0f, 2.0f, 4.0f);
            Create("Fog volume", std::move(entity));
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
        if (ImGui::MenuItem("Reset the layout")) {
            m_layoutBuilt = false;
            ImGui::ClearIniSettings();
        }
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
    if (ImGui::MenuItem(m_local ? "Local" : "World")) {
        m_local = !m_local;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("The axes the gizmo moves along: the entity's own, or the world's");
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
                                const std::function<void(const std::string&)>& run) {
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
        const std::string path = LevelPathOf(m_pathField);
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
        const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.12f, nullptr, &center);
        ImGui::DockBuilderDockWindow("Hierarchy", left);
        ImGui::DockBuilderDockWindow("Inspector", right);
        ImGui::DockBuilderDockWindow("Environment", right);
        ImGui::DockBuilderDockWindow("Status", bottom);
        ImGui::DockBuilderFinish(dockspace);
    }
    m_layoutBuilt = true;
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
    if (id == m_selected) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    const std::string label = std::format("[{}] {}##{}", KindLabel(entity),
                                          entity.name.empty() ? std::format("Entity {}", id) : entity.name, id);
    const bool open = ImGui::TreeNodeEx(label.c_str(), flags);
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        Select(id);
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
        ImGui::TextDisabled("Right mouse: fly with WASD, Q and E, the wheel sets the speed. W E R: move, rotate, "
                            "scale. F: frame the selection. Ctrl+Z, Ctrl+Y: undo, redo. F5: play.");
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
        const bool selected = entity.id == m_selected;
        const float distance = glm::distance(glm::vec3(matrix[3]), m_camera.position);
        if (!selected && distance > kMarkerReach) {
            continue;
        }
        const float fade = selected ? 1.0f : std::clamp(1.5f - distance / kMarkerReach * 1.5f, 0.15f, 1.0f);
        const ImU32 alpha = static_cast<ImU32>(fade * 255.0f) << 24;
        const ImU32 color = selected ? IM_COL32(255, 220, 60, 255) : ((MarkerColor(entity) & 0x00FFFFFFU) | alpha);
        // Volumes show their box; a selected light shows where it points and how far it reaches.
        if (entity.fog || entity.wind || entity.weather || entity.lightning) {
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
    glm::mat4 matrix = world.Get<ecs::WorldTransform>(entity).matrix;
    const glm::vec2 extent = m_viewportMax - m_viewportMin;
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());
    ImGuizmo::SetRect(m_viewportMin.x, m_viewportMin.y, extent.x, extent.y);
    const glm::mat4 view = m_camera.View();
    const glm::mat4 projection = Projection();
    const auto operation = static_cast<ImGuizmo::OPERATION>(m_operation == 0 ? ImGuizmo::TRANSLATE : m_operation);
    const ImGuizmo::MODE mode = m_local || operation == ImGuizmo::SCALE ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
    if (ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection), operation, mode,
                             glm::value_ptr(matrix))) {
        const ecs::Entity parent = world.Parent(entity);
        const glm::mat4 parentWorld = parent.IsNull() ? glm::mat4(1.0f) : world.Get<ecs::WorldTransform>(parent).matrix;
        scene::LevelEntity edited = *selected;
        edited.transform = Decompose(glm::inverse(parentWorld) * matrix);
        Apply(edited, operation == ImGuizmo::ROTATE ? "Rotate" : operation == ImGuizmo::SCALE ? "Scale" : "Move");
    }
    m_gizmoUsing = ImGuizmo::IsUsing();
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
        const glm::vec2 extent = m_viewportMax - m_viewportMin;
        const glm::vec2 ndc((mouse.x - m_viewportMin.x) / extent.x * 2.0f - 1.0f,
                            1.0f - (mouse.y - m_viewportMin.y) / extent.y * 2.0f);
        const glm::mat4 inverse = glm::inverse(Projection() * m_camera.View());
        const glm::vec4 near = inverse * glm::vec4(ndc, -1.0f, 1.0f);
        const glm::vec4 far = inverse * glm::vec4(ndc, 1.0f, 1.0f);
        const glm::vec3 origin = glm::vec3(near) / near.w;
        const glm::vec3 direction = glm::normalize(glm::vec3(far) / far.w - origin);
        if (const std::optional<PickHit> hit = m_picker.Cast(world, origin, direction)) {
            nearest = m_instance->IdOf(hit->entity);
        }
    }
    Select(nearest);
}

} // namespace editor
