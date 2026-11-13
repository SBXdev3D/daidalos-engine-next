// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/app/Application.hpp>
#include <daidalos/app/DevTools.hpp>
#include <daidalos/ecs/Transform.hpp>
#include <daidalos/scene/Components.hpp>

#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

#include <imgui.h>

#include <algorithm>
#include <cstring>
#include <format>
#include <numeric>
#include <string_view>
#include <utility>

namespace daidalos::app {

namespace {

constexpr ImVec4 kWarningColor{1.0f, 0.8f, 0.3f, 1.0f};
constexpr ImVec4 kErrorColor{1.0f, 0.4f, 0.3f, 1.0f};
constexpr ImVec4 kQuietColor{0.6f, 0.6f, 0.6f, 1.0f};

ImVec4 LevelColor(logging::Level level) {
    switch (level) {
    case logging::Level::Trace:
    case logging::Level::Debug:
        return kQuietColor;
    case logging::Level::Warning:
        return kWarningColor;
    case logging::Level::Error:
    case logging::Level::Critical:
        return kErrorColor;
    default:
        return ImVec4(0.9f, 0.9f, 0.9f, 1.0f);
    }
}

std::string EntityName(const ecs::World& world, ecs::Entity entity) {
    if (const scene::NodeName* name = world.TryGet<scene::NodeName>(entity); name != nullptr && !name->value.empty()) {
        return std::format("{} #{}", name->value, entity.Index());
    }
    return std::format("Entity #{}", entity.Index());
}

/// "1.2 M" and the like, for large counts.
std::string Count(std::uint64_t value) {
    if (value >= 10'000'000) {
        return std::format("{:.1f} M", static_cast<double>(value) / 1.0e6);
    }
    if (value >= 10'000) {
        return std::format("{:.1f} k", static_cast<double>(value) / 1.0e3);
    }
    return std::format("{}", value);
}

std::string Megabytes(std::uint64_t bytes) {
    return std::format("{:.0f} MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
}

void TextLine(std::string_view text) {
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
}

} // namespace

DevTools::DevTools(Application& app) : m_app(&app), m_console(app.CVars(), app.ConsoleOutput()) {
    m_console.AddCommand(
        "quit", "Quits the game",
        [this](Console& /*console*/, std::span<const std::string> /*arguments*/) { m_app->RequestQuit(); });
    m_console.AddCommand(
        "windows", "Shows every window of the tools",
        [this](Console& /*console*/, std::span<const std::string> /*arguments*/) { ShowAllWindows(); });
    AddEngineInspectors();
    // The overlay blends in linear light, where ImGui's 94% opaque windows let through what looks like a
    // quarter of the scene's brightness: bright pixels behind them read as noise.
    ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w = 1.0f;
}

void DevTools::ShowAllWindows() {
    m_showConsole = true;
    m_showStats = true;
    m_showEntities = true;
    m_showResources = true;
    for (Window& window : m_windows) {
        window.visible = true;
    }
}

void DevTools::SetOpen(bool open) {
    if (open && !m_open) {
        m_focusInput = true;
        m_scrollToBottom = true;
        m_input.fill(0);
    }
    m_open = open;
}

void DevTools::AddWindow(std::string name, std::function<void()> draw) {
    m_windows.push_back({std::move(name), std::move(draw)});
}

void DevTools::AddEngineInspectors() {
    AddInspector<scene::NodeName>("Name", [](scene::NodeName& name) { TextLine(name.value); });
    AddInspector<ecs::LocalTransform>("Local transform", [](ecs::LocalTransform& transform) {
        ImGui::DragFloat3("Translation", &transform.translation.x, 0.01f);
        glm::vec3 degrees = glm::degrees(glm::eulerAngles(transform.rotation));
        if (ImGui::DragFloat3("Rotation", &degrees.x, 0.5f)) {
            transform.rotation = glm::normalize(glm::quat(glm::radians(degrees)));
        }
        ImGui::DragFloat3("Scale", &transform.scale.x, 0.01f);
    });
    AddInspector<ecs::WorldTransform>("World transform", [](ecs::WorldTransform& transform) {
        const glm::vec4& position = transform.matrix[3];
        ImGui::Text("Position %.2f %.2f %.2f", position.x, position.y, position.z);
    });
    AddInspector<scene::Camera>("Camera", [](scene::Camera& camera) {
        bool ownFov = camera.verticalFov.has_value();
        if (ImGui::Checkbox("Own field of view", &ownFov)) {
            camera.verticalFov = ownFov ? std::optional<float>(glm::radians(60.0f)) : std::nullopt;
        }
        if (camera.verticalFov.has_value()) {
            float degrees = glm::degrees(*camera.verticalFov);
            if (ImGui::SliderFloat("Vertical degrees", &degrees, 10.0f, 150.0f)) {
                camera.verticalFov = glm::radians(degrees);
            }
        }
        ImGui::DragFloat("Near plane", &camera.nearPlane, 0.001f, 0.001f, 10.0f);
    });
    AddInspector<scene::MeshRenderer>("Mesh renderer", [](scene::MeshRenderer& mesh) {
        ImGui::Text("Mesh %u, material %u", mesh.mesh.Index(), mesh.material.Index());
        ImGui::Checkbox("Casts shadows", &mesh.castsShadows);
    });
    AddInspector<scene::SkinnedMeshRenderer>("Skinned mesh renderer", [](scene::SkinnedMeshRenderer& mesh) {
        ImGui::Text("Mesh %u, material %u, %zu joints", mesh.mesh.Index(), mesh.material.Index(), mesh.joints.size());
        ImGui::Checkbox("Casts shadows", &mesh.castsShadows);
    });
    AddInspector<scene::Light>("Light", [](scene::Light& light) {
        const bool directional = light.type == renderer::LightType::Directional;
        TextLine(directional                               ? "Directional light"
                 : light.type == renderer::LightType::Spot ? "Spot light"
                                                           : "Point light");
        ImGui::Checkbox("Enabled", &light.enabled);
        ImGui::ColorEdit3("Color", &light.color.x, ImGuiColorEditFlags_Float);
        ImGui::DragFloat(directional ? "Illuminance (lux)" : "Intensity (cd)", &light.intensity, 0.5f, 0.0f, 100000.0f);
        ImGui::DragFloat("Temperature (K)", &light.temperature, 10.0f, 0.0f, 40000.0f);
        if (directional) {
            float angle = glm::degrees(light.angularRadius);
            if (ImGui::SliderFloat("Angular radius", &angle, 0.0f, 10.0f)) {
                light.angularRadius = glm::radians(angle);
            }
            return;
        }
        ImGui::DragFloat("Range", &light.range, 0.05f, 0.0f, 500.0f);
        ImGui::DragFloat("Radius", &light.radius, 0.005f, 0.0f, 2.0f);
        if (light.type == renderer::LightType::Spot) {
            float inner = glm::degrees(light.innerAngle);
            float outer = glm::degrees(light.outerAngle);
            if (ImGui::SliderFloat("Inner angle", &inner, 0.0f, 89.0f)) {
                light.innerAngle = glm::radians(inner);
            }
            if (ImGui::SliderFloat("Outer angle", &outer, 0.0f, 89.0f)) {
                light.outerAngle = glm::radians(outer);
            }
        }
    });
    AddInspector<scene::ParticleEmitter>("Particle emitter", [](scene::ParticleEmitter& emitter) {
        ImGui::Text("Effect %u", emitter.effect.Index());
        ImGui::Checkbox("Enabled", &emitter.enabled);
        ImGui::DragFloat("Per second", &emitter.rate, 0.1f, 0.0f, 10000.0f);
    });
    AddInspector<scene::KinematicBody>("Kinematic body",
                                       [](scene::KinematicBody& body) { ImGui::Text("Body %u", body.id.value); });
    AddInspector<scene::SoundEmitter>("Sound emitter",
                                      [](scene::SoundEmitter& sound) { ImGui::Text("Voice %u", sound.voice.Index()); });
}

void DevTools::Draw(const core::FrameTime& time) {
    m_frameTimes[m_frameCursor] = static_cast<float>(time.unclampedDelta.count() * 1000.0);
    m_frameCursor = (m_frameCursor + 1) % kFrameHistory;
    m_frameCount = std::min(m_frameCount + 1, kFrameHistory);

    if (!m_open) {
        DrawSummary();
        return;
    }
    DrawMenu();
    if (m_showConsole) {
        DrawConsole();
    }
    if (m_showStats) {
        DrawStats();
    }
    if (m_showEntities) {
        DrawEntities();
    }
    if (m_showResources) {
        DrawResources();
    }
    // The game's windows go down the right side, under the statistics, and leave the focus to the console.
    float y = 560.0f;
    for (Window& window : m_windows) {
        if (window.visible) {
            ImGui::SetNextWindowPos(ImVec2(780.0f, y), ImGuiCond_FirstUseEver);
            y += 40.0f;
            if (ImGui::Begin(window.name.c_str(), &window.visible,
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing)) {
                window.draw();
            }
            ImGui::End();
        }
    }
}

void DevTools::DrawMenu() {
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }
    if (ImGui::BeginMenu("Windows")) {
        ImGui::MenuItem("Console", nullptr, &m_showConsole);
        ImGui::MenuItem("Statistics", nullptr, &m_showStats);
        ImGui::MenuItem("Entities", nullptr, &m_showEntities);
        ImGui::MenuItem("Resources", nullptr, &m_showResources);
        if (!m_windows.empty()) {
            ImGui::Separator();
            for (Window& window : m_windows) {
                ImGui::MenuItem(window.name.c_str(), nullptr, &window.visible);
            }
        }
        ImGui::EndMenu();
    }
    ImGui::TextDisabled("The key under Escape closes the tools");
    ImGui::EndMainMenuBar();
}

void DevTools::DrawSummary() {
    // The average over the last second or so.
    float milliseconds = 0.0f;
    std::size_t frames = 0;
    for (std::size_t back = 1; back <= m_frameCount && milliseconds < 1000.0f; ++back) {
        milliseconds += m_frameTimes[(m_frameCursor + kFrameHistory - back) % kFrameHistory];
        ++frames;
    }
    const float average = frames > 0 ? milliseconds / static_cast<float>(frames) : 0.0f;
    double gpu = 0.0;
    for (const renderer::PassTime& pass : m_app->Renderer().GpuTimes()) {
        gpu += pass.milliseconds;
    }
    const renderer::FrameStats stats = m_app->Renderer().Stats();

    ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f));
    ImGui::SetNextWindowBgAlpha(0.45f);
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                        ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
                                        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    if (ImGui::Begin("##summary", nullptr, kFlags)) {
        TextLine(std::format("{:.0f} FPS   {:.2f} ms   GPU {:.2f} ms   {} draws   {} triangles",
                             average > 0.0f ? 1000.0f / average : 0.0f, average, gpu, stats.drawnDraws,
                             Count(stats.triangles)));
        if (const std::string& error = m_app->Renderer().ShaderError(); !error.empty()) {
            ImGui::TextColored(kErrorColor, "Shader reload failed: open the tools to read why");
        }
    }
    ImGui::End();
}

