// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <daidalos/core/CVarRegistry.hpp>
#include <daidalos/core/FrameTimer.hpp>
#include <daidalos/core/Result.hpp>
#include <daidalos/core/WorkerPool.hpp>
#include <daidalos/input/InputState.hpp>
#include <daidalos/logging/Log.hpp>
#include <daidalos/platform/Event.hpp>
#include <daidalos/platform/Platform.hpp>
#include <daidalos/platform/Window.hpp>
#include <daidalos/renderer/Renderer.hpp>
#include <daidalos/renderer/Scene.hpp>
#include <daidalos/scene/Scene.hpp>
#if DAI_DEV_TOOLS
#include <daidalos/debugui/DebugUi.hpp>
#endif

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace daidalos::app {

class Application;

/// What the application runs: a game, the sandbox, a test. Every function has an empty default.
class IApplicationHandler {
public:
    virtual ~IApplicationHandler() = default;

    /// Called once everything is started and the window exists. Register cvars here.
    /// Returning an error stops the startup; OnStop is then not called.
    virtual core::Result<void> OnStart(Application& /*app*/) { return {}; }

    /// Called for every platform event, after the application has handled it.
    virtual void OnEvent(Application& /*app*/, const platform::Event& /*event*/) {}

    /// Called zero or more times per frame, with a constant step (60 Hz by default), before OnUpdate.
    virtual void OnFixedUpdate(Application& /*app*/, core::Seconds /*step*/) {}

    /// Called once per frame.
    virtual void OnUpdate(Application& /*app*/, const core::FrameTime& /*time*/) {}

    /// Called once per frame after OnUpdate, when ApplicationConfig::rendering is set. The view already
    /// shows the current scene, if there is one: this is the place to add to it, or to fill it by hand.
    virtual void OnRender(Application& /*app*/, renderer::SceneView& /*scene*/) {}

    /// Called once when the loop ends, before anything is shut down.
    virtual void OnStop(Application& /*app*/) {}
};

struct ApplicationConfig {
    logging::LogConfig log;
    /// Directory for crash dumps. Empty disables the crash handler.
    std::filesystem::path crashDumpDirectory;
    /// Config file with "name value" lines. Empty or missing means defaults are used.
    std::filesystem::path configFile;
    /// Command-line arguments; "+name value" pairs override cvars.
    std::vector<std::string> arguments;
    platform::PlatformConfig platform;
    /// Initial window settings. The size is the default of the window.width and window.height cvars.
    platform::WindowConfig window;
    /// Starts the renderer, which needs Vulkan and a video driver that supports it (not "dummy").
    bool rendering = false;
    core::FrameTimerConfig frameTimer;
};

/// Starts the engine, runs the main loop and shuts everything down in reverse order.
///
/// Startup order: logging, crash handler, configuration (cvars), platform, main window, worker pool,
/// renderer, debug overlay, scene slot, handler.
/// If a step fails, the steps already started are stopped (StartupSequence) and Run returns the error.
/// The loop ends when the main window is closed, the system asks to quit, RequestQuit is called, or
/// rendering fails, in which case Run returns that error after shutting down.
class Application {
public:
    explicit Application(ApplicationConfig config);
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    ~Application();

    /// Runs until the application quits. Returns an error if the startup or rendering fails.
    core::Result<void> Run(IApplicationHandler& handler);

    /// Ends the loop after the current frame.
    void RequestQuit() noexcept { m_quitRequested = true; }

    /// Replaces the current scene between two frames, or at once before the loop starts. Null leaves no
    /// scene. The old one is destroyed then, and whichever is current when the application stops goes
    /// before the renderer, also when OnStart fails. Once in place, its systems run every frame and
    /// its world is extracted for rendering.
    void SetScene(std::unique_ptr<scene::Scene> scene);
    /// Null without a scene.
    [[nodiscard]] scene::Scene* CurrentScene() noexcept { return m_currentScene.get(); }

    /// Valid between OnStart and OnStop.
    [[nodiscard]] platform::Window& MainWindow();
    /// Threads for slow work, such as decoding assets. Results are delivered before each frame's
    /// updates. Valid between OnStart and OnStop.
    [[nodiscard]] core::WorkerPool& Workers();
    /// Keyboard, mouse and gamepad state, updated with the frame's events before OnFixedUpdate and
    /// OnUpdate. Presses and releases last one frame: read them in OnUpdate, because OnFixedUpdate
    /// may run zero or several times per frame.
    [[nodiscard]] const input::InputState& Input() const noexcept { return m_input; }
    /// Valid between OnStart and OnStop, when ApplicationConfig::rendering is set.
    [[nodiscard]] renderer::Renderer& Renderer();
#if DAI_DEV_TOOLS
    /// The overlay, or null without ApplicationConfig::rendering. Valid between OnStart and OnStop.
    [[nodiscard]] debugui::DebugUi* DebugOverlay() noexcept { return m_debugUi ? &*m_debugUi : nullptr; }
#endif
    [[nodiscard]] core::CVarRegistry& CVars() noexcept { return m_cvars; }
    [[nodiscard]] const ApplicationConfig& Config() const noexcept { return m_config; }

private:
    core::Result<void> LoadConfiguration();
    core::Result<void> CreateMainWindow();
    core::Result<void> CreateRenderer();
    core::Result<void> CreateDebugOverlay();
    void RenderFrame(IApplicationHandler& handler);
    void ReportConfigurationProblems();
    void MainLoop(IApplicationHandler& handler);
    void HandleEvent(const platform::Event& event);
    void ApplyPendingScene();

    ApplicationConfig m_config;
    core::CVarRegistry m_cvars;
    std::optional<core::CVar<std::int64_t>> m_windowWidth;
    std::optional<core::CVar<std::int64_t>> m_windowHeight;
    std::optional<core::CVar<bool>> m_vsync;
    std::optional<core::CVar<bool>> m_validation;
    std::optional<platform::Platform> m_platform;
    std::optional<platform::Window> m_window;
    std::optional<core::WorkerPool> m_workers;
    std::optional<renderer::Renderer> m_renderer;
#if DAI_DEV_TOOLS
    std::optional<debugui::DebugUi> m_debugUi;
#endif
    std::unique_ptr<scene::Scene> m_currentScene;
    std::unique_ptr<scene::Scene> m_pendingScene;
    bool m_scenePending = false;
    bool m_looping = false;
    renderer::SceneView m_scene;
    input::InputState m_input;
    std::optional<core::Error> m_failure;
    bool m_quitRequested = false;
    bool m_running = false;
};

} // namespace daidalos::app
