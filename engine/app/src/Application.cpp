// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/app/Application.hpp>
#include <daidalos/core/Assert.hpp>
#include <daidalos/core/Profile.hpp>
#include <daidalos/platform/CrashHandler.hpp>

#include <glm/trigonometric.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <format>
#include <fstream>
#include <iterator>
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
// The first launch measures High after this many frames with a scene, while the GPU and the histories
// settle, over this many (ADR-0052).
constexpr std::uint32_t kProbeWarmupFrames = 90;
constexpr std::uint32_t kProbeFrames = 120;
// The settings the presets decide, which ChooseQuality puts back to the preset.
constexpr std::array<std::string_view, 12> kPresetSettings{
    "renderer.render_scale",         "renderer.min_render_scale", "renderer.indirect_light",
    "renderer.gi_bounces",           "renderer.gi_rays",          "renderer.reflections",
    "renderer.reflection_roughness", "renderer.restir_spatial",   "renderer.volumetric_fog",
    "renderer.fog_slices",           "renderer.fog_tile",         "renderer.particle_density"};

/// The variable's value if something set it, the preset's otherwise.
template <class T>
T Preset(const core::CVar<T>& variable, T presetValue) {
    return variable.IsSet() ? variable.Get() : presetValue;
}
#ifdef NDEBUG
constexpr bool kValidationByDefault = false;
#else
constexpr bool kValidationByDefault = true;
#endif

float FieldOfViewRadians(const core::CVar<double>& degrees) {
    return glm::radians(static_cast<float>(std::clamp(degrees.Get(), 30.0, 150.0)));
}

float ReflectionRoughness(double roughness) {
    return static_cast<float>(std::clamp(roughness, 0.0, 1.0));
}

std::uint32_t FogSlices(std::int64_t slices) {
    return static_cast<std::uint32_t>(std::clamp<std::int64_t>(slices, 16, 256));
}

std::uint32_t FogTile(std::int64_t pixels) {
    return static_cast<std::uint32_t>(std::clamp<std::int64_t>(pixels, 8, 32));
}

float ViewDistanceMeters(const core::CVar<double>& meters) {
    return static_cast<float>(std::max(meters.Get(), 1.0));
}

/// The view named by renderer.debug_view; an unknown name shows the lit scene, with a warning once.
renderer::DebugView DebugViewMode(const core::CVar<std::string>& cvar) {
    const std::string& name = cvar.Get();
    if (name == "rays") {
        return renderer::DebugView::Rays;
    }
    if (name == "path") {
        return renderer::DebugView::Path;
    }
    if (name == "motion") {
        return renderer::DebugView::Motion;
    }
    if (name == "normals") {
        return renderer::DebugView::Normals;
    }
    if (name == "indirect") {
        return renderer::DebugView::Indirect;
    }
    if (name == "radiance_cache") {
        return renderer::DebugView::RadianceCache;
    }
    if (name == "reflections") {
        return renderer::DebugView::Reflections;
    }
    if (name == "lights") {
        return renderer::DebugView::Lights;
    }
    if (name == "bloom") {
        return renderer::DebugView::Bloom;
    }
    if (name == "fog") {
        return renderer::DebugView::Fog;
    }
    static std::string warned;
    if (name != "off" && name != warned) {
        warned = name;
        DAI_LOG_WARNING(kModule,
                        "Unknown debug view '{}': use off, rays, path, motion, normals, indirect, radiance_cache, "
                        "reflections, lights, bloom or fog",
                        name);
    }

    return renderer::DebugView::Off;
}

/// The mode named by renderer.antialiasing; any other name, such as the fxaa and msaa modes that TAA
/// replaced (ADR-0037), gives TAA, with a warning once.
renderer::Antialiasing AntialiasingMode(const core::CVar<std::string>& cvar) {
    const std::string& name = cvar.Get();
    if (name == "off") {
        return renderer::Antialiasing::Off;
    }
    if (name == "taa") {
        return renderer::Antialiasing::Taa;
    }
    static std::string reported;
    if (reported != name) {
        reported = name;
        DAI_LOG_WARNING(kModule, "Unknown antialiasing '{}', using taa: use off or taa", name);
    }
    return renderer::Antialiasing::Taa;
}

