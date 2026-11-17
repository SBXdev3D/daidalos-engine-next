// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/app/Quality.hpp>
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
#include <daidalos/renderer/SceneView.hpp>
#include <daidalos/scene/Scene.hpp>
#include <daidalos/ui/Canvas.hpp>
#if DAI_DEV_TOOLS
#include <daidalos/app/Console.hpp>
#include <daidalos/app/DevTools.hpp>
#include <daidalos/debugui/DebugUi.hpp>
#endif

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
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

    /// Called when the main window is closed or the system asks to quit. Returning false keeps the application
    /// running, to ask about unsaved work first; RequestQuit then ends it.
    virtual bool OnCloseRequested(Application& /*app*/) { return true; }

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
    /// Config files with "name value" lines, read in order, so that a later one wins. A relative path is
    /// next to the executable, where the build copies the engine's daidalos.cfg; a game adds its own
    /// file after it. Missing files are skipped.
    std::vector<std::filesystem::path> configFiles{"daidalos.cfg"};
    /// The player's own settings, which Application::SaveSettings writes, read after the config files
    /// and before the command line. Relative to the executable's directory, like them. Empty reads and
    /// writes none.
    std::filesystem::path userConfigFile{"user.cfg"};
    /// Where the pipeline cache is kept between runs (ADR-0052), next to the executable if relative, as
    /// the player's settings. Empty for none.
    std::filesystem::path pipelineCacheFile{"pipelines.cache"};
    /// Command-line arguments; "+name value" pairs override cvars.
    std::vector<std::string> arguments;
    platform::PlatformConfig platform;
    /// Initial window settings. The size is the default of the window.width and window.height cvars.
    platform::WindowConfig window;
    /// Starts the renderer, which needs Vulkan and a video driver that supports it (not "dummy").
    bool rendering = false;
    /// Opens the audio output. Without one, the game runs silently.
    bool audio = false;
    core::FrameTimerConfig frameTimer;
};

/// Starts the engine, runs the main loop and shuts everything down in reverse order.
///
/// Startup order: logging, crash handler, configuration (cvars), platform, main window, worker pool,
/// audio, renderer, debug overlay, handler. The scene, if any, is the first to go.
/// If a step fails, the steps already started are stopped in reverse order and Run returns the error.
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
    /// The game's interface for this frame, begun again before each frame's updates and drawn over the
    /// scene (ADR-0027). Empty without ApplicationConfig::rendering.
    [[nodiscard]] ui::Canvas& Canvas() noexcept { return m_canvas; }
#if DAI_DEV_TOOLS
    /// The overlay, or null without ApplicationConfig::rendering. Valid between OnStart and OnStop.
    [[nodiscard]] debugui::DebugUi* DebugOverlay() noexcept { return m_debugUi ? &*m_debugUi : nullptr; }
    /// The developer tools (ADR-0030), or null without ApplicationConfig::rendering. Valid between
    /// OnStart and OnStop.
    [[nodiscard]] DevTools* Tools() noexcept { return m_devTools ? &*m_devTools : nullptr; }
    /// Opens or closes the tools, as the key under Escape does. Open, they free the pointer and the game
    /// gets no keyboard or mouse input; closed, the pointer is captured again if it was.
    void SetToolsOpen(bool open);
    /// Every log message and the console's output, while the application runs.
    [[nodiscard]] ConsoleLog& ConsoleOutput() noexcept { return m_consoleLog; }
