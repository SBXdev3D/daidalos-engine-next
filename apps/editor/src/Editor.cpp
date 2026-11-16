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

#include <ImGuizmo.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <map>
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
    m_dirty = false;
    m_scene = &target;
    app.SetScene(std::move(scene));
    m_status = std::format("Opened {}: {} entities", levelPath, m_level.entities.size());
    DAI_LOG_INFO(kModule, "{}", m_status);
    UpdateTitle(app);
    return {};
}

void Editor::Save() {
    const std::filesystem::path file = m_assetRoot / std::filesystem::path(m_levelPath);
    if (core::Result<void> saved = scene::SaveLevelFile(file, m_level); !saved) {
        m_status = core::Format(saved.error());
        DAI_LOG_ERROR(kModule, "{}", m_status);
        return;
    }
    m_dirty = false;
    m_status = std::format("Saved {}", m_levelPath);
    UpdateTitle(*m_app);
}

void Editor::UpdateTitle(app::Application& app) {
    app.MainWindow().SetTitle(std::format("Daidalos Editor - {}{}", m_levelPath, m_dirty ? " *" : ""));
}

scene::LevelEntity* Editor::Selected() {
    return m_selected == 0 ? nullptr : m_level.Find(m_selected);
}

void Editor::Select(std::uint64_t id) {
    m_selected = id;
    m_eulerOf = 0;
}

void Editor::Apply(const scene::LevelEntity& after) {
    scene::LevelEntity* before = m_level.Find(after.id);
    if (before == nullptr || *before == after) {
        return;
    }
    scene::LevelEntity changed = *before;
    changed.transform = after.transform;
    if (changed == after && m_instance != nullptr) {
        // Only the transform: move what is there.
        const ecs::Entity entity = m_instance->Find(after.id);
        if (!entity.IsNull()) {
            static_cast<math::Transform&>(m_scene->World().Get<ecs::LocalTransform>(entity)) = after.transform;
        }
        *before = after;
    } else {
        const bool newModel = before->model != after.model;
        *before = after;
        if (m_instance != nullptr) {
            m_instance->Create(m_level, *before);
            if (newModel) {
                m_picker.Rebuild(m_scene->World());
            }
        }
    }
    if (!m_dirty) {
        m_dirty = true;
        UpdateTitle(*m_app);
    }
}

void Editor::OnUpdate(app::Application& app, const core::FrameTime& time) {
    ImGuizmo::BeginFrame();
    DrawMenu(app);
    DrawDockspace();
    DrawHierarchy();
    DrawInspector();
    DrawEnvironment();
    DrawStatus(app, time);

    if (m_scene != nullptr) {
        UpdateCamera(app, static_cast<float>(time.unclampedDelta.count()));
        DrawMarkers();
        DrawGizmo();
        Pick(app);
    }

    const ImGuiIO& io = ImGui::GetIO();
    if (!io.WantTextInput && !m_flying) {
        const bool control = io.KeyCtrl;
        if (control && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
            Save();
        }
        if (!control && ImGui::IsKeyPressed(ImGuiKey_W, false)) {
            m_operation = ImGuizmo::TRANSLATE;
        }
        if (!control && ImGui::IsKeyPressed(ImGuiKey_E, false)) {
            m_operation = ImGuizmo::ROTATE;
        }
        if (!control && ImGui::IsKeyPressed(ImGuiKey_R, false)) {
            m_operation = ImGuizmo::SCALE;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            Select(0);
        }
        if (!control && ImGui::IsKeyPressed(ImGuiKey_F, false) && Selected() != nullptr && m_instance != nullptr) {
            // Frames the selection.
            const ecs::Entity entity = m_instance->Find(m_selected);
            if (!entity.IsNull()) {
                ecs::PropagateTransforms(m_scene->World());
                Bounds bounds = m_picker.WorldBounds(m_scene->World(), entity);
                const glm::vec3 center = bounds.Empty()
                                             ? glm::vec3(m_scene->World().Get<ecs::WorldTransform>(entity).matrix[3])
                                             : (bounds.min + bounds.max) * 0.5f;
                const float radius = bounds.Empty() ? 1.0f : glm::length(bounds.max - bounds.min) * 0.5f;
                const float distance = std::max(radius / std::tan(m_camera.verticalFov * 0.5f), 1.0f);
                m_camera.position = center - m_camera.Forward() * distance;
            }
        }
    }
    if (m_scene != nullptr && !m_cameraEntity.IsNull()) {
        static_cast<math::Transform&>(m_scene->World().Get<ecs::LocalTransform>(m_cameraEntity)) =
            Decompose(m_camera.World());
    }
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
        if (ImGui::MenuItem("Save", "Ctrl+S")) {
            Save();
        }
        if (ImGui::MenuItem("Reload from disk")) {
            if (core::Result<void> opened = Open(app, m_levelPath); !opened) {
                m_status = core::Format(opened.error());
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Quit")) {
            app.RequestQuit();
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
        Apply(edited);
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
        m_level.environment = environment;
        if (m_instance != nullptr) {
            m_instance->ApplyEnvironment(environment);
        }
        if (!m_dirty) {
            m_dirty = true;
            UpdateTitle(*m_app);
        }
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
                            "scale. F: frame the selection.");
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
        Apply(edited);
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