/// The tone mapper named by renderer.tonemap; any other name gives AgX, with a warning once.
renderer::Upscaler UpscalerNamed(const core::CVar<std::string>& cvar) {
    const std::string& name = cvar.Get();
    if (name == "fsr") {
        return renderer::Upscaler::Fsr3;
    }
    static std::string reported;
    if (name != "engine" && reported != name) {
        reported = name;
        DAI_LOG_WARNING(kModule, "Unknown upscaler '{}', using engine: use engine or fsr", name);
    }
    return renderer::Upscaler::Engine;
}

renderer::ToneMapper ToneMapperNamed(const core::CVar<std::string>& cvar) {
    const std::string& name = cvar.Get();
    if (name == "neutral") {
        return renderer::ToneMapper::Neutral;
    }
    static std::string reported;
    if (name != "agx" && reported != name) {
        reported = name;
        DAI_LOG_WARNING(kModule, "Unknown tone mapper '{}', using agx: use agx or neutral", name);
    }
    return renderer::ToneMapper::AgX;
}

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
        // A new level: the first launch's measurement starts over on it.
        m_sceneFrames = 0;
        m_probeMilliseconds = 0.0;
        m_probeFrames = 0;
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
    logging::LogConfig log = m_config.log;
#if DAI_DEV_TOOLS
    m_consoleLog.Clear();
    log.sinks.push_back(&m_consoleLog);
