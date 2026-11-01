// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/app/Application.hpp>
#include <daidalos/ecs/Transform.hpp>
#include <daidalos/renderer/Primitives.hpp>

#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>

#include "Factory.hpp"

#if DAI_DEV_TOOLS
#include <imgui.h>
#endif

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace daidalos;

// Finds "assets" in the working directory, then beside the executable and in the directories above
// it, so the demo starts from a double-click in the build tree.
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
    return kName; // The load then reports what is missing.
}

// The demo: a dark factory, on the level of the original Daidalos demo (docs/DEMO.md). It loads in the
// background, and F5 starts it over in a new scene, which replaces the old one once it is ready.
class Sandbox final : public app::IApplicationHandler {
public:
    core::Result<void> OnStart(app::Application& app) override {
        DAI_LOG_INFO("sandbox", "Daidalos Engine Next {} started", DAI_VERSION);
        m_frameLimit =
            app.CVars().Register<std::int64_t>("sandbox.frames", 0, "Quit after this many frames; 0 runs until closed");
        m_levelPath = app.CVars()
                          .Register<std::string>("sandbox.scene", "Factory/Factory.gltf",
                                                 "Level to load from the assets directory")
                          .Get();
        m_settings.mouseSensitivity =
            static_cast<float>(app.CVars().Register<double>("sandbox.mouse", 0.0025, "Radians per mouse unit").Get());
        m_settings.darkness = static_cast<float>(
            app.CVars().Register<double>("sandbox.darkness", 1.0, "Multiplies the darkness; 0 shows the level").Get());
        m_start =
            app.CVars()
                .Register<std::string>("sandbox.start", "", "Where to start: 'x y z yaw pitch', in meters and degrees")
                .Get();
        m_actions = sandbox::MakeActions();
        m_assetRoot = FindAssetRoot();
        DAI_LOG_INFO("sandbox", "Assets in '{}'", m_assetRoot.string());

        // The first scene is current at once: the renderer's owner then releases it on any failure.
        app.SetScene(std::make_unique<scene::Scene>(app.Renderer(), m_assetRoot));
        LoadLevel(app, *app.CurrentScene());
        app.MainWindow().SetMouseCaptured(true);
        return {};
    }

    void OnUpdate(app::Application& app, const core::FrameTime& time) override {
        const input::InputState& input = app.Input();
        if (m_actions.WasPressed(input, sandbox::Quit)) {
            app.RequestQuit();
        }
        if (m_actions.WasPressed(input, sandbox::ToggleMouse)) {
            m_mouseCaptured = !m_mouseCaptured;
            app.MainWindow().SetMouseCaptured(m_mouseCaptured);
        }
        if (m_actions.WasPressed(input, sandbox::Restart) && !m_loading) {
            m_nextScene = std::make_unique<scene::Scene>(app.Renderer(), m_assetRoot);
            LoadLevel(app, *m_nextScene);
        }
        ++m_totalFrames;
        if (m_frameLimit->Get() > 0 && m_totalFrames >= m_frameLimit->Get()) {
            app.RequestQuit();
        }

        ++m_frames;
        m_elapsed += time.unclampedDelta;
        if (m_elapsed >= core::Seconds{1.0}) {
            m_fps = static_cast<double>(m_frames) / m_elapsed.count();
            app.MainWindow().SetTitle(std::format("Daidalos Sandbox - {:.0f} FPS", m_fps));
            m_frames = 0;
            m_elapsed = core::Seconds::zero();
        }
        DrawHud(app);
    }

    void OnStop(app::Application& app) override {
        m_nextScene.reset();
        DAI_LOG_INFO("sandbox", "Stopping after {} frames, {} presented", m_totalFrames,
                     app.Renderer().Device().PresentedFrameCount());
        if (scene::Scene* current = app.CurrentScene()) {
            current->World().Each<sandbox::Player>([](ecs::Entity /*entity*/, const sandbox::Player& player) {
                DAI_LOG_INFO("sandbox", "Player's feet at {:.2f} {:.2f} {:.2f}", player.feet.x, player.feet.y,
                             player.feet.z);
            });
        }
    }

    [[nodiscard]] const std::optional<core::Error>& Failure() const { return m_failure; }

private:
    void LoadLevel(app::Application& app, scene::Scene& target) {
        m_loading = true;
        target.Assets().LoadModelAsync(m_levelPath, app.Workers(),
                                       [this, &app, &target](core::Result<const renderer::Model*> model) {
                                           m_loading = false;
                                           if (core::Result<void> built = Build(app, target, model); !built) {
                                               DAI_LOG_ERROR("sandbox", "{}", core::Format(built.error()));
                                               m_failure = std::move(built.error());
                                               app.RequestQuit();
                                               return;
                                           }
                                           if (m_nextScene) {
                                               app.SetScene(std::move(m_nextScene));
                                           }
                                       });
    }

    core::Result<void> Build(app::Application& app, scene::Scene& target,
                             const core::Result<const renderer::Model*>& model) {
        if (model) {
            core::Result<ecs::Entity> player = sandbox::BuildLevel(target, **model);
            if (!player) {
                return tl::unexpected(player.error());
            }
        } else if (model.error().code == core::ErrorCode::NotFound) {
            // Other levels can be tried with sandbox.scene, so a missing one is not fatal.
            DAI_LOG_INFO("sandbox", "No level, showing a test floor: {}", core::Format(model.error()));
            if (core::Result<void> built = BuildTestFloor(app, target); !built) {
                return built;
            }
        } else {
            return tl::unexpected(model.error());
        }
        sandbox::AddSystems(target, app.Input(), m_actions, m_settings);
        ApplyStart(target);
        return {};
    }

