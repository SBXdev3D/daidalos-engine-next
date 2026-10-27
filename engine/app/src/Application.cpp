// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/app/Application.hpp>
#include <daidalos/app/StartupSequence.hpp>
#include <daidalos/core/Assert.hpp>
#include <daidalos/core/Profile.hpp>
#include <daidalos/platform/CrashHandler.hpp>

#include <chrono>
#include <format>
#include <utility>
#include <variant>

namespace daidalos::app {

namespace {

constexpr std::string_view kModule = "app";
constexpr std::int64_t kMaxWindowSize = 32768;
// While minimized there is nothing to draw: wait for events instead of spinning.
constexpr std::chrono::milliseconds kMinimizedWait{100};

core::Result<std::uint32_t> WindowDimension(const core::CVar<std::int64_t>& cvar) {
    const std::int64_t value = cvar.Get();
    if (value < 1 || value > kMaxWindowSize) {
        return core::MakeError(
            core::ErrorCode::InvalidArgument, std::string(kModule),
            std::format("{} is {}; it must be between 1 and {}", cvar.Name(), value, kMaxWindowSize));
    }
    return static_cast<std::uint32_t>(value);
}

} // namespace

Application::Application(ApplicationConfig config) : m_config(std::move(config)) {}

Application::~Application() = default;

platform::Window& Application::MainWindow() {
    DAI_ASSERT(m_window.has_value(), "The main window exists only while the application runs");
    return *m_window;
}

core::Result<void> Application::Run(IApplicationHandler& handler) {
    DAI_ASSERT(!m_running, "Application::Run is already running");
    m_running = true;
    m_quitRequested = false;
    m_input = {};

    StartupSequence startup;
    startup.Add("logging", [this] { return logging::Initialize(m_config.log); }, [] { logging::Shutdown(); });
    startup.Add(
        "crash handler",
        [this]() -> core::Result<void> {
            if (m_config.crashDumpDirectory.empty()) {
                return {};
            }
            return platform::InstallCrashHandler({m_config.crashDumpDirectory, "crash"});
        },
        [] { platform::UninstallCrashHandler(); });
    startup.Add("configuration", [this] { return LoadConfiguration(); });
    startup.Add(
        "platform",
        [this]() -> core::Result<void> {
            core::Result<platform::Platform> created = platform::Platform::Create(m_config.platform);
            if (!created) {
                return tl::unexpected(std::move(created.error()));
            }
            m_platform.emplace(std::move(*created));
            return {};
        },
        [this] { m_platform.reset(); });
    startup.Add("main window", [this] { return CreateMainWindow(); }, [this] { m_window.reset(); });
    startup.Add(
        "application", [this, &handler] { return handler.OnStart(*this); },
        [this, &handler] { handler.OnStop(*this); });

    core::Result<void> result = startup.Start();
    if (result) {
        ReportConfigurationProblems();
        DAI_LOG_INFO(kModule, "Running");
        MainLoop(handler);
        DAI_LOG_INFO(kModule, "Shutting down");
        startup.Stop();
    }

    m_running = false;
    return result;
}

core::Result<void> Application::LoadConfiguration() {
    if (!m_windowWidth) {
        m_windowWidth = m_cvars.Register<std::int64_t>("window.width", m_config.window.width,
                                                       "Main window width, in screen coordinates");
        m_windowHeight = m_cvars.Register<std::int64_t>("window.height", m_config.window.height,
                                                        "Main window height, in screen coordinates");
    }

    if (!m_config.configFile.empty()) {
        if (core::Result<void> loaded = m_cvars.LoadConfigFile(m_config.configFile); loaded) {
            DAI_LOG_INFO(kModule, "Configuration loaded from '{}'", m_config.configFile.generic_string());
        } else if (loaded.error().code == core::ErrorCode::NotFound) {
            DAI_LOG_INFO(kModule, "No configuration file '{}', using defaults", m_config.configFile.generic_string());
        } else {
            return loaded;
        }
    }
    return m_cvars.ApplyCommandLine(m_config.arguments);
}

core::Result<void> Application::CreateMainWindow() {
    platform::WindowConfig config = m_config.window;
    const core::Result<std::uint32_t> width = WindowDimension(*m_windowWidth);
    if (!width) {
        return tl::unexpected(width.error());
    }
    const core::Result<std::uint32_t> height = WindowDimension(*m_windowHeight);
    if (!height) {
        return tl::unexpected(height.error());
    }
    config.width = *width;
    config.height = *height;

    core::Result<platform::Window> window = platform::Window::Create(*m_platform, config);
    if (!window) {
        return tl::unexpected(std::move(window.error()));
    }
    m_window.emplace(std::move(*window));
    return {};
}

void Application::ReportConfigurationProblems() {
    // Every module has registered its cvars by now, so the remaining problems are mistakes in the
    // config file or on the command line.
    for (const core::Error& error : m_cvars.TakeErrors()) {
        DAI_LOG_WARNING(kModule, "Ignored setting: {}", error.message);
    }
    for (const std::string& name : m_cvars.UnregisteredNames()) {
        DAI_LOG_WARNING(kModule, "Unknown setting '{}' was ignored", name);
    }
}

void Application::MainLoop(IApplicationHandler& handler) {
    core::FrameTimer timer(m_config.frameTimer);
    std::vector<platform::Event> events;
    auto previous = std::chrono::steady_clock::now();

    while (!m_quitRequested) {
        {
            DAI_PROFILE_SCOPE("Events");
            m_input.BeginFrame();
            events.clear();
            if (m_window->IsMinimized()) {
                m_platform->WaitEvents(kMinimizedWait, events);
            } else {
                m_platform->PollEvents(events);
            }
            for (const platform::Event& event : events) {
                m_input.Process(event);
                HandleEvent(event);
                handler.OnEvent(*this, event);
            }
        }
        if (m_quitRequested) {
            break;
        }

        const auto now = std::chrono::steady_clock::now();
        const core::FrameTime time = timer.Advance(now - previous);
        previous = now;

        {
            DAI_PROFILE_SCOPE("FixedUpdate");
            for (std::uint32_t step = 0; step < time.fixedSteps; ++step) {
                handler.OnFixedUpdate(*this, timer.Config().fixedStep);
            }
        }
        {
            DAI_PROFILE_SCOPE("Update");
            handler.OnUpdate(*this, time);
        }
        DAI_PROFILE_FRAME();
    }
}

void Application::HandleEvent(const platform::Event& event) {
    if (std::holds_alternative<platform::QuitRequested>(event)) {
        DAI_LOG_INFO(kModule, "Quit requested by the system");
        RequestQuit();
    } else if (const auto* close = std::get_if<platform::WindowCloseRequested>(&event);
               close != nullptr && close->window == m_window->Id()) {
        DAI_LOG_INFO(kModule, "Main window closed");
        RequestQuit();
    }
}

} // namespace daidalos::app