#endif
    DAI_TRY(Starting("logging", logging::Initialize(log)));
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
    m_devTools.reset();
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
        m_fullscreen = m_cvars.Register<bool>("window.fullscreen", m_config.window.fullscreen,
                                              "Cover the display without borders, at its own resolution");
        m_vsync = m_cvars.Register<bool>("renderer.vsync", true, "Synchronize presentation with the display");
        m_exposure = m_cvars.Register<double>("renderer.exposure_compensation", 0.0,
                                              "Stops added to the scene's exposure: +1 is twice as bright");
        m_autoExposure = m_cvars.Register<bool>(
            "renderer.auto_exposure", true,
            "The eye adapts to the light within the level's range; off, it stays at the level's exposure");
        m_bloom = m_cvars.Register<double>("renderer.bloom", 0.04,
                                           "Share of the image that is its bloom, from 0 (none) to 1");
        m_sharpness = m_cvars.Register<double>("renderer.sharpness", 0.25,
                                               "Sharpening of the antialiased image, from 0 (none) to 1");
        m_renderScale = m_cvars.Register<double>(
            "renderer.render_scale", 1.0,
            "Render size over the window's, from 0.5 to 1, upscaled by temporal antialiasing; with dynamic "
            "resolution, the most it goes up to");
        m_dynamicResolution = m_cvars.Register<bool>(
            "renderer.dynamic_resolution", true,
            "Lower the render size when the GPU cannot hold the target frame rate, and raise it back after");
        m_minRenderScale = m_cvars.Register<double>("renderer.min_render_scale", 0.5,
                                                    "Lowest render size over the window's with dynamic resolution");
        m_targetFps = m_cvars.Register<std::int64_t>(
            "renderer.target_fps", 60, "Frames per second dynamic resolution holds on the GPU, from 30 to 240");
        m_upscaler = m_cvars.Register<std::string>(
            "renderer.upscaler", "engine",
            "What upscales the render resolution with temporal antialiasing: engine, or fsr (AMD FSR 3.1)");
        m_toneMapper = m_cvars.Register<std::string>(
            "renderer.tonemap", "agx", "Tone mapping: agx, or neutral (Khronos PBR Neutral) for comparison");
        m_hdr = m_cvars.Register<bool>("renderer.hdr", true, "HDR10 output on a display with HDR turned on");
        m_hdrPaperWhite = m_cvars.Register<double>("renderer.hdr_paper_white", 0.0,
                                                   "Nits of the interface's white in HDR10; 0 takes the system's");
        m_hdrPeak = m_cvars.Register<double>("renderer.hdr_peak", 0.0,
                                             "Nits of the brightest light in HDR10; 0 takes the display's");
        m_anisotropy = m_cvars.Register<std::int64_t>(
            "renderer.anisotropy", 16, "Anisotropic filtering of textures, from 1 (off) to 16, read at startup");
        m_indirectLight = m_cvars.Register<bool>("renderer.indirect_light", true,
                                                 "Light that bounces off surfaces; off leaves direct light only");
        m_denoiser = m_cvars.Register<bool>(
            "renderer.denoiser", true, "Filter the noise of soft shadows and indirect light; off shows it as traced");
        m_reflections = m_cvars.Register<bool>("renderer.reflections", true,
                                               "Ray-traced reflections; off, the indirect light stands in for them");
        m_reflectionRoughness = m_cvars.Register<double>(
            "renderer.reflection_roughness", 0.6,
            "Surfaces rougher than this, from 0 to 1, trace no reflection: the indirect light stands in for it");
        m_volumetricFog = m_cvars.Register<bool>("renderer.volumetric_fog", true,
                                                 "Fog lit by the lights, with light shafts; off, the air is clear");
        m_fogSlices = m_cvars.Register<std::int64_t>(
            "renderer.fog_slices", 64, "Slices of the volumetric fog in depth, from 16 to 256: finer, for their cost");
        m_fogTile = m_cvars.Register<std::int64_t>(
            "renderer.fog_tile", 16, "Pixels across a cell of the volumetric fog, from 8 to 32: finer, for their cost");
        m_particleDensity = m_cvars.Register<double>(
            "renderer.particle_density", 1.0,
            "Multiplies the particles that emitters and weather emit, from 0 to 1: fewer, for their cost");
        m_quality = m_cvars.Register<std::string>(
            "renderer.quality", "auto",
            "Quality preset: low, medium, high or ultra; auto measures the GPU at the first launch and chooses. "
            "The settings it decides follow it unless set themselves");
        m_restirSpatial =
            m_cvars.Register<bool>("renderer.restir_spatial", true,
                                   "Pixels reuse the lights their neighbours chose; off, only their own past");
        m_restirUnbiased = m_cvars.Register<bool>(
            "renderer.restir_unbiased", false,
            "Test every reused light for shadows, to compare with the path tracer: twice the rays");
        m_giBounces = m_cvars.Register<std::int64_t>(
            "renderer.gi_bounces", 1,
            "Surfaces lit along each ray of indirect light before the radiance cache is read, from 0 to 3");
        m_giLightCandidates =
            m_cvars.Register<std::int64_t>("renderer.gi_light_candidates", 4,
                                           "Lights weighed at each surface indirect light bounces off, from 1 to 8");
        m_giClamp = m_cvars.Register<double>(
            "renderer.gi_clamp", 16.0,
            "Brightest bounced light, as on screen before tone mapping, against fireflies; 0 for no limit");
        m_giRays = m_cvars.Register<std::int64_t>(
            "renderer.gi_rays", 1, "Rays of indirect light per pixel, from 1 to 16: less noise, for their cost");
        m_normalStrength = m_cvars.Register<double>(
            "renderer.normal_strength", 1.0, "Multiplies the relief of normal maps: 0 flattens them, 1 as authored");
        m_fieldOfView = m_cvars.Register<double>(
            "view.fov", 90.0, "Degrees across the view, from 30 to 150, for cameras without their own");
        m_viewDistance =
            m_cvars.Register<double>("view.distance", 150.0, "Meters to the far plane: farther, nothing is drawn");
        m_antialiasing =
            m_cvars.Register<std::string>("renderer.antialiasing", "taa", "Smoothing of edges: off or taa");
        m_validation = m_cvars.Register<bool>("renderer.validation", kValidationByDefault,
                                              "Enable the Vulkan validation layers, read at startup");
        m_debugView = m_cvars.Register<std::string>(
            "renderer.debug_view", "off",
            "Shows a debug view instead of the lit scene: off, rays, path, motion, normals, indirect, "
            "radiance_cache, reflections, lights, bloom or fog");
        m_pathBounces = m_cvars.Register<std::int64_t>(
            "renderer.path_bounces", 4, "Surfaces a path of the path tracer meets at most; 1 is direct light only");
        m_pathSamples = m_cvars.Register<std::int64_t>(
            "renderer.path_samples", 4096, "Paths per pixel after which the path tracer's image stops changing");
        m_syncValidation = m_cvars.Register<bool>(
            "renderer.sync_validation", false,
            "With validation, also check the synchronization between passes (slow), read at startup");
        m_volume = m_cvars.Register<double>("audio.volume", 1.0, "Overall volume, from 0 to 1");
        m_effectsVolume = m_cvars.Register<double>("audio.effects", 1.0, "Volume of sound effects, from 0 to 1");
        m_musicVolume = m_cvars.Register<double>("audio.music", 1.0, "Volume of music, from 0 to 1");
        m_mouseSensitivity =
            m_cvars.Register<double>("input.mouse_sensitivity", 0.0025, "Radians of turn per mouse unit");
    }

    for (const std::filesystem::path& configFile : m_config.configFiles) {
        const std::filesystem::path path =
            configFile.is_relative() ? platform::ExecutableDirectory() / configFile : configFile;
        if (core::Result<void> loaded = m_cvars.LoadConfigFile(path); loaded) {
            DAI_LOG_INFO(kModule, "Configuration loaded from '{}'", path.generic_string());
        } else if (loaded.error().code == core::ErrorCode::NotFound) {
            DAI_LOG_INFO(kModule, "No configuration file '{}', using defaults", path.generic_string());
        } else {
            return loaded;
        }
    }
    DAI_TRY(LoadUserSettings());
    return m_cvars.ApplyCommandLine(m_config.arguments);
}

