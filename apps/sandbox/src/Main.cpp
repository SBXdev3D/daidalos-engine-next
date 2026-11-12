// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/app/Application.hpp>
#include <daidalos/ecs/Transform.hpp>
#include <daidalos/platform/Platform.hpp>
#include <daidalos/renderer/CornellBox.hpp>
#include <daidalos/renderer/MirrorRoom.hpp>
#include <daidalos/renderer/Primitives.hpp>

#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>

#include "Factory.hpp"
#include "Hud.hpp"
#include "Menus.hpp"

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
#include <span>
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
        m_levelPath =
            app.CVars()
                .Register<std::string>("sandbox.scene", "Factory/Factory.gltf",
                                       "Level to load from the assets directory, or cornell for the Cornell box")
                .Get();
        m_settings.mouseSensitivity = app.MouseSensitivity();
        m_settings.darkness =
            static_cast<float>(app.CVars()
                                   .Register<double>("sandbox.darkness", 1.0,
                                                     "1 is the factory's darkness; 0 shows it brighter, without fog")
                                   .Get());
        m_settings.startWithFlashlight =
            app.CVars().Register<bool>("sandbox.flashlight", false, "Start with the flashlight on").Get();
        m_start =
            app.CVars()
                .Register<std::string>("sandbox.start", "", "Where to start: 'x y z yaw pitch', in meters and degrees")
                .Get();
        m_menuAtStart =
            app.CVars()
                .Register<bool>("sandbox.menu", true, "Show the main menu at the start; false plays at once")
                .Get();
        m_actions = sandbox::MakeActions();
        m_assetRoot = FindAssetRoot();
        DAI_LOG_INFO("sandbox", "Assets in '{}'", m_assetRoot.string());

        // The interface outlives the levels, which come and go with their scenes.
        m_interfaceAssets = std::make_unique<scene::AssetLibrary>(app.Renderer(), m_assetRoot);
        core::Result<sandbox::HudAssets> hud = sandbox::LoadHud(*m_interfaceAssets);
        if (!hud) {
            m_interfaceAssets.reset();
            return tl::unexpected(std::move(hud.error()));
        }
        m_hud = *hud;
        m_ui.SetStyle(sandbox::MenuStyle(m_hud));
        AddDevTools(app);

        // The first scene is current at once: the renderer's owner then releases it on any failure.
        app.SetScene(std::make_unique<scene::Scene>(app.Renderer(), m_assetRoot, app.Audio()));
        LoadLevel(app, *app.CurrentScene());
        app.MainWindow().SetMouseCaptured(true);
        return {};
    }

    void OnUpdate(app::Application& app, const core::FrameTime& time) override {
        const input::InputState& input = app.Input();
        const auto delta = static_cast<float>(time.delta.count());
        // A menu closes in one frame and the game goes on in a later one, once the button that closed it
        // is released: the guns fire for as long as it is held.
        if (m_resumePending && !m_actions.IsDown(input, sandbox::Fire)) {
            m_resumePending = false;
            if (app.CurrentScene() != nullptr) {
                app.CurrentScene()->SetPaused(false);
            }
        }
        m_settings.mouseSensitivity = app.MouseSensitivity();

        if (m_loading) {
            m_loadingSeconds += delta;
        } else if (m_screen == sandbox::Screen::None) {
            if (m_actions.WasPressed(input, sandbox::Menu)) {
                OpenScreen(app, sandbox::Screen::Pause);
            } else {
                if (m_actions.WasPressed(input, sandbox::ToggleMouse)) {
                    m_mouseCaptured = !m_mouseCaptured;
                    app.MainWindow().SetMouseCaptured(m_mouseCaptured);
                }
                if (m_actions.WasPressed(input, sandbox::Restart)) {
                    Restart(app);
                }
                m_playSeconds += delta;
                WatchTheEnd(app, delta);
            }
        } else if (m_actions.WasPressed(input, sandbox::Menu)) {
            if (m_screen == sandbox::Screen::Pause) {
                OpenScreen(app, sandbox::Screen::None);
            } else if (m_screen == sandbox::Screen::Options) {
                CloseOptions(app);
            }
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
        DrawInterface(app);
    }

    void OnStop(app::Application& app) override {
        m_nextScene.reset();
        m_interfaceAssets.reset();
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
        m_loadingSeconds = 0.0f;
        if (m_levelPath == kCornellBox || m_levelPath == kMirrorRoom) {
            OnLevelLoaded(app, target, core::MakeError(core::ErrorCode::NotFound, "sandbox", "Built in code"));
            return;
        }
        target.Assets().LoadModelAsync(
            m_levelPath, app.Workers(),
            [this, &app, &target](core::Result<const scene::Model*> model) { OnLevelLoaded(app, target, model); });
    }

    void OnLevelLoaded(app::Application& app, scene::Scene& target, const core::Result<const scene::Model*>& model) {
        m_loading = false;
        m_levelShown = true;
        if (core::Result<void> built = Build(app, target, model); !built) {
            DAI_LOG_ERROR("sandbox", "{}", core::Format(built.error()));
            m_failure = std::move(built.error());
            app.RequestQuit();
            return;
        }
        m_playSeconds = 0.0f;
        m_endSeconds = 0.0f;
        if (m_nextScene) {
            app.SetScene(std::move(m_nextScene));
            OpenScreen(app, sandbox::Screen::None);
        } else {
            // The first level waits behind the main menu.
            OpenScreen(app, m_menuAtStart ? sandbox::Screen::Main : sandbox::Screen::None);
        }
    }

    /// Starts the level over in a new scene, which replaces this one once it is built.
    void Restart(app::Application& app) {
        if (m_loading) {
            return;
        }
        m_screen = sandbox::Screen::None;
        m_ui.ResetFocus();
        m_nextScene = std::make_unique<scene::Scene>(app.Renderer(), m_assetRoot, app.Audio());
        LoadLevel(app, *m_nextScene);
    }

    /// Shows a screen, or none to play. The scene stops behind every screen, and the mouse is free to
    /// point at buttons.
    void OpenScreen(app::Application& app, sandbox::Screen screen) {
        if (screen == sandbox::Screen::Options) {
            m_optionsReturn = m_screen;
            m_settingsChanged = false;
        }
        m_screen = screen;
        m_ui.ResetFocus();
        const bool playing = screen == sandbox::Screen::None;
        m_mouseCaptured = playing;
        app.MainWindow().SetMouseCaptured(playing);
        if (playing) {
            m_resumePending = true;
        } else if (scene::Scene* current = app.CurrentScene()) {
            current->SetPaused(true);
        }
    }

    void CloseOptions(app::Application& app) {
        if (m_settingsChanged) {
            if (core::Result<void> saved = app.SaveSettings(); !saved) {
                DAI_LOG_ERROR("sandbox", "{}", core::Format(saved.error()));
            }
        }
        OpenScreen(app, m_optionsReturn);
    }

    /// Death, or the last zombie's, ends the game after a moment, the time to see it happen.
    void WatchTheEnd(app::Application& app, float delta) {
        constexpr float kEndDelay = 2.0f;
        const sandbox::Player* player = FindPlayer(app);
        if (player == nullptr) {
            return;
        }
        const bool dead = player->dead;
        const bool cleared = player->zombieCount > 0 && player->kills >= player->zombieCount;
        if (!dead && !cleared) {
            m_endSeconds = 0.0f;
            return;
        }
        m_endSeconds += delta;
        if (m_endSeconds >= kEndDelay) {
            OpenScreen(app, dead ? sandbox::Screen::Dead : sandbox::Screen::Complete);
        }
    }

    void HandleChoice(app::Application& app, sandbox::MenuChoice choice) {
        switch (choice) {
        case sandbox::MenuChoice::None:
            break;
        case sandbox::MenuChoice::Play:
        case sandbox::MenuChoice::Resume:
            OpenScreen(app, sandbox::Screen::None);
            break;
        case sandbox::MenuChoice::Restart:
            Restart(app);
            break;
        case sandbox::MenuChoice::Options:
            OpenScreen(app, sandbox::Screen::Options);
            break;
        case sandbox::MenuChoice::Back:
            CloseOptions(app);
            break;
        case sandbox::MenuChoice::Quit:
            app.RequestQuit();
            break;
        }
    }

    [[nodiscard]] static const sandbox::Player* FindPlayer(app::Application& app) {
        const sandbox::Player* found = nullptr;
        if (scene::Scene* current = app.CurrentScene()) {
            current->World().Each<sandbox::Player>(
                [&found](ecs::Entity /*entity*/, const sandbox::Player& player) { found = &player; });
        }
        return found;
    }

    core::Result<void> Build(app::Application& app, scene::Scene& target,
                             const core::Result<const scene::Model*>& model) {
        if (m_levelPath == kCornellBox) {
            const renderer::CornellBox box = renderer::MakeCornellBox();
            DAI_TRY(BuildTestRoom(app, target, box.parts, {&box.light, 1}, 2.75f));
        } else if (m_levelPath == kMirrorRoom) {
            const renderer::MirrorRoom room = renderer::MakeMirrorRoom();
            DAI_TRY(BuildTestRoom(app, target, room.parts, room.lights, 3.0f));
        } else if (model) {
            core::Result<ecs::Entity> player = sandbox::BuildLevel(target, **model, m_levelPath);
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
        // In front of the Cornell box's opening, unless told otherwise.
        std::istringstream text(!m_start.empty()             ? m_start
                                : m_levelPath == kCornellBox ? std::string("0 1.5 9 0 0")
                                : m_levelPath == kMirrorRoom ? std::string("0.6 1.6 6 0 -5")
                                                             : m_start);
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
        DAI_TRY_ASSIGN(renderer::MeshHandle cube, app.Renderer().CreateMesh(renderer::MakeCube(), "Test floor"));
        ecs::World& world = target.World();
        const ecs::Entity floor = world.Create();
        ecs::LocalTransform transform;
        transform.translation = {0.0f, -0.05f, 0.0f};
        transform.scale = {12.0f, 0.1f, 12.0f};
        world.Add<ecs::LocalTransform>(floor, transform);
        world.Add<scene::MeshRenderer>(floor, cube);
        const std::array<glm::vec3, 4> corners{glm::vec3(-6.0f, 0.0f, -6.0f), glm::vec3(-6.0f, 0.0f, 6.0f),
                                               glm::vec3(6.0f, 0.0f, 6.0f), glm::vec3(6.0f, 0.0f, -6.0f)};
        const std::array<std::uint32_t, 6> triangles{0, 1, 2, 0, 2, 3};
        DAI_TRY(target.Physics().AddStaticMesh(corners, triangles));
        DAI_TRY(sandbox::BuildLevel(target, scene::Model{}, ""));
        return {};
    }

    /// A test room built of cubes: the Cornell box (renderer::MakeCornellBox), the test scene of the path
    /// tracer, with sandbox.scene cornell, or the mirror room (renderer::MakeMirrorRoom), of reflections and
    /// glass, with sandbox.scene mirrors. Its floor spans x and z from -half to half, and goes on in front.
    static core::Result<void> BuildTestRoom(app::Application& app, scene::Scene& target,
                                            std::span<const renderer::CornellPart> parts,
                                            std::span<const renderer::Light> lights, float half) {
        DAI_TRY_ASSIGN(renderer::MeshHandle cube, app.Renderer().CreateMesh(renderer::MakeCube(), "Test room"));
        ecs::World& world = target.World();
        for (const renderer::CornellPart& part : parts) {
            DAI_TRY_ASSIGN(renderer::MaterialHandle material, app.Renderer().CreateMaterial(part.material));
            const ecs::Entity entity = world.Create();
            ecs::LocalTransform transform;
            transform.translation = part.position;
            transform.rotation = glm::angleAxis(part.yaw, glm::vec3(0.0f, 1.0f, 0.0f));
            transform.scale = part.size;
            world.Add<ecs::LocalTransform>(entity, transform);
            world.Add<scene::MeshRenderer>(entity, cube, material);
        }
        for (const renderer::Light& lamp : lights) {
            const ecs::Entity light = world.Create();
            ecs::LocalTransform lightTransform;
            lightTransform.translation = lamp.position;
            world.Add<ecs::LocalTransform>(light, lightTransform);
            world.Add<scene::Light>(
                light,
                scene::Light{.type = lamp.type, .color = lamp.color, .range = lamp.range, .falloff = lamp.falloff});
        }
        // Something to stand on, inside the room and in front of it.
        const std::array<glm::vec3, 4> corners{glm::vec3(-half, 0.0f, -half), glm::vec3(-half, 0.0f, 12.0f),
                                               glm::vec3(half, 0.0f, 12.0f), glm::vec3(half, 0.0f, -half)};
        const std::array<std::uint32_t, 6> triangles{0, 1, 2, 0, 2, 3};
        DAI_TRY(target.Physics().AddStaticMesh(corners, triangles));
        DAI_TRY(sandbox::BuildLevel(target, scene::Model{}, ""));
        return {};
    }

    /// The game's own interface, in every build.
    void DrawInterface(app::Application& app) {
        ui::Canvas& canvas = app.Canvas();
        const platform::Extent window = app.MainWindow().Size();
        m_ui.Begin(canvas, app.Input(), {static_cast<float>(window.width), static_cast<float>(window.height)});
        if (m_loading) {
            sandbox::DrawLoading(canvas, m_hud, !m_levelShown, m_loadingSeconds);
            return;
        }
        const sandbox::Player* player = FindPlayer(app);
        if (player != nullptr && (m_screen == sandbox::Screen::None || m_screen == sandbox::Screen::Dead)) {
            sandbox::DrawHud(canvas, m_hud, *player);
        }

        sandbox::MenuChoice choice = sandbox::MenuChoice::None;
        switch (m_screen) {
        case sandbox::Screen::None:
            break;
        case sandbox::Screen::Main:
            choice = sandbox::DrawMainMenu(m_ui, canvas, m_hud);
            break;
        case sandbox::Screen::Pause:
            choice = sandbox::DrawPauseMenu(m_ui, canvas, m_hud);
            break;
        case sandbox::Screen::Options:
            choice = sandbox::DrawOptions(m_ui, canvas, m_hud, app, m_settingsChanged);
            break;
        case sandbox::Screen::Dead:
            if (player != nullptr) {
                choice = sandbox::DrawDeathScreen(m_ui, canvas, m_hud, *player);
            }
            break;
        case sandbox::Screen::Complete:
            if (player != nullptr) {
                choice = sandbox::DrawCompleteScreen(m_ui, canvas, m_hud, *player, m_playSeconds);
            }
            break;
        }
        HandleChoice(app, choice);
    }

    /// The game's part of the developer tools, in development builds: its own window, the player in
    /// the entity inspector, and a restart command.
    void AddDevTools(app::Application& app) {
#if DAI_DEV_TOOLS
        app::DevTools* tools = app.Tools();
        if (tools == nullptr) {
            return;
        }
        tools->AddWindow("Game", [&app] {
            if (scene::Scene* current = app.CurrentScene()) {
                current->World().Each<sandbox::Player>([](ecs::Entity /*entity*/, const sandbox::Player& player) {
                    ImGui::Text("Zombies %d / %d", player.kills, player.zombieCount);
                });
            }
            ImGui::TextDisabled("WASD: move. Shift: run. Space: jump. C: crouch. E: use. F: flashlight.");
            ImGui::TextDisabled("Esc: menu. F5: restart. Tab: free the mouse.");
            ImGui::TextDisabled("Left button: fire. R: reload. 1, 2, Q or the wheel: weapons.");
        });
        tools->AddInspector<sandbox::Player>("Player", [](sandbox::Player& player) {
            ImGui::SliderInt("Health", &player.health, 0, 200);
            ImGui::SliderInt("Armor", &player.armor, 0, 200);
            ImGui::Checkbox("Has the flashlight", &player.hasFlashlight);
            ImGui::Checkbox("Has the red card", &player.hasRedCard);
            for (std::size_t kind = 0; kind < sandbox::kWeaponKinds; ++kind) {
                sandbox::WeaponState& weapon = player.weapons[kind];
                ImGui::PushID(static_cast<int>(kind));
                ImGui::Checkbox(kind == 0 ? "Handgun" : "Shotgun", &weapon.owned);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(120.0f);
                ImGui::InputInt("Reserve", &weapon.reserve);
                ImGui::PopID();
            }
            ImGui::Text("Feet %.2f %.2f %.2f, %d kills of %d", player.feet.x, player.feet.y, player.feet.z,
                        player.kills, player.zombieCount);
        });
        tools->GetConsole().AddCommand(
            "restart", "Starts the level over, as F5 does",
            [this, &app](app::Console& /*console*/, std::span<const std::string> /*args*/) { Restart(app); });
#else
        static_cast<void>(app);
#endif
    }

    input::ActionMap m_actions;
    std::filesystem::path m_assetRoot;
    std::string m_levelPath;
    /// The value of sandbox.scene that builds the Cornell box instead of loading a level.
    static constexpr std::string_view kCornellBox = "cornell";
    /// The value of sandbox.scene that builds the mirror room.
    static constexpr std::string_view kMirrorRoom = "mirrors";
    sandbox::Settings m_settings;
    std::string m_start;
    std::unique_ptr<scene::Scene> m_nextScene;
    std::unique_ptr<scene::AssetLibrary> m_interfaceAssets;
    sandbox::HudAssets m_hud;
    ui::Context m_ui;
    sandbox::Screen m_screen = sandbox::Screen::None;
    /// Where the options go back to.
    sandbox::Screen m_optionsReturn = sandbox::Screen::Main;
    bool m_settingsChanged = false;
    bool m_menuAtStart = true;
    bool m_resumePending = false;
    /// Seconds of play in this level, and since it ended, by death or victory.
    float m_playSeconds = 0.0f;
    float m_endSeconds = 0.0f;
    float m_loadingSeconds = 0.0f;
    bool m_loading = false;
    /// False until the first level is built: loading covers the screen until then.
    bool m_levelShown = false;
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
    // The engine's settings, then the game's: the build leaves commented copies of both next to the
    // executable.
    config.configFiles = {"daidalos.cfg", "sandbox.cfg"};
    config.arguments.assign(argv + 1, argv + argc);
    config.window.title = "Daidalos Sandbox";
    config.rendering = true;
    config.audio = true;

    app::Application application(std::move(config));
    Sandbox sandbox;
    core::Result<void> result = application.Run(sandbox);
    if (result && sandbox.Failure().has_value()) {
        result = tl::unexpected(*sandbox.Failure());
    }
    if (!result) {
        const std::string message = core::Format(result.error());
        std::fprintf(stderr, "%s\n", message.c_str());
        // Started from a file browser, the player has no console: the reason must be on screen.
        platform::ShowErrorMessage("Daidalos Sandbox", message);
        return 1;
    }
    return 0;
}
