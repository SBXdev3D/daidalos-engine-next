// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <daidalos/core/CVarRegistry.hpp>
#include <daidalos/core/FrameTimer.hpp>
#include <daidalos/core/Result.hpp>
#include <daidalos/input/InputState.hpp>
#include <daidalos/logging/Log.hpp>
#include <daidalos/platform/Event.hpp>
#include <daidalos/platform/Platform.hpp>
#include <daidalos/platform/Window.hpp>

#include <cstdint>
#include <filesystem>
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
    core::FrameTimerConfig frameTimer;
};

/// Starts the engine, runs the main loop and shuts everything down in reverse order.
///
/// Startup order: logging, crash handler, configuration (cvars), platform, main window, handler.
/// If a step fails, the steps already started are stopped (StartupSequence) and Run returns the error.
/// The loop ends when the main window is closed, the system asks to quit, or RequestQuit is called.
class Application {
public:
    explicit Application(ApplicationConfig config);
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    ~Application();

    /// Runs until the application quits. Returns an error if the startup fails.
    core::Result<void> Run(IApplicationHandler& handler);

    /// Ends the loop after the current frame.
    void RequestQuit() noexcept { m_quitRequested = true; }

    /// Valid between OnStart and OnStop.
    [[nodiscard]] platform::Window& MainWindow();
    /// Keyboard, mouse and gamepad state, updated with the frame's events before OnFixedUpdate and
    /// OnUpdate. Presses and releases last one frame: read them in OnUpdate, because OnFixedUpdate
    /// may run zero or several times per frame.
    [[nodiscard]] const input::InputState& Input() const noexcept { return m_input; }
    [[nodiscard]] core::CVarRegistry& CVars() noexcept { return m_cvars; }
    [[nodiscard]] const ApplicationConfig& Config() const noexcept { return m_config; }

private:
    core::Result<void> LoadConfiguration();
    core::Result<void> CreateMainWindow();
    void ReportConfigurationProblems();
    void MainLoop(IApplicationHandler& handler);
    void HandleEvent(const platform::Event& event);

    ApplicationConfig m_config;
    core::CVarRegistry m_cvars;
    std::optional<core::CVar<std::int64_t>> m_windowWidth;
    std::optional<core::CVar<std::int64_t>> m_windowHeight;
    std::optional<platform::Platform> m_platform;
    std::optional<platform::Window> m_window;
    input::InputState m_input;
    bool m_quitRequested = false;
    bool m_running = false;
};

} // namespace daidalos::app