std::filesystem::path Application::UserConfigPath() const {
    const std::filesystem::path& file = m_config.userConfigFile;
    return file.is_relative() ? platform::ExecutableDirectory() / file : file;
}

core::Result<void> Application::LoadUserSettings() {
    m_userSettings.clear();
    if (m_config.userConfigFile.empty()) {
        return {};
    }
    const std::filesystem::path path = UserConfigPath();
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) {
        return {};
    }
    std::ifstream file(path, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (!file.is_open() || file.bad()) {
        return core::MakeError(core::ErrorCode::IoError, kModule,
                               std::format("Cannot read '{}'", path.generic_string()));
    }
    DAI_TRY_ASSIGN(const std::vector<core::ConfigLine> lines, core::ParseConfigLines(text, path.generic_string()));
    for (const core::ConfigLine& line : lines) {
        if (core::Result<void> set = m_cvars.Set(line.name, line.value); !set) {
            return core::MakeError(core::ErrorCode::ParseError, kModule,
                                   std::format("{}:{}: {}", path.generic_string(), line.number, set.error().message));
        }
        m_userSettings.insert_or_assign(line.name, line.value);
    }
    DAI_LOG_INFO(kModule, "Player's settings loaded from '{}'", path.generic_string());
    return {};
}

core::Result<void> Application::ChangeSetting(std::string_view name, std::string_view value) {
    if (m_cvars.Find(name) == nullptr) {
        return core::MakeError(core::ErrorCode::NotFound, kModule, std::format("No setting '{}'", name));
    }
    DAI_TRY(m_cvars.Set(name, value));
    m_userSettings.insert_or_assign(std::string(name), std::string(value));
    return {};
}

