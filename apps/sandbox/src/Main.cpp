// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/app/Application.hpp>
#include <daidalos/ecs/Transform.hpp>
#include <daidalos/math/Axes.hpp>
#include <daidalos/renderer/AssetLibrary.hpp>
#include <daidalos/renderer/Primitives.hpp>

#if DAI_DEV_TOOLS
#include <imgui.h>
#endif

#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace daidalos;

constexpr const char* kAssetRoot = "assets"; // Relative to the working directory.
constexpr glm::vec3 kModelPosition{-0.5f, 0.0f, -3.0f};
constexpr float kModelScale = 0.15f;        // The sample lantern is much taller than a person.
constexpr float kMoveSpeed = 4.0f;          // Meters per second.
constexpr float kFastMoveFactor = 3.0f;     // While Shift is held.
constexpr float kMouseSensitivity = 0.003f; // Radians per mouse unit.
constexpr float kStickLookSpeed = 2.0f;     // Radians per second.
constexpr float kStickDeadZone = 0.15f;     // Sticks rest slightly off center.
constexpr float kMaxPitch = glm::radians(89.0f);
constexpr float kSpinSpeed = glm::radians(45.0f); // Radians per second.

float ApplyDeadZone(float value) {
    return (std::abs(value) < kStickDeadZone) ? 0.0f : value;
}

// Draws a few cubes and flies a camera around them: W, A, S, D, Q and E to move, right mouse button
// to look, or the sticks of a gamepad. Quits when the window is closed or Escape (or a gamepad's Back
// button) is pressed.
class Sandbox final : public app::IApplicationHandler {
public:
    core::Result<void> OnStart(app::Application& app) override {
        DAI_LOG_INFO("sandbox", "Daidalos Engine Next {} started", DAI_VERSION);
        const platform::Extent size = app.MainWindow().PixelSize();
        DAI_LOG_INFO("sandbox", "Main window: {}x{} pixels", size.width, size.height);
        m_frameLimit =
            app.CVars().Register<std::int64_t>("sandbox.frames", 0, "Quit after this many frames; 0 runs until closed");
        const core::CVar<std::string> scene = app.CVars().Register<std::string>(
            "sandbox.scene", "Lantern/glTF/Lantern.gltf", "Model to load from the assets directory");
        if (core::Result<void> loaded = LoadModel(app.Renderer(), scene.Get()); !loaded) {
            return loaded;
        }
        return BuildScene(app.Renderer());
    }

    void OnEvent(app::Application& /*app*/, const platform::Event& event) override {
        if (const auto* resized = std::get_if<platform::WindowResized>(&event)) {
            DAI_LOG_DEBUG("sandbox", "Resized to {}x{} pixels", resized->pixelSize.width, resized->pixelSize.height);
        }
    }

    void OnUpdate(app::Application& app, const core::FrameTime& time) override {
        if (QuitPressed(app.Input())) {
            DAI_LOG_INFO("sandbox", "Quit requested from input");
            app.RequestQuit();
        }
        ++m_totalFrames;
        if (m_frameLimit->Get() > 0 && m_totalFrames >= m_frameLimit->Get()) {
            app.RequestQuit();
        }

        const auto delta = static_cast<float>(time.unclampedDelta.count());
        UpdateCamera(app, delta);

        // A parent that turns, carrying its child: the transform hierarchy at work.
        m_spin += kSpinSpeed * delta;
        m_world.Get<ecs::LocalTransform>(m_spinner).rotation = glm::angleAxis(m_spin, math::kUp);

        ++m_frames;
        m_elapsed += time.unclampedDelta;
        if (m_elapsed >= core::Seconds{1.0}) {
            m_fps = static_cast<double>(m_frames) / m_elapsed.count();
            app.MainWindow().SetTitle(std::format("Daidalos Sandbox - {:.0f} FPS", m_fps));
            m_frames = 0;
            m_elapsed = core::Seconds::zero();
        }
        DrawDebugPanel(app);
    }