void DevTools::DrawConsole() {
    ImGui::SetNextWindowPos(ImVec2(10.0f, 30.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(760.0f, 360.0f), ImGuiCond_FirstUseEver);
    // The other windows leave the focus to the console when they appear, so typing goes to it.
    if (m_focusInput) {
        ImGui::SetNextWindowFocus();
    }
    if (!ImGui::Begin("Console", &m_showConsole)) {
        ImGui::End();
        return;
    }
    if (const std::uint64_t revision = m_console.Log().Revision(); revision != m_logRevision) {
        m_logRevision = revision;
        m_lines = m_console.Log().Lines();
        m_scrollToBottom = true;
    }

    const float inputHeight = ImGui::GetFrameHeightWithSpacing();
    if (ImGui::BeginChild("##lines", ImVec2(0.0f, -inputHeight), ImGuiChildFlags_None,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(m_lines.size()));
        while (clipper.Step()) {
            for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
                const ConsoleLog::Line& line = m_lines[static_cast<std::size_t>(index)];
                ImGui::PushStyleColor(ImGuiCol_Text, LevelColor(line.level));
                TextLine(line.text);
                ImGui::PopStyleColor();
            }
        }
        // Follows new lines, unless the reader scrolled up.
        if (m_scrollToBottom && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 40.0f) {
            ImGui::SetScrollHereY(1.0f);
        }
        m_scrollToBottom = false;
    }
    ImGui::EndChild();

    const auto callback = [](ImGuiInputTextCallbackData* data) -> int {
        auto* tools = static_cast<DevTools*>(data->UserData);
        const auto replace = [data](const std::string& text) {
            data->DeleteChars(0, data->BufTextLen);
            data->InsertChars(0, text.c_str());
        };
        if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
            replace(tools->m_console.Complete(std::string_view(data->Buf, static_cast<std::size_t>(data->BufTextLen))));
        } else if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
            const std::span<const std::string> history = tools->m_console.History();
            std::size_t& place = tools->m_historyPlace;
            place = std::min(place, history.size());
            if (data->EventKey == ImGuiKey_UpArrow && place > 0) {
                --place;
            } else if (data->EventKey == ImGuiKey_DownArrow && place < history.size()) {
                ++place;
            }
            replace(place < history.size() ? history[place] : std::string());
        }
        return 0;
    };
    ImGui::SetNextItemWidth(-1.0f);
    constexpr ImGuiInputTextFlags kFlags = ImGuiInputTextFlags_EnterReturnsTrue |
                                           ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackHistory;
    if (ImGui::InputText("##input", m_input.data(), m_input.size(), kFlags, callback, this)) {
        m_console.Execute(m_input.data());
        m_input.fill(0);
        m_historyPlace = m_console.History().size();
        m_focusInput = true;
    }
    ImGui::SetItemDefaultFocus();
    if (std::exchange(m_focusInput, false)) {
        ImGui::SetKeyboardFocusHere(-1);
    }
    ImGui::End();
}