#endif
    /// Null when silent, which a scene accepts. Scenes must be created with this engine to be heard.
    [[nodiscard]] audio::AudioEngine* Audio() noexcept { return m_audio.has_value() ? &*m_audio : nullptr; }
    [[nodiscard]] core::CVarRegistry& CVars() noexcept { return m_cvars; }
    /// Sets a cvar, as an options menu does, and keeps the value among the player's settings, which
    /// SaveSettings writes. The value is text, as in config files. Fails, changing nothing, if the
    /// name is not registered or the value does not parse.
    core::Result<void> ChangeSetting(std::string_view name, std::string_view value);
    /// Puts a cvar back to its default, or to its quality preset's value, and drops it from the player's
    /// settings.
    void ResetSetting(std::string_view name);
    /// Writes the player's settings to ApplicationConfig::userConfigFile: those it held at the start
    /// and those changed since, so that settings from the command line are not kept by accident.
    core::Result<void> SaveSettings();
    /// The quality preset in use (ADR-0052): renderer.quality, or High while the first launch measures
    /// which one this GPU holds.
    [[nodiscard]] Quality CurrentQuality() const;
    /// Whether a setting the presets decide was set by a config file, the command line or the player:
    /// the menu then shows Custom.
    [[nodiscard]] bool QualityIsCustom() const;
    /// Whether the first launch is still measuring which preset to choose.
    [[nodiscard]] bool ChoosingQuality() const;
    /// Chooses a preset as the options menu does: the settings it decides go back to it, among the
    /// player's settings too. SaveSettings writes the choice.
    core::Result<void> ChooseQuality(Quality quality);
    /// The settings the renderer uses for what the presets decide: the preset's, unless set.
    [[nodiscard]] QualitySettings EffectiveQualitySettings() const;
    /// Radians of turn per unit of mouse movement, from input.mouse_sensitivity.
    [[nodiscard]] float MouseSensitivity() const;
    [[nodiscard]] const ApplicationConfig& Config() const noexcept { return m_config; }