    void OnRender(app::Application& /*app*/, renderer::SceneView& scene) override {
        ecs::PropagateTransforms(m_world);
        renderer::ExtractScene(m_world, scene);
    }

    void OnStop(app::Application& app) override {
        m_assets.reset();
        DAI_LOG_INFO("sandbox", "Stopping after {} frames, {} presented", m_totalFrames,
                     app.Renderer().Device().PresentedFrameCount());
    }

private:
    // The sample scene is downloaded (scripts/fetch_sample_assets.py). Without it, the cubes stand alone.
    core::Result<void> LoadModel(renderer::Renderer& target, const std::string& path) {
        m_assets = std::make_unique<renderer::AssetLibrary>(target, kAssetRoot);
        core::Result<const renderer::Model*> model = m_assets->LoadModel(path);
        if (!model) {
            if (model.error().code == core::ErrorCode::NotFound) {
                DAI_LOG_INFO("sandbox", "No sample scene: {}", core::Format(model.error()));
                return {};
            }
            return tl::unexpected(std::move(model.error()));
        }
        core::Result<ecs::Entity> root = renderer::Instantiate(m_world, **model);
        if (!root) {
            return tl::unexpected(std::move(root.error()));
        }
        ecs::LocalTransform& transform = m_world.Get<ecs::LocalTransform>(*root);
        transform.translation = kModelPosition;
        transform.scale = glm::vec3(kModelScale);
        return {};
    }

    core::Result<void> BuildScene(renderer::Renderer& target) {
        core::Result<renderer::MeshHandle> cube = target.CreateMesh(renderer::MakeCube(), "Cube");
        if (!cube) {
            return tl::unexpected(std::move(cube.error()));
        }
        const auto colored = [&target](const glm::vec3& color) {
            renderer::MaterialDesc desc;
            desc.baseColorFactor = glm::vec4(color, 1.0f);
            return target.CreateMaterial(desc);
        };
        core::Result<renderer::MaterialHandle> gray = colored({0.5f, 0.5f, 0.55f});
        core::Result<renderer::MaterialHandle> red = colored({0.8f, 0.15f, 0.1f});
        core::Result<renderer::MaterialHandle> blue = colored({0.1f, 0.3f, 0.8f});
        if (!gray || !red || !blue) {
            return tl::unexpected(std::move(!gray ? gray.error() : (!red ? red.error() : blue.error())));
        }

        AddMesh(*cube, *gray, {0.0f, -0.05f, 0.0f}, {12.0f, 0.1f, 12.0f}); // The floor.
        AddMesh(*cube, *gray, {-2.5f, 0.5f, -1.0f}, glm::vec3(1.0f));
        m_spinner = AddMesh(*cube, *red, {1.0f, 0.75f, 0.0f}, {1.0f, 1.5f, 1.0f});
        const ecs::Entity satellite = AddMesh(*cube, *blue, {1.5f, 0.0f, 0.0f}, glm::vec3(0.4f));
        if (core::Result<void> attached = m_world.SetParent(satellite, m_spinner); !attached) {
            return attached;
        }

        m_camera = m_world.Create();
        ecs::LocalTransform transform;
        transform.translation = {0.0f, 1.6f, 6.0f};
        m_world.Add<ecs::LocalTransform>(m_camera, transform);
        m_world.Add<renderer::Camera>(m_camera);
        return {};
    }

    ecs::Entity AddMesh(renderer::MeshHandle mesh, renderer::MaterialHandle material, const glm::vec3& position,
                        const glm::vec3& scale) {
        const ecs::Entity entity = m_world.Create();
        ecs::LocalTransform transform;
        transform.translation = position;
        transform.scale = scale;
        m_world.Add<ecs::LocalTransform>(entity, transform);
        m_world.Add<renderer::MeshRenderer>(entity, mesh, material);
        return entity;
    }

    [[nodiscard]] static bool QuitPressed(const input::InputState& input) {
        if (input.WasPressed(platform::Key::Escape)) {
            return true;
        }
        for (const platform::GamepadId gamepad : input.Gamepads()) {
            if (input.WasPressed(gamepad, platform::GamepadButton::Back)) {
                return true;
            }
        }
        return false;
    }