core::Result<void> Application::SaveSettings() {
    if (m_config.userConfigFile.empty()) {
        return {};
    }
    std::string text = "# The player's settings, written by the game's options. They override daidalos.cfg and the\n"
                       "# game's own file; command-line arguments override them. Delete a line to go back to those.\n";
    for (const auto& [name, value] : m_userSettings) {
        text += core::FormatConfigLine(name, value);
        text += '\n';
    }

    // A new file replaces the old one only once it is complete.
    const std::filesystem::path path = UserConfigPath();
    std::filesystem::path temporary = path;
    temporary += ".new";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!file.flush()) {
            return core::MakeError(core::ErrorCode::IoError, kModule,
                                   std::format("Cannot write '{}'", temporary.generic_string()));
        }
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return core::MakeError(core::ErrorCode::IoError, kModule,
                               std::format("Cannot replace '{}': {}", path.generic_string(), error.message()));
    }
    DAI_LOG_INFO(kModule, "Player's settings saved to '{}'", path.generic_string());
    return {};
}

float Application::MouseSensitivity() const {
    return m_mouseSensitivity ? static_cast<float>(m_mouseSensitivity->Get()) : 0.0025f;
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
    config.fullscreen = m_fullscreen->Get();
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
    const QualitySettings quality = EffectiveQualitySettings();
    DAI_TRY_ASSIGN(
        renderer::Renderer created,
        renderer::Renderer::Create(
            *m_window,
            {.validation = m_validation->Get(),
             .synchronizationValidation = m_syncValidation->Get(),
             .vsync = m_vsync->Get(),
             .exposureCompensation = static_cast<float>(m_exposure->Get()),
             .image = Image(),
             .resolution = Resolution(),
             .anisotropy = static_cast<float>(std::clamp<std::int64_t>(m_anisotropy->Get(), 1, 16)),
             .pipelineCacheFile = PipelineCachePath(),
             .normalStrength = static_cast<float>(m_normalStrength->Get()),
             .indirectLight = quality.indirectLight,
             .denoiser = m_denoiser->Get(),
             .reflections = quality.reflections,
             .reflectionRoughness = ReflectionRoughness(quality.reflectionRoughness),
             .volumetricFog = quality.volumetricFog,
             .fogSlices = FogSlices(quality.fogSlices),
             .fogTile = FogTile(quality.fogTile),
             .restirSpatial = quality.restirSpatial,
             .restirUnbiased = m_restirUnbiased->Get(),
             .indirect = IndirectLight(),
             .fieldOfView = FieldOfViewRadians(*m_fieldOfView),
             .viewDistance = ViewDistanceMeters(*m_viewDistance),
             .antialiasing = AntialiasingMode(*m_antialiasing),
             .debugView = DebugViewMode(*m_debugView),
             .pathBounces = static_cast<std::uint32_t>(std::clamp<std::int64_t>(m_pathBounces->Get(), 1, 64)),
             .pathSamples = static_cast<std::uint32_t>(std::clamp<std::int64_t>(m_pathSamples->Get(), 1, 1 << 20))}));
    m_renderer.emplace(std::move(created));
    return {};
}

renderer::ImageConfig Application::Image() const {
    return {.autoExposure = m_autoExposure->Get(),
            .bloom = static_cast<float>(std::clamp(m_bloom->Get(), 0.0, 1.0)),
            .toneMapper = ToneMapperNamed(*m_toneMapper),
            .hdr = m_hdr->Get(),
            .hdrPaperWhite = static_cast<float>(std::max(m_hdrPaperWhite->Get(), 0.0)),
            .hdrPeak = static_cast<float>(std::max(m_hdrPeak->Get(), 0.0)),
            .sharpness = static_cast<float>(std::clamp(m_sharpness->Get(), 0.0, 1.0))};
}