private:
    /// Starts each step in order. Stop releases what a failure left started.
    core::Result<void> Start(IApplicationHandler& handler);
    /// Stops whatever is started, in reverse order.
    void Stop();
    core::Result<void> LoadConfiguration();
    core::Result<void> LoadUserSettings();
    [[nodiscard]] std::filesystem::path UserConfigPath() const;
    [[nodiscard]] std::filesystem::path PipelineCachePath() const;
    core::Result<void> CreateMainWindow();
    void CreateAudio();
    core::Result<void> CreateRenderer();
    /// The renderer.gi_* variables.
    [[nodiscard]] renderer::IndirectLightConfig IndirectLight() const;
    [[nodiscard]] renderer::ImageConfig Image() const;
    [[nodiscard]] renderer::ResolutionConfig Resolution() const;
    core::Result<void> CreateDebugOverlay();
    void RenderFrame(IApplicationHandler& handler);
    void ReportConfigurationProblems();
    void MainLoop(IApplicationHandler& handler);
    void HandleEvent(IApplicationHandler& handler, const platform::Event& event);
    void ApplyPendingScene();
    /// After a frame with a scene: locks pipeline creation, and measures the frame for the first launch's
    /// choice of preset.
    void AfterSceneFrame();
    void LogGpuTimes();

    ApplicationConfig m_config;
    core::CVarRegistry m_cvars;
    /// The player's settings, by name, as text: see ChangeSetting.
    std::map<std::string, std::string, std::less<>> m_userSettings;
    std::optional<core::CVar<std::int64_t>> m_windowWidth;
    std::optional<core::CVar<std::int64_t>> m_windowHeight;
    std::optional<core::CVar<bool>> m_vsync;
    std::optional<core::CVar<double>> m_exposure;
    std::optional<core::CVar<bool>> m_autoExposure;
    std::optional<core::CVar<double>> m_bloom;
    std::optional<core::CVar<double>> m_sharpness;
    std::optional<core::CVar<double>> m_renderScale;
    std::optional<core::CVar<bool>> m_dynamicResolution;
    std::optional<core::CVar<double>> m_minRenderScale;
    std::optional<core::CVar<std::int64_t>> m_targetFps;
    /// renderer.log_gpu_times: the passes' GPU times, summed over the frames logged.
    std::optional<core::CVar<std::int64_t>> m_logGpuTimes;
    std::map<std::string, double> m_gpuTimes;
    std::uint32_t m_gpuTimeFrames = 0;
    bool m_gpuTimesLogged = false;
    std::optional<core::CVar<std::string>> m_upscaler;
    std::optional<core::CVar<std::string>> m_toneMapper;
    std::optional<core::CVar<bool>> m_hdr;
    std::optional<core::CVar<double>> m_hdrPaperWhite;
    std::optional<core::CVar<double>> m_hdrPeak;
    std::optional<core::CVar<std::int64_t>> m_anisotropy;
    std::optional<core::CVar<double>> m_fieldOfView;
    std::optional<core::CVar<double>> m_viewDistance;
    std::optional<core::CVar<std::string>> m_antialiasing;
    std::optional<core::CVar<bool>> m_fullscreen;
    std::optional<core::CVar<double>> m_normalStrength;
    std::optional<core::CVar<bool>> m_indirectLight;
    std::optional<core::CVar<bool>> m_denoiser;
    std::optional<core::CVar<bool>> m_reflections;
    std::optional<core::CVar<double>> m_reflectionRoughness;
    std::optional<core::CVar<bool>> m_volumetricFog;
    std::optional<core::CVar<bool>> m_tracedParticles;
    std::optional<core::CVar<std::int64_t>> m_fogSlices;
    std::optional<core::CVar<std::int64_t>> m_fogTile;
    std::optional<core::CVar<double>> m_particleDensity;
    std::optional<core::CVar<std::string>> m_quality;
    std::optional<core::CVar<bool>> m_restirSpatial;
    std::optional<core::CVar<bool>> m_restirUnbiased;
    std::optional<core::CVar<std::int64_t>> m_giBounces;
    std::optional<core::CVar<std::int64_t>> m_giLightCandidates;
    std::optional<core::CVar<double>> m_giClamp;
    std::optional<core::CVar<std::int64_t>> m_giRays;
    std::optional<core::CVar<bool>> m_validation;
    std::optional<core::CVar<bool>> m_syncValidation;
    std::optional<core::CVar<std::string>> m_debugView;
    std::optional<core::CVar<std::int64_t>> m_pathBounces;
    std::optional<core::CVar<std::int64_t>> m_pathSamples;
    std::optional<core::CVar<double>> m_volume;
    std::optional<core::CVar<double>> m_effectsVolume;
    std::optional<core::CVar<double>> m_musicVolume;
    std::optional<core::CVar<double>> m_mouseSensitivity;
    std::optional<core::CVar<bool>> m_directInput;
    std::optional<platform::Platform> m_platform;
    std::optional<platform::Window> m_window;
    std::optional<core::WorkerPool> m_workers;
    std::optional<audio::AudioEngine> m_audio;
    std::optional<renderer::Renderer> m_renderer;
#if DAI_DEV_TOOLS
    ConsoleLog m_consoleLog;
    std::optional<debugui::DebugUi> m_debugUi;
    std::optional<DevTools> m_devTools;
    /// Whether the pointer was captured when the tools opened.
    bool m_mouseCapturedBeforeTools = false;
#endif
    std::unique_ptr<scene::Scene> m_currentScene;
    std::unique_ptr<scene::Scene> m_pendingScene;
    /// Frames drawn with a scene, and the GPU milliseconds of those the first launch measures.
    std::uint32_t m_sceneFrames = 0;
    double m_probeMilliseconds = 0.0;
    std::uint32_t m_probeFrames = 0;
    bool m_scenePending = false;
    bool m_looping = false;
    renderer::SceneView m_scene;
    ui::Canvas m_canvas;
    input::InputState m_input;
    std::optional<core::Error> m_failure;
    bool m_quitRequested = false;
    bool m_loggingStarted = false;
    bool m_crashHandlerInstalled = false;
    bool m_running = false;
};

} // namespace daidalos::app
