// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/app/Application.hpp>
#include <daidalos/core/Assert.hpp>
#include <daidalos/core/Profile.hpp>
#include <daidalos/platform/CrashHandler.hpp>

#include <algorithm>
#include <chrono>
#include <format>
#include <thread>
#include <utility>
#include <variant>

namespace daidalos::app {

namespace {

constexpr std::string_view kModule = "app";
constexpr std::int64_t kMaxWindowSize = 32768;
// While minimized there is nothing to draw: wait for events instead of spinning.
constexpr std::chrono::milliseconds kMinimizedWait{100};
// Asset decoding is the only customer: more threads than this would only compete for the disk.
constexpr std::uint32_t kMaxWorkerThreads = 4;
#ifdef NDEBUG
constexpr bool kValidationByDefault = false;
#else
constexpr bool kValidationByDefault = true;
#endif

core::Result<std::uint32_t> WindowDimension(const core::CVar<std::int64_t>& cvar) {
    const std::int64_t value = cvar.Get();
    if (value < 1 || value > kMaxWindowSize) {
        return core::MakeError(
            core::ErrorCode::InvalidArgument, kModule,
            std::format("{} is {}; it must be between 1 and {}", cvar.Name(), value, kMaxWindowSize));
    }
    return static_cast<std::uint32_t>(value);
}

// Names the step in the message of its failure: "Starting renderer failed: ...".
template <class T>
core::Result<T> Starting(std::string_view step, core::Result<T> result) {
    if (!result) {
        result.error().message = std::format("Starting {} failed: {}", step, result.error().message);
    }
    return result;
}

} // namespace

Application::Application(ApplicationConfig config) : m_config(std::move(config)) {}

Application::~Application() = default;

platform::Window& Application::MainWindow() {
    DAI_ASSERT(m_window.has_value(), "The main window exists only while the application runs");
    return *m_window;
}

core::WorkerPool& Application::Workers() {
    DAI_ASSERT(m_workers.has_value(), "The worker pool exists only while the application runs");
    return *m_workers;
}

void Application::SetScene(std::unique_ptr<scene::Scene> scene) {
    if (m_looping) {
        m_pendingScene = std::move(scene);
        m_scenePending = true;
    } else {
        m_currentScene = std::move(scene);
    }
}

void Application::ApplyPendingScene() {
    if (m_scenePending) {
        m_scenePending = false;
        m_currentScene = std::move(m_pendingScene);
    }
}

renderer::Renderer& Application::Renderer() {
    DAI_ASSERT(m_renderer.has_value(), "The renderer exists only with ApplicationConfig::rendering, while running");
    return *m_renderer;
}

core::Result<void> Application::Run(IApplicationHandler& handler) {
    DAI_ASSERT(!m_running, "Application::Run is already running");
    m_running = true;
    m_quitRequested = false;
    m_failure.reset();
    m_input = {};

    core::Result<void> result = Start(handler);
    if (result) {
        ReportConfigurationProblems();
        DAI_LOG_INFO(kModule, "Running");
        MainLoop(handler);
        DAI_LOG_INFO(kModule, "Shutting down");
        handler.OnStop(*this);
    }
    Stop();

    m_running = false;
    if (m_failure.has_value()) {
        return tl::unexpected(std::move(*m_failure));
    }
    return result;
}

core::Result<void> Application::Start(IApplicationHandler& handler) {
    DAI_TRY(Starting("logging", logging::Initialize(m_config.log)));
    m_loggingStarted = true;
    if (!m_config.crashDumpDirectory.empty()) {
        DAI_TRY(Starting("crash handler", platform::InstallCrashHandler({m_config.crashDumpDirectory, "crash"})));
        m_crashHandlerInstalled = true;
    }
    DAI_TRY(Starting("configuration", LoadConfiguration()));

    DAI_TRY_ASSIGN(platform::Platform platform, Starting("platform", platform::Platform::Create(m_config.platform)));
    m_platform.emplace(std::move(platform));
    DAI_TRY(Starting("main window", CreateMainWindow()));

    const std::uint32_t cores = std::thread::hardware_concurrency();
    m_workers.emplace(std::clamp<std::uint32_t>(cores > 1 ? cores - 1 : 1, 1, kMaxWorkerThreads));
    CreateAudio();
    DAI_TRY(Starting("renderer", CreateRenderer()));
    DAI_TRY(Starting("debug overlay", CreateDebugOverlay()));
    return Starting("application", handler.OnStart(*this));
}

void Application::Stop() {
    // The scene first, because its assets and sounds belong to the renderer and the audio engine. The
    // handler may have created one before failing in OnStart.
    m_pendingScene.reset();
    m_scenePending = false;
    m_currentScene.reset();
#if DAI_DEV_TOOLS
    m_debugUi.reset();
#endif
    m_renderer.reset();
    m_audio.reset();
    m_workers.reset();
    m_window.reset();
    m_platform.reset();
    if (std::exchange(m_crashHandlerInstalled, false)) {
        platform::UninstallCrashHandler();
    }
    if (std::exchange(m_loggingStarted, false)) {
        logging::Shutdown();
    }
}

core::Result<void> Application::LoadConfiguration() {
    if (!m_windowWidth) {
        m_windowWidth = m_cvars.Register<std::int64_t>("window.width", m_config.window.width,
                                                       "Main window width, in screen coordinates");
        m_windowHeight = m_cvars.Register<std::int64_t>("window.height", m_config.window.height,
                                                        "Main window height, in screen coordinates");
        m_vsync = m_cvars.Register<bool>("renderer.vsync", true, "Synchronize presentation with the display");
        m_exposure = m_cvars.Register<double>("renderer.exposure", 1.0, "Multiplies the light before tonemapping");
        m_anisotropy = m_cvars.Register<std::int64_t>(
            "renderer.anisotropy", 16, "Anisotropic filtering of textures, from 1 (off) to 16, read at startup");
        m_ambientOcclusion = m_cvars.Register<bool>("renderer.ambient_occlusion", true,
                                                    "Darken the ambient light in creases and corners");
        m_normalStrength = m_cvars.Register<double>(
            "renderer.normal_strength", 1.0, "Multiplies the relief of normal maps: 0 flattens them, 1 as authored");
        m_validation = m_cvars.Register<bool>("renderer.validation", kValidationByDefault,
                                              "Enable the Vulkan validation layers, read at startup");
        m_volume = m_cvars.Register<double>("audio.volume", 1.0, "Overall volume, from 0 to 1, read at startup");
        m_effectsVolume = m_cvars.Register<double>("audio.effects", 1.0, "Volume of sound effects, read at startup");
        m_musicVolume = m_cvars.Register<double>("audio.music", 1.0, "Volume of music, read at startup");
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
    config.vulkan = m_config.rendering;

    DAI_TRY_ASSIGN(platform::Window window, platform::Window::Create(*m_platform, config));
    m_window.emplace(std::move(window));
    return {};
}

void Application::CreateAudio() {
    if (!m_config.audio) {
        return;
    }
    core::Result<audio::AudioEngine> created = audio::AudioEngine::Create({
        .volume = static_cast<float>(m_volume->Get()),
        .effectsVolume = static_cast<float>(m_effectsVolume->Get()),
        .musicVolume = static_cast<float>(m_musicVolume->Get()),
    });
    if (created) {
        m_audio.emplace(std::move(*created));
    } else {
        DAI_LOG_WARNING(kModule, "Running without sound: {}", core::Format(created.error()));
    }
}

core::Result<void> Application::CreateRenderer() {
    if (!m_config.rendering) {
        return {};
    }
    DAI_TRY_ASSIGN(renderer::Renderer created,
                   renderer::Renderer::Create(*m_window, {.validation = m_validation->Get(),
                                                          .vsync = m_vsync->Get(),
                                                          .exposure = static_cast<float>(m_exposure->Get()),
                                                          .anisotropy = static_cast<float>(
                                                              std::clamp<std::int64_t>(m_anisotropy->Get(), 1, 16)),
                                                          .normalStrength = static_cast<float>(m_normalStrength->Get()),
                                                          .ambientOcclusion = m_ambientOcclusion->Get()}));
    m_renderer.emplace(std::move(created));
    return {};
}

core::Result<void> Application::CreateDebugOverlay() {
#if DAI_DEV_TOOLS
    if (m_renderer.has_value()) {
        DAI_TRY_ASSIGN(debugui::DebugUi created,
                       debugui::DebugUi::Create(m_renderer->Device(), m_renderer->ColorFormat()));
        m_debugUi.emplace(std::move(created));
    }
#endif
    return {};
}

void Application::RenderFrame(IApplicationHandler& handler) {
    m_scene.camera.reset();
    m_scene.draws.clear();
    m_scene.atmosphere = {};
    m_scene.lights.clear();
    m_scene.particleSpawns.clear();
    m_scene.timeStep = 0.0f;
    if (m_currentScene) {
        m_currentScene->Extract(m_scene);
    }
    handler.OnRender(*this, m_scene);

    renderer::OverlayRecorder overlay;
    core::Result<void> overlayResult;
#if DAI_DEV_TOOLS
    m_debugUi->EndFrame();
    overlay = [this, &overlayResult](rhi::CommandList& commands, rhi::Extent extent) {
        overlayResult = m_debugUi->Draw(commands, extent);
    };
#endif

    m_renderer->SetVsync(m_vsync->Get());
    m_renderer->SetExposure(static_cast<float>(m_exposure->Get()));
    m_renderer->SetNormalStrength(static_cast<float>(m_normalStrength->Get()));
    m_renderer->SetAmbientOcclusion(m_ambientOcclusion->Get());
    core::Result<void> rendered = m_renderer->Render(m_scene, overlay);
    if (rendered && !overlayResult) {
        rendered = std::move(overlayResult);
    }
    if (!rendered) {
        DAI_LOG_ERROR(kModule, "Rendering failed: {}", core::Format(rendered.error()));
        m_failure = std::move(rendered.error());
        RequestQuit();
    }
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

    m_looping = true;
    while (!m_quitRequested) {
        ApplyPendingScene();
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
#if DAI_DEV_TOOLS
                if (m_debugUi.has_value()) {
                    m_debugUi->ProcessEvent(event);
                }
#endif
                handler.OnEvent(*this, event);
            }
        }
        if (m_quitRequested) {
            break;
        }

        m_workers->Deliver();

        const auto now = std::chrono::steady_clock::now();
        const core::FrameTime time = timer.Advance(now - previous);
        previous = now;

        {
            DAI_PROFILE_SCOPE("FixedUpdate");
            for (std::uint32_t step = 0; step < time.fixedSteps; ++step) {
                handler.OnFixedUpdate(*this, timer.Config().fixedStep);
                if (m_currentScene) {
                    m_currentScene->RunFixedStep(timer.Config().fixedStep);
                }
            }
        }
#if DAI_DEV_TOOLS
        if (m_debugUi.has_value()) {
            m_debugUi->BeginFrame(time.unclampedDelta, m_window->Size(), m_window->PixelSize());
        }
#endif
        {
            DAI_PROFILE_SCOPE("Update");
            handler.OnUpdate(*this, time);
            if (m_currentScene) {
                m_currentScene->RunFrame(time.delta, static_cast<float>(time.alpha));
            }
        }
        if (m_renderer.has_value()) {
            DAI_PROFILE_SCOPE("Render");
            RenderFrame(handler);
        }
        DAI_PROFILE_FRAME();
    }
    m_looping = false;
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
