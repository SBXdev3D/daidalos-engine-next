// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/app/Application.hpp>
#include <daidalos/ecs/Transform.hpp>
#include <daidalos/platform/Platform.hpp>
#include <daidalos/renderer/CornellBox.hpp>
#include <daidalos/renderer/DarkCorridor.hpp>
#include <daidalos/renderer/MirrorRoom.hpp>
#include <daidalos/renderer/Primitives.hpp>
#include <daidalos/scene/Level.hpp>

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
                .Register<std::string>("sandbox.scene", "Factory/Factory.level.json",
                                       "Level file or glTF model to load from the assets directory, or cornell "
                                       "for the Cornell box")
                .Get();
        m_settings.mouseSensitivity = app.MouseSensitivity();
        m_settings.darkness = static_cast<float>(
            app.CVars()
                .Register<double>("sandbox.darkness", 1.0,
                                  "1 is the factory's darkness; 0 shows it brighter, without its haze")
                .Get());
        m_settings.startWithFlashlight =
            app.CVars().Register<bool>("sandbox.flashlight", false, "Start with the flashlight on").Get();
        m_settings.flashlightBattery =
            static_cast<float>(app.CVars()
                                   .Register<double>("sandbox.flashlight_battery", 900.0,
                                                     "Seconds a full flashlight battery lasts; 0 never runs down")
                                   .Get());
        m_settings.checkpointSave = platform::ExecutableDirectory() / "saves" / "checkpoint.save";
        // Continue from the last checkpoint, if there is one (ADR-0072).
        if (std::error_code missing;
            app.CVars()
                .Register<bool>("sandbox.continue", false, "Start from the last checkpoint instead of the beginning")
                .Get() &&
            std::filesystem::exists(m_settings.checkpointSave, missing)) {
            m_settings.loadSave = m_settings.checkpointSave;
        }
        m_settings.classicMusic =
            app.CVars()
                .Register<bool>("sandbox.classic_music", false,
                                "Play the old demo's music track instead of the score that follows the danger")
                .Get();
        m_settings.aperture = static_cast<float>(
            app.CVars()
                .Register<double>("sandbox.aperture", 0.0,
                                  "The camera's f-number for depth of field, as 2.8 or 8; 0 keeps everything sharp")
                .Get());
        m_settings.focusDistance = static_cast<float>(
            app.CVars().Register<double>("sandbox.focus", 3.0, "Meters the camera is focused at").Get());
        m_start =
            app.CVars()
                .Register<std::string>("sandbox.start", "", "Where to start: 'x y z yaw pitch', in meters and degrees")
                .Get();
        m_showcase = app.CVars()
                         .Register<bool>("sandbox.showcase", false,
                                         "Put the effects of mesh particles and ribbons in front of the start")
                         .Get();
        m_menuAtStart =
            app.CVars()
                .Register<bool>("sandbox.menu", true, "Show the main menu at the start; false plays at once")
                .Get();
        m_calibrated = app.CVars()
                           .Register<bool>("sandbox.calibrated", false,
                                           "The brightness was calibrated; false shows its screen before the menu")
                           .Get();
        // The controls, as the player bound them (ADR-0076), and the gamepads' motors.
        sandbox::RegisterControls(app.CVars());
        m_actions = sandbox::MakeActions(app.CVars());
        m_settings.rumble = &app.Rumble();
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
        // What the player reads and how (ADR-0075).
        m_languageSetting = app.CVars().Register<std::string>(
            "game.language", "en", "The language of the interface, subtitles and captions: a file of assets/Text");
        m_subtitles = app.CVars().Register<bool>("game.subtitles", true, "Show what is said");
        m_captions =
            app.CVars().Register<bool>("game.captions", false, "Show what sounds are heard, and from which side");
        m_textSize =
            app.CVars().Register<double>("game.text_size", 1.0, "Size of subtitles and captions, 1 by default");
        m_cameraShake =
            app.CVars().Register<double>("view.camera_shake", 1.0, "How much the camera shakes, from 0 to 1");
        LoadText();
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
        if (m_languageSetting->Get() != m_language) {
            LoadText();
        }
        if (app.CurrentScene() != nullptr) {
            app.CurrentScene()->SetShakeScale(static_cast<float>(m_cameraShake->Get()));
        }

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
                    m_settings.loadSave.clear();
                    Restart(app);
                }
                // Saves (ADR-0072): F6 saves where the player is, F9 starts the factory over as it was saved, or
                // at the last checkpoint.
                if (m_actions.WasPressed(input, sandbox::QuickSave) && app.CurrentScene() != nullptr) {
                    if (const core::Result<void> saved = sandbox::SaveGame(*app.CurrentScene(), QuickSavePath());
                        !saved) {
                        DAI_LOG_WARNING("sandbox", "Save: {}", core::Format(saved.error()));
                    }
                }
                if (m_actions.WasPressed(input, sandbox::QuickLoad)) {
                    std::error_code missing;
                    const std::filesystem::path quick = QuickSavePath();
                    m_settings.loadSave = std::filesystem::exists(quick, missing) ? quick : m_settings.checkpointSave;
                    if (std::filesystem::exists(m_settings.loadSave, missing)) {
                        Restart(app);
                    } else {
                        m_settings.loadSave.clear();
                    }
                }
                m_playSeconds += delta;
                WatchTheEnd(app, delta);
            }
        } else if ((m_actions.WasPressed(input, sandbox::Menu) || m_actions.WasPressed(input, sandbox::MenuBack)) &&
                   !m_options.capturing.has_value()) {
            if (m_screen == sandbox::Screen::Pause) {
                OpenScreen(app, sandbox::Screen::None);
            } else if (m_screen == sandbox::Screen::Options) {
                CloseOptions(app);
            } else if (m_screen == sandbox::Screen::Calibration) {
                CloseCalibration(app);
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
        if (m_levelPath == kCornellBox || m_levelPath == kMirrorRoom || m_levelPath == kDarkCorridor) {
            OnLevelLoaded(app, target, core::MakeError(core::ErrorCode::NotFound, "sandbox", "Built in code"));
            return;
        }
        // A level file (ADR-0054), or a bare glTF model as a level of one entity. Its models load on workers,
        // behind the loading screen; the entities are made once they are all there.
        const bool bareModel = !m_levelPath.ends_with(".level.json");
        core::Result<scene::Level> level = bareModel
                                               ? core::Result<scene::Level>(scene::LevelOfModel(m_levelPath))
                                               : scene::LoadLevelFile(m_assetRoot / std::filesystem::path(m_levelPath));
        if (!level) {
            OnLevelLoaded(app, target, tl::unexpected(level.error()));
            return;
        }
        const std::vector<std::string> models = scene::LevelModels(*level, m_levelPath);
        if (models.empty()) {
            OnLevelLoaded(app, target, std::move(level));
            return;
        }
        struct Loading {
            scene::Level level;
            std::size_t remaining = 0;
            std::optional<core::Error> failure;
        };
        auto loading = std::make_shared<Loading>(std::move(*level), models.size());
        for (const std::string& model : models) {
            target.Assets().LoadModelAsync(
                model, app.Workers(),
                [this, &app, &target, loading, bareModel](const core::Result<const scene::Model*>& loaded) {
                    // A level file's missing model is logged when its entity is made; a bare model is the level.
                    if (!loaded && bareModel && !loading->failure) {
                        loading->failure = loaded.error();
                    }
                    if (--loading->remaining == 0) {
                        OnLevelLoaded(app, target,
                                      loading->failure ? core::Result<scene::Level>(tl::unexpected(*loading->failure))
                                                       : core::Result<scene::Level>(std::move(loading->level)));
                    }
                });
        }
    }

    void OnLevelLoaded(app::Application& app, scene::Scene& target, const core::Result<scene::Level>& level) {
        m_loading = false;
        m_levelShown = true;
        if (core::Result<void> built = Build(app, target, level); !built) {
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
            // The first level waits behind the main menu, and the first start begins with the brightness.
            if (m_menuAtStart && !m_calibrated) {
                m_screen = sandbox::Screen::Main;
                OpenScreen(app, sandbox::Screen::Calibration);
            } else {
                OpenScreen(app, m_menuAtStart ? sandbox::Screen::Main : sandbox::Screen::None);
            }
        }
    }

    /// Starts the level over in a new scene, which replaces this one once it is built.
    [[nodiscard]] static std::filesystem::path QuickSavePath() {
        return platform::ExecutableDirectory() / "saves" / "quick.save";
    }

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
            m_options.capturing.reset();
            m_ui.Hold(false);
        } else if (screen == sandbox::Screen::Calibration) {
            m_calibrationReturn = m_screen;
        }
        m_screen = screen;
        m_ui.ResetFocus();
        const bool playing = screen == sandbox::Screen::None;
        m_mouseCaptured = playing;
        app.MainWindow().SetMouseCaptured(playing);
        if (playing) {
            m_resumePending = true;
        } else {
            // The pads go still with the game.
            app.Rumble().Stop();
            if (scene::Scene* current = app.CurrentScene()) {
                current->SetPaused(true);
            }
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

    /// Saves the brightness, and that it was calibrated, before going back.
    void CloseCalibration(app::Application& app) {
        if (!m_calibrated) {
            m_calibrated = true;
            if (core::Result<void> marked = app.ChangeSetting("sandbox.calibrated", "true"); !marked) {
                DAI_LOG_WARNING("sandbox", "{}", core::Format(marked.error()));
            }
            m_settingsChanged = true;
        }
        if (m_calibrationReturn == sandbox::Screen::Options) {
            // The options save what changed when they close.
            m_screen = sandbox::Screen::Options;
            m_ui.ResetFocus();
            return;
        }
        if (m_settingsChanged) {
            if (core::Result<void> saved = app.SaveSettings(); !saved) {
                DAI_LOG_ERROR("sandbox", "{}", core::Format(saved.error()));
            }
            m_settingsChanged = false;
        }
        OpenScreen(app, m_calibrationReturn);
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
        case sandbox::MenuChoice::Calibrate:
            OpenScreen(app, sandbox::Screen::Calibration);
            break;
        case sandbox::MenuChoice::Back:
            if (m_screen == sandbox::Screen::Calibration) {
                CloseCalibration(app);
            } else {
                CloseOptions(app);
            }
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

    core::Result<void> Build(app::Application& app, scene::Scene& target, const core::Result<scene::Level>& level) {
        if (m_levelPath == kCornellBox) {
            const renderer::CornellBox box = renderer::MakeCornellBox();
            DAI_TRY(BuildTestRoom(app, target, box.parts, {&box.light, 1}, 2.75f));
        } else if (m_levelPath == kMirrorRoom) {
            const renderer::MirrorRoom room = renderer::MakeMirrorRoom();
            DAI_TRY(BuildTestRoom(app, target, room.parts, room.lights, 3.0f));
        } else if (m_levelPath == kDarkCorridor) {
            // Lit by nothing but the player's flashlight, which is on from the start.
            const renderer::DarkCorridor corridor = renderer::MakeDarkCorridor();
            DAI_TRY(BuildTestRoom(app, target, corridor.parts, {}, 20.0f));
            m_settings.startWithFlashlight = true;
        } else if (level) {
            core::Result<ecs::Entity> player = sandbox::BuildLevel(target, *level, m_levelPath);
            if (!player) {
                return tl::unexpected(player.error());
            }
        } else if (level.error().code == core::ErrorCode::NotFound) {
            // Other levels can be tried with sandbox.scene, so a missing one is not fatal.
            DAI_LOG_INFO("sandbox", "No level, showing a test floor: {}", core::Format(level.error()));
            if (core::Result<void> built = BuildTestFloor(app, target); !built) {
                return built;
            }
        } else {
            return tl::unexpected(level.error());
        }
        sandbox::AddSystems(target, app.Input(), m_actions, m_settings);
        ApplyStart(target);
        if (m_showcase) {
            // In front of where the player starts; entities are created after the search.
            std::optional<std::pair<glm::vec3, float>> start;
            target.World().Each<sandbox::Player, ecs::LocalTransform>(
                [&](ecs::Entity /*entity*/, const sandbox::Player& player, const ecs::LocalTransform& transform) {
                    start.emplace(transform.translation, player.yaw);
                });
            if (start) {
                sandbox::AddEffectShowcase(target, m_levelPath, start->first, start->second);
            }
        }
        return {};
    }

    void ApplyStart(scene::Scene& target) const {
        glm::vec3 position{0.0f};
        float yaw = 0.0f;
        float pitch = 0.0f;
        // In front of the Cornell box's opening, unless told otherwise.
        std::istringstream text(!m_start.empty()               ? m_start
                                : m_levelPath == kCornellBox   ? std::string("0 1.5 9 0 0")
                                : m_levelPath == kMirrorRoom   ? std::string("0.6 1.6 6 0 -5")
                                : m_levelPath == kDarkCorridor ? std::string("0.2 1.5 -1 0 -3")
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
        DAI_TRY(sandbox::BuildLevel(target, scene::Level{}, ""));
        return {};
    }

    /// A test room built of cubes: the Cornell box (renderer::MakeCornellBox), the test scene of the path
    /// tracer, with sandbox.scene cornell, the mirror room (renderer::MakeMirrorRoom), of reflections and
    /// glass, with sandbox.scene mirrors, or the dark corridor (renderer::MakeDarkCorridor), of indirect
    /// light, with sandbox.scene corridor. Its floor spans x and z from -half to half, and goes on in front.
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
            // Lights shine down their entity's -Z.
            lightTransform.rotation = glm::quat(glm::vec3(0.0f, 0.0f, -1.0f), glm::normalize(lamp.direction));
            world.Add<ecs::LocalTransform>(light, lightTransform);
            // The test rooms' colors are in candela already (ADR-0043).
            world.Add<scene::Light>(light, scene::Light{.type = lamp.type,
                                                        .color = lamp.color,
                                                        .range = lamp.range,
                                                        .innerAngle = lamp.innerAngle,
                                                        .outerAngle = lamp.outerAngle});
        }
        // Something to stand on, inside the room and in front of it.
        const std::array<glm::vec3, 4> corners{glm::vec3(-half, 0.0f, -half), glm::vec3(-half, 0.0f, 12.0f),
                                               glm::vec3(half, 0.0f, 12.0f), glm::vec3(half, 0.0f, -half)};
        const std::array<std::uint32_t, 6> triangles{0, 1, 2, 0, 2, 3};
        DAI_TRY(target.Physics().AddStaticMesh(corners, triangles));
        DAI_TRY(sandbox::BuildLevel(target, scene::Level{}, ""));
        return {};
    }

    /// Reads the text of the language the player chose, and the names of the others (ADR-0075).
    void LoadText() {
        m_language = m_languageSetting->Get();
        m_text = scene::TextTable::Load(m_assetRoot, m_language);
        m_hud.text = &m_text;
        m_hud.languages.clear();
        for (const std::string& code : scene::TextTable::Languages(m_assetRoot)) {
            const scene::TextTable table = scene::TextTable::Load(m_assetRoot, code);
            m_hud.languages.emplace_back(code, std::string(table.Get("language.name")));
        }
        ui::WidgetStyle style = sandbox::MenuStyle(m_hud);
        style.onText = std::string(m_text.Get("options.on"));
        style.offText = std::string(m_text.Get("options.off"));
        m_ui.SetStyle(style);
        DAI_LOG_INFO("sandbox", "Text in '{}': {} strings", m_language, m_text.Size());
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
        if (scene::Scene* scene = app.CurrentScene(); scene != nullptr && m_screen == sandbox::Screen::None) {
            const ecs::Entity camera = scene->ActiveCamera();
            const ecs::WorldTransform* eye =
                scene->World().IsAlive(camera) ? scene->World().TryGet<ecs::WorldTransform>(camera) : nullptr;
            if (eye != nullptr) {
                const glm::vec3 position(eye->matrix[3]);
                const glm::quat rotation = glm::quat_cast(glm::mat3(eye->matrix));
                sandbox::DrawCaptions(canvas, m_hud, scene->Captions(), position, glm::normalize(rotation),
                                      {.subtitles = m_subtitles->Get(),
                                       .sounds = m_captions->Get(),
                                       .size = static_cast<float>(m_textSize->Get())});
            }
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
            choice = sandbox::DrawOptions(m_ui, canvas, m_hud, app, m_options, m_settingsChanged);
            if (std::exchange(m_options.rebound, false)) {
                m_actions = sandbox::MakeActions(app.CVars());
            }
            break;
        case sandbox::Screen::Calibration:
            choice = sandbox::DrawCalibration(m_ui, canvas, m_hud, app, m_settingsChanged);
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
    /// The value of sandbox.scene that builds the dark corridor (ADR-0045).
    static constexpr std::string_view kDarkCorridor = "corridor";
    sandbox::Settings m_settings;
    std::string m_start;
    std::unique_ptr<scene::Scene> m_nextScene;
    std::unique_ptr<scene::AssetLibrary> m_interfaceAssets;
    sandbox::HudAssets m_hud;
    /// The interface's text in the player's language, which m_hud points to (ADR-0075).
    scene::TextTable m_text;
    std::string m_language;
    std::optional<core::CVar<std::string>> m_languageSetting;
    std::optional<core::CVar<bool>> m_subtitles;
    std::optional<core::CVar<bool>> m_captions;
    std::optional<core::CVar<double>> m_textSize;
    std::optional<core::CVar<double>> m_cameraShake;
    sandbox::OptionsState m_options;
    ui::Context m_ui;
    sandbox::Screen m_screen = sandbox::Screen::None;
    /// Where the options go back to.
    sandbox::Screen m_optionsReturn = sandbox::Screen::Main;
    /// Where the brightness calibration goes back to.
    sandbox::Screen m_calibrationReturn = sandbox::Screen::Main;
    bool m_calibrated = false;
    bool m_settingsChanged = false;
    bool m_menuAtStart = true;
    /// sandbox.showcase: the effects of mesh particles and ribbons in front of the start.
    bool m_showcase = false;
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
