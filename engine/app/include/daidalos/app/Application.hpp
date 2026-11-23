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
#include <daidalos/input/Rumble.hpp>
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
    /// Puts a scene in place of the current one at once and gives the current one back, alive: an editor's tabs
    /// keep the scenes of the documents not shown (ADR-0109). Call it from a handler's OnUpdate, not from a
    /// scene's own systems.
    [[nodiscard]] std::unique_ptr<scene::Scene> ExchangeScene(std::unique_ptr<scene::Scene> scene);
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
    /// The gamepads' motors (ADR-0076): what the game pulses and holds there is sent after each OnUpdate,
    /// scaled by input.rumble.
    [[nodiscard]] input::RumbleMixer& Rumble() noexcept { return m_rumble; }
    /// Saves the next frame drawn (ADR-0077): base.png, the image as displayed but without the interface,
    /// and with exr, base.exr, the scene's light before bloom and the display transform. The files are
    /// written on a worker; TakeSavedCaptures says when. F12 saves a PNG in CaptureDirectory.
    void CaptureFrame(std::filesystem::path base, bool exr);
    /// Whether a capture is waiting for its frame or being written.
    [[nodiscard]] bool Capturing() const { return m_capture.has_value() || m_capturesWriting > 0; }
    /// The PNG of each capture written since the last call. Failures are logged instead.
    [[nodiscard]] std::vector<std::filesystem::path> TakeSavedCaptures();
    /// Screenshots, beside the player's settings file.
    [[nodiscard]] std::filesystem::path CaptureDirectory() const;
    /// A file name in CaptureDirectory for a new capture, from the time: daidalos-2026-11-18-071757, without
    /// its extension.
    [[nodiscard]] std::filesystem::path NewCapturePath() const;
    /// Recording (ADR-0123): whether record.directory names where to record. The sound then plays on no device:
    /// a recording reads it, so that it keeps pace with the frames however long they take to draw.
    [[nodiscard]] bool RecordingAvailable() const;
    /// From the next frame, each frame stands for 1/record.fps seconds, however long it takes, and is saved in
    /// record.directory as frame_000000.jpg (or .png, by record.format), interface included, the sound beside
    /// them as sound.wav when the recording ends. Fails without record.directory or rendering.
    core::Result<void> StartRecording();
    /// No more frames: those drawn are still written, then the sound; Recording turns false when all is.
    void StopRecording();
    /// Whether a recording goes on or is still being written.
    [[nodiscard]] bool Recording() const noexcept { return m_recording.has_value(); }
    /// Valid between OnStart and OnStop, when ApplicationConfig::rendering is set.
    [[nodiscard]] renderer::Renderer& Renderer();
    /// The game's interface for this frame, begun again before each frame's updates and drawn over the
    /// scene (ADR-0027). Empty without ApplicationConfig::rendering.
    [[nodiscard]] ui::Canvas& Canvas() noexcept { return m_canvas; }
    /// The crash reports written at this start, for runs that crashed since the last one (ADR-0080): what the
    /// game may tell the player about.
    [[nodiscard]] const std::vector<std::filesystem::path>& CrashReports() const noexcept { return m_crashReports; }
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
    /// Hands a captured frame to a worker to write.
    void WriteCaptures();
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
    double m_soundMilliseconds = 0.0;
    bool m_gpuTimesLogged = false;
    std::optional<core::CVar<std::string>> m_upscaler;
    std::optional<core::CVar<std::string>> m_toneMapper;
    std::optional<core::CVar<bool>> m_hdr;
    std::optional<core::CVar<double>> m_hdrPaperWhite;
    std::optional<core::CVar<double>> m_hdrPeak;
    std::optional<core::CVar<std::int64_t>> m_anisotropy;
    std::optional<core::CVar<bool>> m_asyncCompute;
    std::optional<core::CVar<bool>> m_pipelineStatistics;
    std::optional<core::CVar<double>> m_fieldOfView;
    std::optional<core::CVar<double>> m_viewDistance;
    std::optional<core::CVar<std::string>> m_antialiasing;
    std::optional<core::CVar<bool>> m_fullscreen;
    std::optional<core::CVar<double>> m_normalStrength;
    std::optional<core::CVar<bool>> m_indirectLight;
    std::optional<core::CVar<bool>> m_denoiser;
    std::optional<core::CVar<bool>> m_reflections;
    std::optional<core::CVar<double>> m_reflectionRoughness;
    std::optional<core::CVar<std::int64_t>> m_reflectionPixelsPerRay;
    std::optional<core::CVar<bool>> m_volumetricFog;
    std::optional<core::CVar<bool>> m_tracedParticles;
    std::optional<core::CVar<double>> m_filmGrain;
    std::optional<core::CVar<double>> m_vignette;
    std::optional<core::CVar<double>> m_chromaticAberration;
    std::optional<core::CVar<double>> m_lensDirt;
    std::optional<core::CVar<double>> m_motionBlur;
    std::optional<core::CVar<std::string>> m_colorVision;
    std::optional<core::CVar<std::int64_t>> m_fogSlices;
    std::optional<core::CVar<std::int64_t>> m_fogTile;
    std::optional<core::CVar<double>> m_particleDensity;
    std::optional<core::CVar<std::string>> m_quality;
    std::optional<core::CVar<bool>> m_restirSpatial;
    std::optional<core::CVar<bool>> m_restirUnbiased;
    std::optional<core::CVar<std::int64_t>> m_restirCandidates;
    std::optional<core::CVar<std::int64_t>> m_giBounces;
    std::optional<core::CVar<std::int64_t>> m_giLightCandidates;
    std::optional<core::CVar<double>> m_giClamp;
    std::optional<core::CVar<std::int64_t>> m_giRays;
    std::optional<core::CVar<std::int64_t>> m_giNewRays;
    std::optional<core::CVar<std::int64_t>> m_giPixelsPerRay;
    std::optional<core::CVar<bool>> m_validation;
    std::optional<core::CVar<bool>> m_syncValidation;
    std::optional<core::CVar<std::string>> m_debugView;
    std::optional<core::CVar<std::int64_t>> m_pathBounces;
    std::optional<core::CVar<std::int64_t>> m_pathSamples;
    std::optional<core::CVar<double>> m_volume;
    std::optional<core::CVar<double>> m_effectsVolume;
    std::optional<core::CVar<double>> m_musicVolume;
    std::optional<core::CVar<double>> m_mouseSensitivity;
    std::optional<core::CVar<double>> m_rumbleStrength;
    input::RumbleMixer m_rumble;
    struct PendingCapture {
        std::filesystem::path base;
        bool exr = false;
    };
    std::optional<PendingCapture> m_capture;
    // A recording (ADR-0123).
    struct ActiveRecording {
        std::filesystem::path directory;
        std::string extension;
        int quality = 95;
        double fps = 60.0;
        bool stopping = false;
        std::uint64_t framesAsked = 0;
        std::uint64_t framesTaken = 0;
        std::uint64_t framesFailed = 0;
        std::size_t writing = 0;
        std::uint32_t sampleRate = 48000;
        std::uint32_t channels = 2;
        std::uint64_t soundFrames = 0;
        std::vector<float> sound;
    };
    /// Asks for this frame's image and reads its sound, before it is drawn.
    void RecordFrame();
    /// Hands the frames read back to workers, waiting for them when too many are being written, and ends a
    /// stopped recording once all is written.
    void WriteRecording();
    /// Writes the sound and closes the recording.
    void FinishRecording();
    std::optional<ActiveRecording> m_recording;
    /// The sound plays on no device, for recordings to read.
    bool m_soundForRecording = false;
    std::optional<core::CVar<std::string>> m_recordDirectory;
    std::optional<core::CVar<std::int64_t>> m_recordFps;
    std::optional<core::CVar<std::string>> m_recordFormat;
    std::optional<core::CVar<std::int64_t>> m_recordQuality;
    std::size_t m_capturesWriting = 0;
    std::vector<std::filesystem::path> m_savedCaptures;
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
    std::vector<std::filesystem::path> m_crashReports;
    /// Frames the current scene has been played, not paused, for the first launch's measurement.
    std::uint64_t m_playingFrames = 0;
    bool m_running = false;
};

} // namespace daidalos::app