renderer::ResolutionConfig Application::Resolution() const {
    const auto scale = [](double value) { return static_cast<float>(std::clamp(value, 0.5, 1.0)); };
    const QualitySettings quality = EffectiveQualitySettings();
    // The first launch measures High at its full scale, which dynamic resolution would hide.
    const bool probing = ChoosingQuality();
    return {.scale = scale(probing ? SettingsOf(Quality::High).renderScale : quality.renderScale),
            .dynamic = !probing && m_dynamicResolution->Get(),
            .minScale = scale(quality.minRenderScale),
            .targetFps = static_cast<float>(std::clamp<std::int64_t>(m_targetFps->Get(), 30, 240)),
            .upscaler = UpscalerNamed(*m_upscaler)};
}

renderer::IndirectLightConfig Application::IndirectLight() const {
    const auto count = [](const core::CVar<std::int64_t>& value, std::int64_t low, std::int64_t high) {
        return static_cast<std::uint32_t>(std::clamp(value.Get(), low, high));
    };
    const QualitySettings quality = EffectiveQualitySettings();
    return {.bounces = static_cast<std::uint32_t>(std::clamp<std::int64_t>(quality.giBounces, 0, 3)),
            .lightCandidates = count(*m_giLightCandidates, 1, 8),
            .clamp = static_cast<float>(std::max(m_giClamp->Get(), 0.0)),
            .rays = static_cast<std::uint32_t>(std::clamp<std::int64_t>(quality.giRays, 1, 16))};
}

Quality Application::CurrentQuality() const {
    return QualityNamed(m_quality->Get()).value_or(Quality::High);
}

bool Application::ChoosingQuality() const {
    return m_renderer.has_value() && !QualityNamed(m_quality->Get()).has_value();
}

bool Application::QualityIsCustom() const {
    return std::ranges::any_of(kPresetSettings, [this](std::string_view name) {
        const core::CVarEntry* entry = m_cvars.Find(name);
        return entry != nullptr && entry->set;
    });
}

core::Result<void> Application::ChooseQuality(Quality quality) {
    DAI_TRY(ChangeSetting("renderer.quality", NameOf(quality)));
    for (const std::string_view name : kPresetSettings) {
        ResetSetting(name);
    }
    return {};
}

void Application::ResetSetting(std::string_view name) {
    m_cvars.Reset(name);
    if (const auto found = m_userSettings.find(name); found != m_userSettings.end()) {
        m_userSettings.erase(found);
    }
}

QualitySettings Application::EffectiveQualitySettings() const {
    const QualitySettings preset = SettingsOf(CurrentQuality());
    return {.renderScale = Preset(*m_renderScale, preset.renderScale),
            .minRenderScale = Preset(*m_minRenderScale, preset.minRenderScale),
            .indirectLight = Preset(*m_indirectLight, preset.indirectLight),
            .giBounces = Preset(*m_giBounces, preset.giBounces),
            .giRays = Preset(*m_giRays, preset.giRays),
            .reflections = Preset(*m_reflections, preset.reflections),
            .reflectionRoughness = Preset(*m_reflectionRoughness, preset.reflectionRoughness),
            .restirSpatial = Preset(*m_restirSpatial, preset.restirSpatial),
            .volumetricFog = Preset(*m_volumetricFog, preset.volumetricFog),
            .fogSlices = Preset(*m_fogSlices, preset.fogSlices),
            .fogTile = Preset(*m_fogTile, preset.fogTile),
            .particleDensity = Preset(*m_particleDensity, preset.particleDensity)};
}

std::filesystem::path Application::PipelineCachePath() const {
    if (m_config.pipelineCacheFile.empty()) {
        return {};
    }
    const std::filesystem::path& file = m_config.pipelineCacheFile;
    return file.is_relative() ? platform::ExecutableDirectory() / file : file;
}