    void UpdateCamera(app::Application& app, float delta) {
        const input::InputState& input = app.Input();
        bool mouseFree = true;
        bool keyboardFree = true;
#if DAI_DEV_TOOLS
        if (const debugui::DebugUi* overlay = app.DebugOverlay()) {
            mouseFree = !overlay->WantsMouse();
            keyboardFree = !overlay->WantsKeyboard();
        }
#endif

        glm::vec3 move(0.0f); // x right, y up, z backward: the camera's local axes.
        if (keyboardFree) {
            const auto axis = [&input](platform::Key positive, platform::Key negative) {
                return (input.IsDown(positive) ? 1.0f : 0.0f) - (input.IsDown(negative) ? 1.0f : 0.0f);
            };
            move.x = axis(platform::Key::D, platform::Key::A);
            move.y = axis(platform::Key::E, platform::Key::Q);
            move.z = axis(platform::Key::S, platform::Key::W);
        }
        if (mouseFree && input.IsDown(platform::MouseButton::Right)) {
            m_yaw -= input.MouseDelta().x * kMouseSensitivity;
            m_pitch -= input.MouseDelta().y * kMouseSensitivity;
        }
        for (const platform::GamepadId gamepad : input.Gamepads()) {
            using platform::GamepadAxis;
            move.x += ApplyDeadZone(input.Axis(gamepad, GamepadAxis::LeftX));
            move.z -= ApplyDeadZone(input.Axis(gamepad, GamepadAxis::LeftY));
            m_yaw -= ApplyDeadZone(input.Axis(gamepad, GamepadAxis::RightX)) * kStickLookSpeed * delta;
            m_pitch += ApplyDeadZone(input.Axis(gamepad, GamepadAxis::RightY)) * kStickLookSpeed * delta;
        }
        m_pitch = std::clamp(m_pitch, -kMaxPitch, kMaxPitch);

        const bool fast = input.IsDown(platform::Key::LeftShift);
        ecs::LocalTransform& transform = m_world.Get<ecs::LocalTransform>(m_camera);
        transform.rotation = math::FromYawPitchRoll(m_yaw, m_pitch, 0.0f);
        transform.translation += transform.rotation * move * kMoveSpeed * (fast ? kFastMoveFactor : 1.0f) * delta;
    }

    void DrawDebugPanel(app::Application& app) {
#if DAI_DEV_TOOLS
        if (app.DebugOverlay() == nullptr) {
            return;
        }
        ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
        ImGui::Begin("Sandbox", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        const rhi::DeviceInfo& info = app.Renderer().Device().Info();
        ImGui::Text("%s", info.name.c_str());
        ImGui::Text("%.0f FPS, validation %s", m_fps, info.validationEnabled ? "on" : "off");
        if (ImGui::Checkbox("Vertical sync", &m_vsync)) {
            // Cannot fail: the variable is registered and the text is valid.
            static_cast<void>(app.CVars().Set("renderer.vsync", m_vsync ? "true" : "false"));
        }
        ImGui::TextDisabled("WASD, QE, Shift: move. Right mouse: look.");
        if (const std::string& error = app.Renderer().ShaderError(); !error.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "Shader reload failed:");
            ImGui::TextUnformatted(error.c_str());
        }
        ImGui::End();
#else
        static_cast<void>(app);
#endif
    }

    std::unique_ptr<renderer::AssetLibrary> m_assets;
    ecs::World m_world;
    ecs::Entity m_camera;
    ecs::Entity m_spinner;
    std::optional<core::CVar<std::int64_t>> m_frameLimit;
    std::int64_t m_totalFrames = 0;
    float m_yaw = 0.0f;
    float m_pitch = 0.0f;
    float m_spin = 0.0f;
    bool m_vsync = true;
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
    if (const core::Result<void> result = application.Run(sandbox); !result) {
        std::fprintf(stderr, "%s\n", core::Format(result.error()).c_str());
        return 1;
    }
    return 0;
}
