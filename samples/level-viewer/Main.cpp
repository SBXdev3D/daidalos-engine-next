// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// The smallest program that shows a level with the engine (docs/GUIDE.md): an application, a scene, a level
// file made into entities, and a camera that flies with the mouse and the keyboard.
//
//   level-viewer [+viewer.level Factory/Factory.level.json] [+renderer.preset high] ...
//
// Hold the right mouse button and move the mouse to look; W, A, S and D move, Q and E go down and up, Shift
// goes faster; Escape quits.

#include <daidalos/app/Application.hpp>
#include <daidalos/ecs/Transform.hpp>
#include <daidalos/logging/Log.hpp>
#include <daidalos/math/Axes.hpp>
#include <daidalos/math/Transform.hpp>
#include <daidalos/platform/Platform.hpp>
#include <daidalos/scene/Components.hpp>
#include <daidalos/scene/Level.hpp>
#include <daidalos/scene/Scene.hpp>

#include <glm/common.hpp>

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

using namespace daidalos;

namespace {

constexpr std::string_view kModule = "viewer";

/// assets/ beside the program, or in a directory above it: where the build and the archive put it.
std::filesystem::path FindAssets() {
    std::error_code error;
    for (std::filesystem::path directory = platform::ExecutableDirectory(); !directory.empty();
         directory = directory.parent_path()) {
        if (std::filesystem::is_directory(directory / "assets", error)) {
            return directory / "assets";
        }
        if (directory == directory.parent_path()) {
            break;
        }
    }
    return "assets";
}

class Viewer final : public app::IApplicationHandler {
public:
    core::Result<void> OnStart(app::Application& app) override {
        // Settings come from the config files and the command line: +viewer.level <path under assets/>.
        const std::string levelPath =
            app.CVars().Register<std::string>("viewer.level", "Factory/Factory.level.json", "Level file under assets/").Get();

        // A scene owns the entities, the assets they use and the physics world. The application draws it.
        auto scene = std::make_unique<scene::Scene>(app.Renderer(), FindAssets());
        DAI_TRY_ASSIGN(scene::Level level, scene::LoadLevelFile(FindAssets() / levelPath));
        m_level = std::make_unique<scene::LevelInstance>(*scene, levelPath);
        m_level->Build(level);

        // The camera is an entity like any other; the scene draws from the active one.
        ecs::World& world = scene->World();
        m_camera = world.Create();
        world.Add<ecs::LocalTransform>(m_camera);
        world.Add<scene::Camera>(m_camera, scene::Camera{});
        scene->SetActiveCamera(m_camera);
        // Start where the level's player starts, if it says.
        for (const scene::LevelEntity& entity : level.entities) {
            if (entity.game && entity.game->kind == "player") {
                m_position = entity.transform.translation + glm::vec3(0.0f, 1.6f, 0.0f);
            }
        }
        DAI_LOG_INFO(kModule, "Showing {}: {} entities", levelPath, level.entities.size());
        app.SetScene(std::move(scene));
        return {};
    }

    void OnUpdate(app::Application& app, const core::FrameTime& time) override {
        const input::InputState& input = app.Input();
        if (input.WasPressed(platform::Key::Escape)) {
            app.RequestQuit();
        }
        const auto seconds = static_cast<float>(time.delta.count());
        if (input.IsDown(platform::MouseButton::Right)) {
            constexpr float kTurn = 0.003f; // Radians per pixel.
            m_yaw -= input.MouseDelta().x * kTurn;
            m_pitch = glm::clamp(m_pitch - input.MouseDelta().y * kTurn, -1.5f, 1.5f);
        }
        const glm::quat rotation = math::FromYawPitchRoll(m_yaw, m_pitch, 0.0f);
        glm::vec3 move{0.0f};
        const auto axis = [&input](platform::Key plus, platform::Key minus) {
            return (input.IsDown(plus) ? 1.0f : 0.0f) - (input.IsDown(minus) ? 1.0f : 0.0f);
        };
        move += rotation * math::kForward * axis(platform::Key::W, platform::Key::S);
        move += rotation * glm::vec3(1.0f, 0.0f, 0.0f) * axis(platform::Key::D, platform::Key::A);
        move += glm::vec3(0.0f, 1.0f, 0.0f) * axis(platform::Key::E, platform::Key::Q);
        const float speed = input.IsDown(platform::Key::LeftShift) ? 12.0f : 4.0f; // Meters per second.
        m_position += move * speed * seconds;

        // The camera entity's transform is what the scene draws from.
        if (scene::Scene* scene = app.CurrentScene()) {
            math::Transform& transform = scene->World().Get<ecs::LocalTransform>(m_camera);
            transform.translation = m_position;
            transform.rotation = rotation;
        }
    }

    void OnStop(app::Application& /*app*/) override { m_level.reset(); }

private:
    std::unique_ptr<scene::LevelInstance> m_level;
    ecs::Entity m_camera;
    glm::vec3 m_position{0.0f, 1.6f, 0.0f};
    float m_yaw = 0.0f;
    float m_pitch = 0.0f;
};

} // namespace

int main(int argc, char** argv) {
    app::ApplicationConfig config;
    config.log.filePath = "level-viewer.log";
    config.crashDumpDirectory = "crashdumps";
    config.arguments.assign(argv + 1, argv + argc);
    config.window.title = "Daidalos level viewer";
    // A viewer has no player settings to save.
    config.userConfigFile.clear();
    config.rendering = true;

    app::Application application(config);
    Viewer viewer;
    if (const core::Result<void> result = application.Run(viewer); !result) {
        std::fprintf(stderr, "%s\n", core::Format(result.error()).c_str());
        return 1;
    }
    return 0;
}