void Application::AfterSceneFrame() {
    // Only frames that draw a level count: a loading screen measures nothing.
    if (!m_currentScene || !m_renderer.has_value() || !m_scene.camera.has_value() || m_scene.draws.empty()) {
        return;
    }
    ++m_sceneFrames;
    rhi::Device& device = m_renderer->Device();
    if (!device.PipelineCreationLocked()) {
        // Every pipeline is made by now: one made later stalls a frame (ADR-0052).
        device.LockPipelineCreation(true);
        DAI_LOG_INFO(kModule, "Pipelines created in {:.0f} ms, {}", device.PipelineCreationSeconds() * 1000.0,
                     device.Info().pipelineCacheLoaded ? "from the cache" : "without a cache");
    }
    if (!ChoosingQuality() || m_sceneFrames <= kProbeWarmupFrames) {
        return;
    }
    double milliseconds = 0.0;
    for (const renderer::PassTime& pass : m_renderer->GpuTimes()) {
        milliseconds += pass.milliseconds;
    }
    if (milliseconds <= 0.0) {
        return;
    }
    m_probeMilliseconds += milliseconds;
    if (++m_probeFrames < kProbeFrames) {
        return;
    }
    const double high = m_probeMilliseconds / static_cast<double>(m_probeFrames);
    const double fps = static_cast<double>(std::clamp<std::int64_t>(m_targetFps->Get(), 30, 240));
    const Quality chosen = app::ChooseQuality(high, fps);
    DAI_LOG_INFO(kModule, "Quality preset {}: a frame at High took {:.2f} ms on the GPU, for {:.0f} fps",
                 NameOf(chosen), high, fps);
    if (core::Result<void> changed = ChooseQuality(chosen).and_then([this] { return SaveSettings(); }); !changed) {
        DAI_LOG_WARNING(kModule, "The quality preset was not saved: {}", core::Format(changed.error()));
    }
}