void DevTools::DrawStats() {
    ImGui::SetNextWindowPos(ImVec2(780.0f, 30.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Statistics", &m_showStats,
                      ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::End();
        return;
    }
    // Oldest first, for the graph.
    std::array<float, kFrameHistory> ordered{};
    for (std::size_t index = 0; index < m_frameCount; ++index) {
        ordered[index] = m_frameTimes[(m_frameCursor + kFrameHistory - m_frameCount + index) % kFrameHistory];
    }
    const std::span<const float> frames(ordered.data(), m_frameCount);
    const float average =
        frames.empty() ? 0.0f : std::accumulate(frames.begin(), frames.end(), 0.0f) / static_cast<float>(frames.size());
    const float worst = frames.empty() ? 0.0f : *std::ranges::max_element(frames);
    TextLine(std::format("{:.0f} FPS, {:.2f} ms on average, {:.2f} ms at worst, over {} frames",
                         average > 0.0f ? 1000.0f / average : 0.0f, average, worst, frames.size()));
    ImGui::PlotLines("##frames", ordered.data(), static_cast<int>(m_frameCount), 0, nullptr, 0.0f,
                     std::max(worst, 1.0f), ImVec2(420.0f, 60.0f));

    renderer::Renderer& renderer = m_app->Renderer();
    if (ImGui::CollapsingHeader("GPU", ImGuiTreeNodeFlags_DefaultOpen)) {
        const std::span<const renderer::PassTime> passes = renderer.GpuTimes();
        double total = 0.0;
        if (ImGui::BeginTable("##passes", 2, ImGuiTableFlags_SizingFixedFit)) {
            for (const renderer::PassTime& pass : passes) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                TextLine(pass.name);
                ImGui::TableNextColumn();
                TextLine(std::format("{:.2f} ms", pass.milliseconds));
                total += pass.milliseconds;
            }
            ImGui::EndTable();
        }
        TextLine(passes.empty() ? std::string("No timestamps on this device") : std::format("Total {:.2f} ms", total));
    }
    if (ImGui::CollapsingHeader("Frame", ImGuiTreeNodeFlags_DefaultOpen)) {
        const renderer::FrameStats stats = renderer.Stats();
        TextLine(
            std::format("Draws: {} drawn of {}, {} triangles", stats.drawnDraws, stats.draws, Count(stats.triangles)));
        TextLine(std::format("Lights: {}, emissive triangles included; one shadow ray per pixel", stats.lights));
        TextLine(std::format("Particle spawns: {}", stats.particleSpawns));
        if (scene::Scene* scene = m_app->CurrentScene()) {
            TextLine(std::format("Entities: {}{}", scene->World().AliveCount(), scene->IsPaused() ? ", paused" : ""));
        } else {
            TextLine("No scene");
        }
    }
    if (ImGui::CollapsingHeader("Device", ImGuiTreeNodeFlags_DefaultOpen)) {
        const rhi::MemoryStats memory = renderer.Device().Memory();
        TextLine(renderer.Device().Info().name);
        TextLine(std::format("Video memory: {} of {}", Megabytes(memory.usedBytes), Megabytes(memory.budgetBytes)));
        TextLine(std::format("{} buffers, {} textures, {} pipelines; {} waiting to be destroyed", memory.buffers,
                             memory.textures, memory.pipelines, renderer.Device().PendingDestructionCount()));
        if (const std::uint64_t errors = renderer.Device().ValidationErrorCount(); errors > 0) {
            ImGui::TextColored(kErrorColor, "%llu validation errors", static_cast<unsigned long long>(errors));
        }
    }
    if (const std::string& error = renderer.ShaderError(); !error.empty()) {
        ImGui::TextColored(kErrorColor, "Shader reload failed:");
        TextLine(error);
    }
    ImGui::End();
}