    void ApplyStart(scene::Scene& target) const {
        glm::vec3 position{0.0f};
        float yaw = 0.0f;
        float pitch = 0.0f;
        std::istringstream text(m_start);
        if (!(text >> position.x >> position.y >> position.z >> yaw >> pitch)) {
            return;
        }
        ecs::World& world = target.World();
        std::vector<ecs::Entity> players;
        world.Each<sandbox::Player>([&](ecs::Entity entity, sandbox::Player& player) {
            player.yaw = glm::radians(yaw);
            player.pitch = glm::radians(pitch);
            players.push_back(entity);
        });
        for (const ecs::Entity entity : players) {
            sandbox::Teleport(target, entity, position);
        }
    }

    static core::Result<void> BuildTestFloor(app::Application& app, scene::Scene& target) {
        core::Result<renderer::MeshHandle> cube = app.Renderer().CreateMesh(renderer::MakeCube(), "Test floor");
        if (!cube) {
            return tl::unexpected(std::move(cube.error()));
        }
        ecs::World& world = target.World();
        const ecs::Entity floor = world.Create();
        ecs::LocalTransform transform;
        transform.translation = {0.0f, -0.05f, 0.0f};
        transform.scale = {12.0f, 0.1f, 12.0f};
        world.Add<ecs::LocalTransform>(floor, transform);
        world.Add<renderer::MeshRenderer>(floor, *cube);
        const std::array<glm::vec3, 4> corners{glm::vec3(-6.0f, 0.0f, -6.0f), glm::vec3(-6.0f, 0.0f, 6.0f),
                                               glm::vec3(6.0f, 0.0f, 6.0f), glm::vec3(6.0f, 0.0f, -6.0f)};
        const std::array<std::uint32_t, 6> triangles{0, 1, 2, 0, 2, 3};
        if (core::Result<physics::BodyId> body = target.Physics().AddStaticMesh(corners, triangles); !body) {
            return tl::unexpected(std::move(body.error()));
        }
        core::Result<ecs::Entity> player = sandbox::BuildLevel(target, renderer::Model{});
        if (!player) {
            return tl::unexpected(std::move(player.error()));
        }
        return {};
    }

    void DrawHud(app::Application& app) {
#if DAI_DEV_TOOLS
        // A stand-in until the game has its own HUD (docs/DEMO.md).
        if (app.DebugOverlay() == nullptr) {
            return;
        }
        ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
        ImGui::Begin("Daidalos", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("%.0f FPS", m_fps);
        if (m_loading) {
            ImGui::TextUnformatted("Loading...");
        }
        if (scene::Scene* current = app.CurrentScene()) {
            current->World().Each<sandbox::Player>([](ecs::Entity /*entity*/, const sandbox::Player& player) {
                ImGui::Text("Health %d   Armor %d", player.health, player.armor);
                ImGui::Text("Ammo: handgun %d, shotgun %d", player.handgunAmmo, player.shotgunAmmo);
                ImGui::Text("%s%s", player.hasFlashlight ? "[Flashlight] " : "", player.hasRedCard ? "[Red card]" : "");
                if (player.messageTime > 0.0f) {
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "%s", player.message.c_str());
                }
            });
        }
        ImGui::TextDisabled(
            "WASD: move. Shift: run. Space: jump. C: crouch. E: use. F: flashlight. F5: restart. Tab: free the mouse.");
        if (const std::string& error = app.Renderer().ShaderError(); !error.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "Shader reload failed:");
            ImGui::TextUnformatted(error.c_str());
        }
        ImGui::End();
#else
        static_cast<void>(app);
#endif
    }

    input::ActionMap m_actions;
    std::filesystem::path m_assetRoot;
    std::string m_levelPath;
    sandbox::Settings m_settings;
    std::string m_start;
    std::unique_ptr<scene::Scene> m_nextScene;
    bool m_loading = false;
    bool m_mouseCaptured = true;
    std::optional<core::Error> m_failure;
    std::optional<core::CVar<std::int64_t>> m_frameLimit;
    std::int64_t m_totalFrames = 0;
    int m_frames = 0;
    double m_fps = 0.0;
    core::Seconds m_elapsed{};
};

} // namespace

int main(int argc, char** argv) {
    app::ApplicationConfig config;
    config.log.minimumLevel = logging::Level::Debug;
    config.log.filePath = "sandbox.log";
    config.crashDumpDirectory = "crashdumps";
    config.configFile = "sandbox.cfg";
    config.arguments.assign(argv + 1, argv + argc);
    config.window.title = "Daidalos Sandbox";
    config.rendering = true;

    app::Application application(std::move(config));
    Sandbox sandbox;
    core::Result<void> result = application.Run(sandbox);
    if (result && sandbox.Failure().has_value()) {
        result = tl::unexpected(*sandbox.Failure());
    }
    if (!result) {
        std::fprintf(stderr, "%s\n", core::Format(result.error()).c_str());
        return 1;
    }
    return 0;
}