core::Result<void> Application::CreateDebugOverlay() {
#if DAI_DEV_TOOLS
    if (m_renderer.has_value()) {
        DAI_TRY_ASSIGN(debugui::DebugUi created,
                       debugui::DebugUi::Create(m_renderer->Device(), m_renderer->ColorFormat()));
        m_debugUi.emplace(std::move(created));
        m_devTools.emplace(*this);
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
    m_scene.ribbons.clear();
    m_scene.timeStep = 0.0f;
    if (m_currentScene) {
        const QualitySettings emission = EffectiveQualitySettings();
        m_currentScene->SetEmission(static_cast<float>(std::clamp(emission.particleDensity, 0.0, 1.0)),
                                    FieldOfViewRadians(*m_fieldOfView));
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

    m_window->SetFullscreen(m_fullscreen->Get());
    m_renderer->SetVsync(m_vsync->Get());
    m_renderer->SetExposureCompensation(static_cast<float>(m_exposure->Get()));
    m_renderer->SetImage(Image());
    m_renderer->SetResolution(Resolution());
    m_renderer->SetNormalStrength(static_cast<float>(m_normalStrength->Get()));
    const QualitySettings quality = EffectiveQualitySettings();
    m_renderer->SetIndirectLight(quality.indirectLight);
    m_renderer->SetDenoiser(m_denoiser->Get());
    m_renderer->SetReflections(quality.reflections);
    m_renderer->SetReflectionRoughness(ReflectionRoughness(quality.reflectionRoughness));
    m_renderer->SetVolumetricFog(quality.volumetricFog, FogSlices(quality.fogSlices), FogTile(quality.fogTile));
    m_renderer->SetRestir(quality.restirSpatial, m_restirUnbiased->Get());
    m_renderer->SetIndirectLightConfig(IndirectLight());
    m_renderer->SetFieldOfView(FieldOfViewRadians(*m_fieldOfView));
    m_renderer->SetViewDistance(ViewDistanceMeters(*m_viewDistance));
    m_renderer->SetAntialiasing(AntialiasingMode(*m_antialiasing));
    m_renderer->SetDebugView(DebugViewMode(*m_debugView));
    m_renderer->SetPathTracer(static_cast<std::uint32_t>(std::clamp<std::int64_t>(m_pathBounces->Get(), 1, 64)),
                              static_cast<std::uint32_t>(std::clamp<std::int64_t>(m_pathSamples->Get(), 1, 1 << 20)));
    core::Result<void> rendered = m_renderer->Render(m_scene, &m_canvas, overlay);
    if (rendered && !overlayResult) {
        rendered = std::move(overlayResult);
    }
    if (!rendered) {
        DAI_LOG_ERROR(kModule, "Rendering failed: {}", core::Format(rendered.error()));
        m_failure = std::move(rendered.error());
        RequestQuit();
        return;
    }
    AfterSceneFrame();
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
#if DAI_DEV_TOOLS
                if (const auto* key = std::get_if<platform::KeyPressed>(&event);
                    key != nullptr && key->key == platform::Key::Grave && !key->repeat && m_devTools.has_value()) {
                    SetToolsOpen(!m_devTools->IsOpen());
                    continue;
                }
                // The tools take the keyboard and the mouse while they are open.
                const bool forTools = m_devTools.has_value() && m_devTools->IsOpen() &&
                                      (std::holds_alternative<platform::KeyPressed>(event) ||
                                       std::holds_alternative<platform::KeyReleased>(event) ||
                                       std::holds_alternative<platform::TextEntered>(event) ||
                                       std::holds_alternative<platform::MouseMoved>(event) ||
                                       std::holds_alternative<platform::MouseButtonPressed>(event) ||
                                       std::holds_alternative<platform::MouseButtonReleased>(event) ||
                                       std::holds_alternative<platform::MouseWheelScrolled>(event));
                if (!forTools) {
                    m_input.Process(event);
                }
#else
                m_input.Process(event);
#endif
                HandleEvent(handler, event);
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
        if (m_audio.has_value()) {
            m_audio->SetVolumes(static_cast<float>(m_volume->Get()), static_cast<float>(m_effectsVolume->Get()),
                                static_cast<float>(m_musicVolume->Get()));
        }

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
        if (m_renderer.has_value()) {
            const platform::Extent pixels = m_window->PixelSize();
            m_canvas.Begin({pixels.width, pixels.height});
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
#if DAI_DEV_TOOLS
        if (m_devTools.has_value()) {
            DAI_PROFILE_SCOPE("DevTools");
            m_devTools->Draw(time);
        }
#endif
        if (m_renderer.has_value()) {
            DAI_PROFILE_SCOPE("Render");
            RenderFrame(handler);
        }
        DAI_PROFILE_FRAME();
    }
    m_looping = false;
}

#if DAI_DEV_TOOLS
void Application::SetToolsOpen(bool open) {
    if (!m_devTools.has_value() || m_devTools->IsOpen() == open) {
        return;
    }
    m_devTools->SetOpen(open);
    if (open) {
        m_mouseCapturedBeforeTools = m_window->IsMouseCaptured();
        m_window->SetMouseCaptured(false);
        // Keys held now would never be released for the game.
        m_input.Process(platform::WindowFocusLost{m_window->Id()});
    } else if (m_mouseCapturedBeforeTools) {
        m_window->SetMouseCaptured(true);
    }
    m_window->SetTextInput(open);
}
#endif

void Application::HandleEvent(IApplicationHandler& handler, const platform::Event& event) {
    if (std::holds_alternative<platform::QuitRequested>(event)) {
        DAI_LOG_INFO(kModule, "Quit requested by the system");
        if (handler.OnCloseRequested(*this)) {
            RequestQuit();
        }
    } else if (const auto* close = std::get_if<platform::WindowCloseRequested>(&event);
               close != nullptr && close->window == m_window->Id()) {
        DAI_LOG_INFO(kModule, "Main window closed");
        if (handler.OnCloseRequested(*this)) {
            RequestQuit();
        }
    }
}

} // namespace daidalos::app