void DevTools::DrawEntities() {
    ImGui::SetNextWindowPos(ImVec2(10.0f, 400.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(760.0f, 420.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Entities", &m_showEntities, ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::End();
        return;
    }
    scene::Scene* scene = m_app->CurrentScene();
    if (scene == nullptr) {
        TextLine("No scene");
        ImGui::End();
        return;
    }
    ecs::World& world = scene->World();

    if (ImGui::BeginTable("##split", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##filter", "Filter by name", m_filter.data(), m_filter.size());
        if (ImGui::BeginChild("##tree")) {
            const std::string_view filter(m_filter.data());
            if (filter.empty()) {
                world.EachEntity([&](ecs::Entity entity) {
                    if (world.Parent(entity).IsNull()) {
                        DrawEntityTree(world, entity);
                    }
                });
            } else {
                world.EachEntity([&](ecs::Entity entity) {
                    const std::string name = EntityName(world, entity);
                    if (name.find(filter) != std::string::npos &&
                        ImGui::Selectable(name.c_str(), entity == m_selected)) {
                        m_selected = entity;
                    }
                });
            }
        }
        ImGui::EndChild();
        ImGui::TableNextColumn();
        if (ImGui::BeginChild("##inspector")) {
            DrawInspector(world);
        }
        ImGui::EndChild();
        ImGui::EndTable();
    }
    ImGui::End();
}

void DevTools::DrawEntityTree(ecs::World& world, ecs::Entity entity) {
    const ecs::Entity firstChild = world.FirstChild(entity);
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (firstChild.IsNull()) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (entity == m_selected) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    ImGui::PushID(static_cast<int>(entity.Index()));
    const bool open = ImGui::TreeNodeEx("##node", flags, "%s", EntityName(world, entity).c_str());
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        m_selected = entity;
    }
    if (open && !firstChild.IsNull()) {
        for (ecs::Entity child = firstChild; !child.IsNull(); child = world.NextSibling(child)) {
            DrawEntityTree(world, child);
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void DevTools::DrawInspector(ecs::World& world) {
    if (!world.IsAlive(m_selected)) {
        TextLine("Pick an entity on the left.");
        return;
    }
    TextLine(std::format("{}, generation {}", EntityName(world, m_selected), m_selected.Generation()));
    if (const ecs::Entity parent = world.Parent(m_selected); !parent.IsNull()) {
        if (ImGui::SmallButton(std::format("Parent: {}", EntityName(world, parent)).c_str())) {
            m_selected = parent;
            return;
        }
    }
    ImGui::Separator();
    for (Inspector& inspector : m_inspectors) {
        if (!inspector.draw(world, m_selected, false)) {
            continue;
        }
        ImGui::PushID(inspector.name.c_str());
        if (ImGui::CollapsingHeader(inspector.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            inspector.draw(world, m_selected, true);
        }
        ImGui::PopID();
    }
}

void DevTools::DrawResources() {
    ImGui::SetNextWindowPos(ImVec2(780.0f, 400.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(480.0f, 420.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Resources", &m_showResources, ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::End();
        return;
    }
    const renderer::FrameStats stats = m_app->Renderer().Stats();
    const rhi::MemoryStats memory = m_app->Renderer().Device().Memory();
    TextLine(std::format("Renderer: {} meshes, {} materials, {} particle effects", stats.meshes, stats.materials,
                         stats.particleEffects));
    TextLine(std::format("Device: {} buffers, {} textures, {} of video memory", memory.buffers, memory.textures,
                         Megabytes(memory.usedBytes)));
    ImGui::Separator();
    scene::Scene* scene = m_app->CurrentScene();
    if (scene == nullptr) {
        TextLine("No scene");
        ImGui::End();
        return;
    }
    const scene::AssetLibrary& assets = scene->Assets();
    TextLine(std::format("The scene's assets: {} models, {} textures, {} fonts{}", assets.ModelCount(),
                         assets.TextureCount(), assets.FontCount(),
                         assets.PendingLoadCount() > 0 ? std::format(", {} loading", assets.PendingLoadCount())
                                                       : std::string()));
    if (ImGui::BeginTable("##assets", 2,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Path");
        ImGui::TableHeadersRow();
        for (const scene::AssetLibrary::AssetInfo& asset : assets.Describe()) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            switch (asset.kind) {
            case scene::AssetLibrary::AssetInfo::Kind::Model:
                TextLine("Model");
                break;
            case scene::AssetLibrary::AssetInfo::Kind::Texture:
                TextLine("Texture");
                break;
            case scene::AssetLibrary::AssetInfo::Kind::Font:
                TextLine("Font");
                break;
            }
            ImGui::TableNextColumn();
            TextLine(asset.path);
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

} // namespace daidalos::app
