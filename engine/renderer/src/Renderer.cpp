// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/core/Assert.hpp>
#include <daidalos/core/Profile.hpp>
#include <daidalos/logging/Log.hpp>
#include <daidalos/renderer/DisplayTransform.hpp>
#include <daidalos/renderer/LightUnits.hpp>
#include <daidalos/renderer/Renderer.hpp>
#include <daidalos/rhi/Spirv.hpp>
#include <daidalos/rhi/Swapchain.hpp>

#include <glm/matrix.hpp>
#include <glm/vec4.hpp>
#include <shaders/GBufferDebug.ps.spv.h>
#include <shaders/GBufferDebug.vs.spv.h>
#include <shaders/Output.ps.spv.h>
#include <shaders/Output.vs.spv.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iterator>
#include <limits>
#include <optional>
#include <utility>

#include "Bloom.hpp"
#include "CameraEffects.hpp"
#include "Denoiser.hpp"
#include "DepthProbe.hpp"
#include "DirectLighting.hpp"
#include "DynamicResolution.hpp"
#include "Exposure.hpp"
#include "FrameCapture.hpp"
#include "Fsr3Upscaler.hpp"
#include "GlobalIllumination.hpp"
#include "MeshPass.hpp"
#include "ParticlePass.hpp"
#include "PathTracerPass.hpp"
#include "RayTracingScene.hpp"
#include "RenderGraph.hpp"
#include "ShaderCompiler.hpp"
#include "TemporalResolvePass.hpp"
#include "UiPass.hpp"
#include "VolumetricFog.hpp"

namespace daidalos::renderer {

namespace {

constexpr std::string_view kModule = "renderer";
constexpr rhi::Format kDepthFormat = rhi::Format::D32Float;
/// Lighting is computed in HDR, then tonemapped into the swapchain (ADR-0022).
constexpr rhi::Format kHdrFormat = rhi::Format::Rgba16Float;
/// The interface and the developer tools draw into a layer of their own, which the output pass puts over the
/// image whatever the display's format (ADR-0048).
constexpr rhi::Format kInterfaceFormat = rhi::Format::Rgba8Srgb;
// Matches OutputPushConstants in shaders/Output.slang.
struct OutputPushConstants {
    std::uint32_t scene;
    std::uint32_t bloom;
    std::uint32_t layer;
    std::uint32_t toneMapper;
    float scale;
    float bloomShare;
    float headroom;
    float paperWhite;
    std::uint32_t hdr10;
    float sharpness;
    /// Where the scene starts in the window (SceneView::viewport); black around it.
    std::int32_t sceneX;
    std::int32_t sceneY;
    /// The lens (ADR-0063).
    float grain;
    float vignette;
    float aberration;
    float lensDirt;
    std::uint32_t frame;
    std::uint32_t colorVision;
    std::array<std::uint32_t, 2> padding;
};
constexpr std::uint32_t kNoTexture = 0xFFFFFFFF;
/// Stops between a level's darkest and brightest exposure beyond which the GPU's fixed exposure loses
/// precision (ADR-0048).
constexpr float kWidestExposureRange = 8.0f;

// Matches GBufferDebugPushConstants in shaders/GBufferDebug.slang.
struct GBufferDebugPushConstants {
    std::uint32_t texture;
    std::uint32_t mode;
    glm::vec2 extent;
};
constexpr std::uint32_t kShowMotion = 0;
constexpr std::uint32_t kShowNormals = 1;
constexpr std::uint32_t kShowLayers = 2;
constexpr std::uint32_t kShowBaseColor = 3;

/// The tints of the lit scene a debug view asks for: decals (1), wetness (2), grey surfaces (4). Match
/// Decals.slangh, Wetness.slangh and MeshTypes.slangh.
std::uint32_t TintFlags(DebugView view) {
    switch (view) {
    case DebugView::Decals:
        return 1U;
    case DebugView::Wetness:
        return 2U;
    case DebugView::LightingOnly:
        return 4U;
    default:
        return 0U;
    }
}

} // namespace

struct Renderer::Impl {
    Impl(platform::Window& targetWindow, rhi::Device createdDevice, rhi::Swapchain createdSwapchain, bool useVsync)
        : window(&targetWindow), device(std::move(createdDevice)), swapchain(std::move(createdSwapchain)),
          vsync(useVsync) {}

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    ~Impl() {
        if (const core::Result<void> idle = device.WaitIdle(); !idle) {
            DAI_LOG_ERROR(kModule, "Shutting down with a failed device: {}", core::Format(idle.error()));
        }
        pathTracer.reset();
        volumetricFog.reset();
        exposureMeter.reset();
        depthProbe.reset();
        capture.reset();
        bloom.reset();
        cameraEffects.reset();
        temporalResolve.reset();
        fsr3.reset();
        denoiser.reset();
        directLighting.reset();
        if (meshPass.has_value()) {
            meshPass->DestroyShadingTargets(shading);
        }
        rayTracing.reset();
        meshPass.reset();
        globalIllumination.reset();
        particlePass.reset();
        uiPass.reset();
        for (const auto& [format, pipeline] : outputPipelines) {
            device.DestroyPipeline(pipeline);
        }
        device.DestroyPipeline(gbufferDebug);
        device.DestroyTexture(motion);
        device.DestroyTexture(reactive);
        device.DestroyTexture(distortion);
        device.DestroyTexture(depth);
        device.DestroyTexture(normals);
        device.DestroyTexture(hdr);
        device.DestroyTexture(background);
        device.DestroyTexture(interfaceLayer);
    }

    /// The output pass's pipeline for a swapchain format. Every format a swapchain can take is created at
    /// startup, so that turning HDR on or off, or moving the window to another display, creates none in
    /// play (ADR-0052); a format seen only later is created then.
    core::Result<rhi::PipelineHandle> OutputPipeline(rhi::Format format) {
        for (const auto& [known, pipeline] : outputPipelines) {
            if (known == format) {
                return pipeline;
            }
        }
        DAI_TRY_ASSIGN(const rhi::PipelineHandle pipeline, device.CreatePipeline({
                                                               .vertexShader = rhi::ToSpirv(kOutputVsSpirv),
                                                               .fragmentShader = rhi::ToSpirv(kOutputPsSpirv),
                                                               .colorFormat = format,
                                                               .cull = rhi::CullMode::None,
                                                               .name = "Output",
                                                           }));
        outputPipelines.emplace_back(format, pipeline);
        return pipeline;
    }

    /// The targets follow the size of the back buffer.
    core::Result<void> ResizeTargets(rhi::Extent extent);
    /// The interface's layer fills the window, whatever part of it the scene fills (ADR-0055).
    core::Result<void> ResizeInterface(rhi::Extent extent);
    /// Reads the timestamps of the last finished frame recorded in this slot into gpuTimes, and its whole
    /// GPU time into frameGpuTime.
    void ReadGpuTimes(std::uint32_t slot);
    void ReloadShaders();
    void SetIndirect(const IndirectLightConfig& config) {
        indirect = {.bounces = std::min(config.bounces, 3U),
                    .lightCandidates = std::clamp(config.lightCandidates, 1U, 8U),
                    .clamp = std::max(config.clamp, 0.0f),
                    .rays = std::clamp(config.rays, 1U, 16U),
                    .newRays = std::clamp(config.newRays, 1U, 16U),
                    .pixelsPerRay = config.pixelsPerRay >= 4 ? 4U : (config.pixelsPerRay >= 2 ? 2U : 1U),
                    .reflectionPixelsPerRay = config.reflectionPixelsPerRay >= 2 ? 2U : 1U};
    }

    platform::Window* window;
    rhi::Device device;
    rhi::Swapchain swapchain;
    std::optional<MeshPass> meshPass;
    std::optional<GlobalIllumination> globalIllumination;
    std::optional<ParticlePass> particlePass;
    std::optional<UiPass> uiPass;
    std::optional<TemporalResolvePass> temporalResolve;
    /// Created with the renderer; its targets only once it upscales a frame.
    std::optional<Fsr3Upscaler> fsr3;
    /// The upscaler of the last frame resolved.
    Upscaler upscaledBy = Upscaler::Engine;
    std::optional<ExposureMeter> exposureMeter;
    std::optional<DepthProbe> depthProbe;
    bool probeDepth = false;
    std::optional<FrameCapture> capture;
    std::optional<Bloom> bloom;
    std::optional<CameraEffects> cameraEffects;
    /// The eye's exposure, the last metering and histogram, and the range of the last frame (ADR-0048).
    EyeAdaptation eye;
    float meteredEv100 = std::numeric_limits<float>::quiet_NaN();
    std::vector<std::uint32_t> histogram;
    float ev100Min = 0.0f;
    float ev100Max = 0.0f;
    bool rangeWarned = false;
    std::optional<Denoiser> denoiser;
    std::optional<DirectLighting> directLighting;
    /// Volumetric fog (ADR-0051), its froxel grid, and whether the last frame had it: its history follows.
    std::optional<VolumetricFog> volumetricFog;
    bool fog = true;
    bool tracedParticles = true;
    // A recording's time per frame (ADR-0123), or none for the clock's.
    std::optional<float> fixedFrameTime;
    FroxelGrid fogGrid;
    bool fogged = false;
    /// Seconds the views drawn have moved on by, which the noise of fog volumes follows.
    double time = 0.0;
    DirectLightingSettings restir;
    GlobalIlluminationSettings indirect;
    /// The last frame was lit: its reservoirs can be reused.
    bool lit = false;
    /// Counts the frames, for the jitter of temporal antialiasing.
    std::uint64_t frameNumber = 0;
    /// Counts every frame drawn, for the noise of soft shadows.
    std::uint32_t frameIndex = 0;
    /// The last frame went through the denoiser: its history can be reprojected.
    bool denoised = false;
    bool denoise = true;
    /// Lights are drawn by the light grid (ADR-0124).
    bool lightGrid = true;
    /// The last frame went through the temporal resolve: its history can be reprojected.
    bool resolved = false;
    /// Bursts and emissions of the last view drawn, for FrameStats.
    std::uint32_t particleSpawns = 0;
    bool indirectLight = true;
    bool reflections = true;
    float reflectionRoughness = 0.6f;
    /// The last frame went through global illumination: its cache still follows the scene.
    bool illuminated = false;
    Antialiasing antialiasing = Antialiasing::Taa;
    DebugView debugView = DebugView::Off;
    std::uint32_t debugFlags = 0; // The lit scene's tints (MeshPass::FrameTargets::debugFlags).
    std::optional<RayTracingScene> rayTracing;
    std::optional<PathTracerPass> pathTracer;
    PathTracerSettings pathSettings;
    /// What the path tracer's average was made with: a change starts it over.
    glm::mat4 pathViewProjection{0.0f};
    PathTracerSettings pathAveraged;
    bool pathShown = false;
    std::vector<std::pair<rhi::Format, rhi::PipelineHandle>> outputPipelines;
    rhi::PipelineHandle gbufferDebug;
    ImageConfig image;
    /// The interface and the overlay, premultiplied, at the window's size.
    rhi::TextureHandle interfaceLayer;
    rhi::Extent interfaceExtent{};
    /// The G-buffer's material and what the lighting writes, which the composite puts together into hdr
    /// (ADR-0039, ADR-0044).
    ShadingTargets shading;
    rhi::TextureHandle hdr;
    /// A copy of hdr before glass, which the glass pass reads (ADR-0042).
    rhi::TextureHandle background;
    rhi::TextureHandle depth;
    /// World normals and roughness of the G-buffer.
    rhi::TextureHandle normals;
    /// Motion vectors of the G-buffer (ADR-0037).
    rhi::TextureHandle motion;
    /// The particles' coverage, where temporal antialiasing trusts its history less.
    rhi::TextureHandle reactive;
    /// How far distortion particles bend the frame (ADR-0053).
    rhi::TextureHandle distortion;
    rhi::Extent targetExtent;
    bool vsync;
    /// Stops added to the scene's exposure (ADR-0043).
    float exposureCompensation = 0.0f;
    RenderGraph graph;
    /// The timed groups of the frame recorded in each slot, in the order of their timestamps.
    std::array<std::vector<std::string_view>, rhi::kFramesInFlight> timedGroups;
    /// The groups the compute queue timed, beside the others (ADR-0095).
    std::array<std::vector<std::string_view>, rhi::kFramesInFlight> computeGroups;
    std::vector<PassTime> gpuTimes;
    std::optional<double> frameGpuTime;
    /// The render resolution (ADR-0049): the settings, the scale that holds the budget, and what the last
    /// frame was rendered at.
    ResolutionConfig resolution;
    DynamicResolution dynamicResolution{rhi::kFramesInFlight};
    std::optional<std::chrono::steady_clock::time_point> lastFrameTime;
    rhi::Extent renderExtent;
    float renderScale = 1.0f;
    std::string shaderError;
#if DAI_DEV_TOOLS
    std::optional<ShaderWatcher> shaderWatcher;
    std::chrono::steady_clock::time_point nextShaderPoll;
#endif
};

core::Result<void> Renderer::Impl::ResizeTargets(rhi::Extent extent) {
    if (depth && hdr && targetExtent == extent) {
        return {};
    }
    device.DestroyTexture(depth);
    device.DestroyTexture(normals);
    device.DestroyTexture(motion);
    device.DestroyTexture(reactive);
    device.DestroyTexture(distortion);
    device.DestroyTexture(hdr);
    device.DestroyTexture(background);
    meshPass->DestroyShadingTargets(shading);
    depth = {};
    normals = {};
    motion = {};
    reactive = {};
    distortion = {};
    hdr = {};
    background = {};
    DAI_TRY(temporalResolve->Resize(extent));
    DAI_TRY(bloom->Resize(extent));
    DAI_TRY(cameraEffects->Resize(extent));
    DAI_TRY(denoiser->Resize(extent));
    DAI_TRY(directLighting->Resize(extent));
    DAI_TRY_ASSIGN(shading, meshPass->CreateShadingTargets(extent));
    DAI_TRY_ASSIGN(reactive, device.CreateTexture({.extent = extent,
                                                   .format = ParticlePass::kReactiveFormat,
                                                   .usage = rhi::TextureUsage::ColorTarget | rhi::TextureUsage::Sampled,
                                                   .name = "Reactive"}));
    DAI_TRY_ASSIGN(distortion,
                   device.CreateTexture({.extent = extent,
                                         .format = ParticlePass::kDistortionFormat,
                                         .usage = rhi::TextureUsage::ColorTarget | rhi::TextureUsage::Sampled,
                                         .name = "Distortion"}));
    DAI_TRY_ASSIGN(motion, device.CreateTexture({.extent = extent,
                                                 .format = MeshPass::kMotionFormat,
                                                 .usage = rhi::TextureUsage::ColorTarget | rhi::TextureUsage::Sampled,
                                                 .name = "Motion"}));
    DAI_TRY_ASSIGN(normals, device.CreateTexture({.extent = extent,
                                                  .format = MeshPass::kNormalFormat,
                                                  .usage = rhi::TextureUsage::ColorTarget | rhi::TextureUsage::Sampled,
                                                  .name = "Normals"}));
    DAI_TRY(globalIllumination->Resize(extent));
    DAI_TRY_ASSIGN(depth, device.CreateTexture({.extent = extent,
                                                .format = kDepthFormat,
                                                .usage = rhi::TextureUsage::DepthTarget | rhi::TextureUsage::Sampled,
                                                .name = "Depth"}));
    DAI_TRY_ASSIGN(hdr, device.CreateTexture({.extent = extent,
                                              .format = kHdrFormat,
                                              .usage = rhi::TextureUsage::ColorTarget | rhi::TextureUsage::Sampled |
                                                       rhi::TextureUsage::TransferSrc,
                                              .name = "HDR color"}));
    DAI_TRY_ASSIGN(background,
                   device.CreateTexture({.extent = extent,
                                         .format = kHdrFormat,
                                         .usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDst,
                                         .name = "Behind glass"}));
    targetExtent = extent;
    return {};
}

core::Result<void> Renderer::Impl::ResizeInterface(rhi::Extent extent) {
    if (interfaceLayer && interfaceExtent == extent) {
        return {};
    }
    device.DestroyTexture(interfaceLayer);
    interfaceLayer = {};
    DAI_TRY_ASSIGN(interfaceLayer,
                   device.CreateTexture({.extent = extent,
                                         .format = kInterfaceFormat,
                                         .usage = rhi::TextureUsage::ColorTarget | rhi::TextureUsage::Sampled,
                                         .name = "Interface"}));
    interfaceExtent = extent;
    return {};
}

void Renderer::Impl::ReadGpuTimes(std::uint32_t slot) {
    const std::span<const double> timestamps = device.CompletedTimestamps();
    const std::vector<std::string_view>& groups = timedGroups[slot];
    gpuTimes.clear();
    frameGpuTime.reset();
    if (timestamps.size() != groups.size() + 1) {
        return;
    }
    frameGpuTime = timestamps.back() - timestamps.front();
    for (std::size_t group = 0; group < groups.size(); ++group) {
        gpuTimes.push_back({groups[group], timestamps[group + 1] - timestamps[group]});
    }
    const std::span<const double> beside = device.CompletedComputeTimestamps();
    const std::vector<std::string_view>& besideGroups = computeGroups[slot];
    if (beside.size() == besideGroups.size() + 1) {
        for (std::size_t group = 0; group < besideGroups.size(); ++group) {
            gpuTimes.push_back({besideGroups[group], beside[group + 1] - beside[group], true});
        }
    }
}

void Renderer::Impl::ReloadShaders() {
#if DAI_DEV_TOOLS
    constexpr std::chrono::milliseconds kPollInterval{500};
    const auto now = std::chrono::steady_clock::now();
    if (!shaderWatcher.has_value() || now < nextShaderPoll) {
        return;
    }
    nextShaderPoll = now + kPollInterval;
    if (!shaderWatcher->Poll()) {
        return;
    }

    DAI_PROFILE_SCOPE("renderer::ReloadShaders");
    // Reloading shaders is a developer's choice, not a hitch in play (ADR-0052).
    const bool locked = device.PipelineCreationLocked();
    device.LockPipelineCreation(false);
    const std::filesystem::path source = std::filesystem::path(DAI_SHADER_SOURCE_DIR) / "GBuffer.slang";
    core::Result<void> reloaded = CompileShader(DAI_SLANGC_PATH, source, ShaderStage::Vertex)
                                      .and_then([&](std::vector<std::uint32_t> vertexShader) {
                                          return CompileShader(DAI_SLANGC_PATH, source, ShaderStage::Pixel)
                                              .and_then([&](std::vector<std::uint32_t> fragmentShader) {
                                                  return meshPass->ReplaceShaders(vertexShader, fragmentShader);
                                              });
                                      });
    device.LockPipelineCreation(locked);
    if (reloaded) {
        shaderError.clear();
        DAI_LOG_INFO(kModule, "Shaders reloaded");
    } else {
        shaderError = reloaded.error().message;
        DAI_LOG_ERROR(kModule, "Shader reload failed, keeping the previous shaders:\n{}", shaderError);
    }
#endif
}

core::Result<Renderer> Renderer::Create(platform::Window& window, const RendererConfig& config) {
    DAI_TRY_ASSIGN(std::vector<std::string> extensions, window.VulkanInstanceExtensions());

    rhi::DeviceConfig deviceConfig;
    deviceConfig.validation = config.validation;
    deviceConfig.synchronizationValidation = config.synchronizationValidation;
    deviceConfig.instanceExtensions = std::move(extensions);
    deviceConfig.presentation = true;
    deviceConfig.anisotropy = config.anisotropy;
    deviceConfig.asyncCompute = config.asyncCompute;
    deviceConfig.pipelineStatistics = config.pipelineStatistics;
    deviceConfig.pipelineCacheFile = config.pipelineCacheFile;
    DAI_TRY_ASSIGN(rhi::Device device, rhi::Device::Create(deviceConfig));

    const platform::Extent size = window.PixelSize();
    DAI_TRY_ASSIGN(rhi::Swapchain swapchain,
                   rhi::Swapchain::Create(
                       device, [&window](std::uintptr_t instance) { return window.CreateVulkanSurface(instance); },
                       {{size.width, size.height}, config.vsync}));

    auto impl = std::make_unique<Impl>(window, std::move(device), std::move(swapchain), config.vsync);
    impl->exposureCompensation = config.exposureCompensation;
    impl->image = config.image;
    impl->resolution = config.resolution;
    for (const rhi::Format format : {impl->swapchain.ColorFormat(), rhi::Format::Bgra8Srgb, rhi::Format::Rgba8Srgb,
                                     rhi::Format::A2b10g10r10Unorm}) {
        DAI_TRY(impl->OutputPipeline(format));
    }
    DAI_TRY_ASSIGN(impl->gbufferDebug, impl->device.CreatePipeline({
                                           .vertexShader = rhi::ToSpirv(kGBufferDebugVsSpirv),
                                           .fragmentShader = rhi::ToSpirv(kGBufferDebugPsSpirv),
                                           .colorFormat = kHdrFormat,
                                           .cull = rhi::CullMode::None,
                                           .name = "G-buffer debug view",
                                       }));
    DAI_TRY_ASSIGN(UiPass uiPass, UiPass::Create(impl->device, kInterfaceFormat));
    impl->uiPass.emplace(std::move(uiPass));
    impl->antialiasing = config.antialiasing;
    DAI_TRY_ASSIGN(MeshPass meshPass, MeshPass::Create(impl->device, kHdrFormat, kDepthFormat));
    impl->meshPass.emplace(std::move(meshPass));
    DAI_TRY_ASSIGN(RayTracingScene rayTracing, RayTracingScene::Create(impl->device, kHdrFormat));
    impl->rayTracing.emplace(std::move(rayTracing));
    impl->debugFlags = TintFlags(config.debugView);
    impl->debugView = impl->debugFlags != 0 ? DebugView::Off : config.debugView;
    DAI_TRY_ASSIGN(PathTracerPass pathTracer, PathTracerPass::Create(impl->device));
    impl->pathTracer.emplace(std::move(pathTracer));
    impl->pathSettings = {
        .maxBounces = config.pathBounces, .maxSamples = config.pathSamples, .background = config.pathBackground};
    DAI_TRY_ASSIGN(GlobalIllumination globalIllumination, GlobalIllumination::Create(impl->device));
    impl->globalIllumination.emplace(std::move(globalIllumination));
    impl->indirectLight = config.indirectLight;
    DAI_TRY_ASSIGN(ParticlePass particlePass, ParticlePass::Create(impl->device, kHdrFormat, kDepthFormat));
    impl->particlePass.emplace(std::move(particlePass));
    DAI_TRY_ASSIGN(TemporalResolvePass temporalResolve, TemporalResolvePass::Create(impl->device));
    impl->temporalResolve.emplace(std::move(temporalResolve));
    DAI_TRY_ASSIGN(Fsr3Upscaler fsr3, Fsr3Upscaler::Create(impl->device));
    impl->fsr3.emplace(std::move(fsr3));
    DAI_TRY_ASSIGN(ExposureMeter exposureMeter, ExposureMeter::Create(impl->device));
    impl->exposureMeter.emplace(std::move(exposureMeter));
    DAI_TRY_ASSIGN(DepthProbe depthProbe, DepthProbe::Create(impl->device));
    impl->depthProbe.emplace(std::move(depthProbe));
    impl->capture.emplace(impl->device);
    DAI_TRY_ASSIGN(Bloom bloom, Bloom::Create(impl->device));
    impl->bloom.emplace(std::move(bloom));
    DAI_TRY_ASSIGN(CameraEffects cameraEffects, CameraEffects::Create(impl->device));
    impl->cameraEffects.emplace(std::move(cameraEffects));
    DAI_TRY_ASSIGN(Denoiser denoiser, Denoiser::Create(impl->device));
    impl->denoiser.emplace(std::move(denoiser));
    DAI_TRY_ASSIGN(DirectLighting directLighting, DirectLighting::Create(impl->device));
    impl->directLighting.emplace(std::move(directLighting));
    DAI_TRY_ASSIGN(VolumetricFog volumetricFog, VolumetricFog::Create(impl->device));
    impl->volumetricFog.emplace(std::move(volumetricFog));
    impl->fog = config.volumetricFog;
    impl->tracedParticles = config.tracedParticles;
    impl->fogGrid.slices = std::clamp(config.fogSlices, 16U, 256U);
    impl->fogGrid.tileSize = std::clamp(config.fogTile, 8U, 32U);
    impl->restir.spatial = config.restirSpatial;
    impl->restir.unbiased = config.restirUnbiased;
    impl->restir.candidates = config.restirCandidates;
    impl->SetIndirect(config.indirect);
    impl->denoise = config.denoiser;
    impl->lightGrid = config.lightGrid;
    impl->reflections = config.reflections;
    impl->reflectionRoughness = config.reflectionRoughness;
    impl->meshPass->SetNormalStrength(config.normalStrength);
    impl->meshPass->SetFieldOfView(config.fieldOfView);
    impl->meshPass->SetViewDistance(config.viewDistance);

#if DAI_DEV_TOOLS
    // Installed builds have no sources to watch: they keep the embedded shaders.
    if (std::filesystem::is_directory(DAI_SHADER_SOURCE_DIR) && std::filesystem::exists(DAI_SLANGC_PATH)) {
        impl->shaderWatcher.emplace(DAI_SHADER_SOURCE_DIR);
        DAI_LOG_INFO(kModule, "Watching shaders in {}", DAI_SHADER_SOURCE_DIR);
    }
#endif
    return Renderer(std::move(impl));
}

Renderer::Renderer(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}
Renderer::Renderer(Renderer&& other) noexcept = default;
Renderer& Renderer::operator=(Renderer&& other) noexcept = default;
Renderer::~Renderer() = default;

core::Result<MeshHandle> Renderer::CreateMesh(const resources::MeshData& data, const std::string& name) {
    return m_impl->meshPass->CreateMesh(data, name);
}

void Renderer::DestroyMesh(MeshHandle mesh) {
    m_impl->meshPass->DestroyMesh(mesh);
}

core::Result<MaterialHandle> Renderer::CreateMaterial(const MaterialDesc& desc) {
    return m_impl->meshPass->CreateMaterial(desc);
}

bool Renderer::UpdateMaterial(MaterialHandle material, const MaterialDesc& desc) {
    return m_impl->meshPass->UpdateMaterial(material, desc);
}

void Renderer::DestroyMaterial(MaterialHandle material) {
    m_impl->meshPass->DestroyMaterial(material);
}

core::Result<ParticleEffectHandle> Renderer::CreateParticleEffect(const ParticleEffectDesc& desc) {
    return m_impl->particlePass->CreateEffect(desc, *m_impl->meshPass);
}

bool Renderer::UpdateParticleEffect(ParticleEffectHandle effect, const ParticleEffectDesc& desc) {
    return m_impl->particlePass->UpdateEffect(effect, desc, *m_impl->meshPass);
}

void Renderer::DestroyParticleEffect(ParticleEffectHandle effect) {
    m_impl->particlePass->DestroyEffect(effect);
}

void Renderer::SetVsync(bool vsync) {
    m_impl->vsync = vsync;
}

void Renderer::SetExposureCompensation(float stops) {
    m_impl->exposureCompensation = stops;
}

void Renderer::SetImage(const ImageConfig& config) {
    m_impl->image = config;
}

void Renderer::SetResolution(const ResolutionConfig& config) {
    m_impl->resolution = config;
}

void Renderer::SetIndirectLight(bool enabled) {
    m_impl->indirectLight = enabled;
}

void Renderer::SetDenoiser(bool enabled) {
    m_impl->denoise = enabled;
}

void Renderer::SetLightGrid(bool enabled) {
    m_impl->lightGrid = enabled;
}

void Renderer::SetReflections(bool enabled) {
    m_impl->reflections = enabled;
}

void Renderer::SetReflectionRoughness(float roughness) {
    m_impl->reflectionRoughness = roughness;
}

void Renderer::SetIndirectLightConfig(const IndirectLightConfig& config) {
    m_impl->SetIndirect(config);
}

void Renderer::SetRestir(bool spatial, bool unbiased, std::uint32_t candidates) {
    m_impl->restir.spatial = spatial;
    m_impl->restir.unbiased = unbiased;
    m_impl->restir.candidates = candidates;
}

void Renderer::SetFieldOfView(float radians) {
    m_impl->meshPass->SetFieldOfView(radians);
}

void Renderer::SetViewDistance(float meters) {
    m_impl->meshPass->SetViewDistance(meters);
}

void Renderer::SetAntialiasing(Antialiasing antialiasing) {
    m_impl->antialiasing = antialiasing;
}

void Renderer::SetDebugView(DebugView view) {
    // The decals' and the wetness' views are the lit scene, tinted (ADR-0060, ADR-0061).
    m_impl->debugFlags = TintFlags(view);
    m_impl->debugView = m_impl->debugFlags != 0 ? DebugView::Off : view;
}

void Renderer::SetPathTracer(std::uint32_t bounces, std::uint32_t samples) {
    m_impl->pathSettings.maxBounces = bounces;
    m_impl->pathSettings.maxSamples = samples;
}

void Renderer::SetDepthProbe(bool enabled) {
    m_impl->probeDepth = enabled;
}

const std::optional<DepthPicture>& Renderer::LatestDepth() const {
    return m_impl->depthProbe->Latest();
}

void Renderer::RequestCapture(bool video) {
    m_impl->capture->Request(video);
}

std::optional<CapturedFrame> Renderer::TakeCapture() {
    return m_impl->capture->Take();
}

std::size_t Renderer::CapturesInFlight() const {
    return m_impl->capture->InFlight();
}

void Renderer::SetFixedFrameTime(std::optional<float> seconds) {
    m_impl->fixedFrameTime = seconds;
}

void Renderer::SetVolumetricFog(bool enabled, std::uint32_t slices, std::uint32_t tile) {
    m_impl->fog = enabled;
    m_impl->fogGrid.slices = std::clamp(slices, 16U, 256U);
    m_impl->fogGrid.tileSize = std::clamp(tile, 8U, 32U);
}

void Renderer::SetTracedParticles(bool enabled) {
    m_impl->tracedParticles = enabled;
}

void Renderer::SetNormalStrength(float strength) {
    m_impl->meshPass->SetNormalStrength(strength);
}

std::vector<ParticleHit> Renderer::ParticleHits() const {
    std::vector<ParticleHit> hits;
    for (const ParticlePass::Hit& hit : m_impl->particlePass->Hits()) {
        hits.push_back({.position = hit.position,
                        .normal = hit.normal,
                        .effect = hit.effect,
                        .material = m_impl->meshPass->MaterialAt(hit.material)});
    }
    return hits;
}

std::span<const PassTime> Renderer::GpuTimes() const {
    return m_impl->gpuTimes;
}

FrameStats Renderer::Stats() const {
    const Impl& impl = *m_impl;
    const MeshPass::Counts& counts = impl.meshPass->LastCounts();
    return {.draws = counts.draws,
            .drawnDraws = counts.drawnDraws,
            .triangles = counts.triangles,
            .lights = counts.lights,
            .particleSpawns = impl.particleSpawns,
            .particleHits = static_cast<std::uint32_t>(impl.particlePass->Hits().size()),
            .particlesAlive = impl.particlePass->AliveCount(),
            .rayInstances = impl.rayTracing->LastStats().instances,
            .skinnedStructures = impl.rayTracing->LastStats().skinnedStructures,
            .pathSamples = impl.debugView == DebugView::Path ? impl.pathTracer->Samples() : 0,
            .ev100 = impl.eye.Ev100(),
            .meteredEv100 = impl.meteredEv100,
            .ev100Min = impl.ev100Min,
            .ev100Max = impl.ev100Max,
            .renderWidth = impl.renderExtent.width,
            .renderHeight = impl.renderExtent.height,
            .renderScale = impl.renderScale,
            .meshes = impl.meshPass->MeshCount(),
            .materials = impl.meshPass->MaterialCount(),
            .particleEffects = impl.particlePass->EffectCount()};
}

std::span<const std::uint32_t> Renderer::ExposureHistogram() const {
    return m_impl->histogram;
}

core::Result<void> Renderer::Render(const SceneView& view, const ui::Canvas* canvas, const OverlayRecorder& overlay) {
    DAI_PROFILE_SCOPE("renderer::Render");
    Impl& impl = *m_impl;

    impl.ReloadShaders();

    const platform::Extent size = impl.window->PixelSize();
    // HDR10 where the display shows HDR (ADR-0048); its paper white and peak are the system's unless set.
    const platform::HdrInfo display = impl.window->Hdr();
    const bool hdrWanted = impl.image.hdr && display.enabled;
    const float paperWhite = impl.image.hdrPaperWhite > 0.0f ? impl.image.hdrPaperWhite : display.sdrWhiteNits;
    const float peak =
        std::max(impl.image.hdrPeak > 0.0f ? impl.image.hdrPeak : display.sdrWhiteNits * display.headroom, paperWhite);
    impl.swapchain.Configure({.extent = {size.width, size.height},
                              .vsync = impl.vsync,
                              .hdr = hdrWanted,
                              .maxLuminance = hdrWanted ? peak : 0.0f});

    DAI_TRY_ASSIGN(std::optional<rhi::Frame> begun, impl.device.BeginFrame(&impl.swapchain));
    if (!begun.has_value()) {
        return {}; // Nothing can be drawn this frame.
    }
    rhi::Frame& frame = *begun;
    impl.ReadGpuTimes(frame.slot);
    impl.capture->Read(frame.slot);
    impl.depthProbe->Read(frame.slot);

    // Exposure (ADR-0048). The GPU's light is exposed once for the level, at the middle of its range, so
    // that no history goes stale when the eye adapts: the eye and the brightness setting scale the image
    // in the output pass.
    const Atmosphere& atmosphere = view.atmosphere;
    const bool adapts = impl.image.autoExposure && atmosphere.autoExposure;
    impl.ev100Min = adapts ? std::min(atmosphere.ev100Min, atmosphere.ev100Max) : atmosphere.ev100;
    impl.ev100Max = adapts ? std::max(atmosphere.ev100Min, atmosphere.ev100Max) : atmosphere.ev100;
    if (impl.ev100Max - impl.ev100Min > kWidestExposureRange && !impl.rangeWarned) {
        impl.rangeWarned = true;
        DAI_LOG_WARNING(kModule, "The exposure range spans {:.1f} stops: beyond {:.0f}, dark light loses precision",
                        impl.ev100Max - impl.ev100Min, kWidestExposureRange);
    }
    std::optional<float> metered;
    if (const std::span<const std::uint32_t> histogram = impl.exposureMeter->Read(frame.slot); !histogram.empty()) {
        impl.histogram.assign(histogram.begin(), histogram.end());
        metered = MeterEv100(histogram);
        impl.meteredEv100 = metered.value_or(std::numeric_limits<float>::quiet_NaN());
    }
    if (!adapts) {
        impl.histogram.clear();
        impl.meteredEv100 = std::numeric_limits<float>::quiet_NaN();
    }
    if (!impl.eye.Started() || view.cut) {
        impl.eye.Reset(std::clamp(atmosphere.ev100, impl.ev100Min, impl.ev100Max));
    }
    impl.eye.Update(adapts ? metered : std::nullopt, view.timeStep, impl.ev100Min, impl.ev100Max);
    const float gpuEv100 = (impl.ev100Min + impl.ev100Max) * 0.5f;
    const float preExposure = ExposureScale(gpuEv100);
    const float displayScale = std::exp2(gpuEv100 - impl.eye.Ev100() + impl.exposureCompensation);

    // The scene fills the window, or the view's rectangle of it (ADR-0055); the interface always fills the
    // window.
    rhi::Extent sceneExtent = frame.extent;
    glm::ivec2 sceneOffset{0};
    if (view.viewport.has_value()) {
        const ViewRect& rect = *view.viewport;
        const std::uint32_t x = std::min(rect.x, frame.extent.width);
        const std::uint32_t y = std::min(rect.y, frame.extent.height);
        const rhi::Extent fitted{std::min(rect.width, frame.extent.width - x),
                                 std::min(rect.height, frame.extent.height - y)};
        if (fitted.width >= 8 && fitted.height >= 8) {
            sceneExtent = fitted;
            sceneOffset = {static_cast<int>(x), static_cast<int>(y)};
        }
    }
    // A failure from here on still ends the frame: the acquired image must be presented.
    core::Result<void> status = impl.ResizeTargets(sceneExtent);
    if (status) {
        status = impl.ResizeInterface(frame.extent);
    }
    // Temporal antialiasing jitters the camera; the debug views show their own images unjittered.
    const bool taa = impl.antialiasing == Antialiasing::Taa && impl.debugView == DebugView::Off;
    // The render resolution (ADR-0049). Only temporal antialiasing upscales: without it, and in debug views,
    // the scene renders at the output's.
    const auto now = std::chrono::steady_clock::now();
    const float seconds = impl.fixedFrameTime.has_value() ? *impl.fixedFrameTime
                          : impl.lastFrameTime.has_value()
                              ? std::chrono::duration<float>(now - *impl.lastFrameTime).count()
                              : 0.0f;
    impl.lastFrameTime = now;
    const ResolutionConfig& resolution = impl.resolution;
    impl.renderScale = impl.dynamicResolution.Update(
        impl.frameGpuTime, seconds,
        {.enabled = taa && resolution.dynamic && !impl.fixedFrameTime.has_value(),
         .maxScale = taa ? resolution.scale : 1.0f,
         .minScale = resolution.minScale,
         .budget = resolution.targetFps > 0.0f ? 0.9 * 1000.0 / resolution.targetFps : 0.0});
    const rhi::Extent renderExtent = RenderExtent(sceneExtent, impl.renderScale);
    impl.renderExtent = renderExtent;
    const float renderScale = static_cast<float>(renderExtent.width) / static_cast<float>(sceneExtent.width);
    RenderGraph& graph = impl.graph;
    const bool cacheView = impl.debugView == DebugView::RadianceCache;
    const bool reflectionView = impl.debugView == DebugView::Reflections;
    const bool indirectView = impl.debugView == DebugView::Indirect || cacheView || reflectionView;
    const bool indirectLight = impl.indirectLight || indirectView;
    const rhi::TextureHandle indirect = indirectLight ? impl.globalIllumination->Output() : rhi::TextureHandle{};
    // Reflections come with indirect light: rough surfaces fall back on it, and smooth ones read its cache.
    const bool reflect = indirectLight && (impl.reflections || reflectionView) && !cacheView;
    const rhi::TextureHandle reflections = reflect ? impl.globalIllumination->Reflections() : rhi::TextureHandle{};
    const std::uint32_t frameIndex = impl.frameIndex++;
    impl.time += view.timeStep;
    // Volumetric fog (ADR-0051), in the lit scene and its own debug view; the grid follows the output's size.
    const bool fogView = impl.debugView == DebugView::Fog;
    const bool fog = (impl.fog || fogView) && (impl.debugView == DebugView::Off || fogView);
    if (status && fog) {
        status = impl.volumetricFog->Resize(sceneExtent, impl.fogGrid);
    }
    const rhi::TextureHandle fogTexture = fog ? impl.volumetricFog->Output() : rhi::TextureHandle{};
    const glm::vec2 jitter = taa ? Jitter(impl.frameNumber, JitterPositions(renderScale)) : glm::vec2(0.0f);
    // Particles in the frame's structure (ADR-0062), where they were at the end of the last simulation.
    ParticlePass::Traced traced = impl.particlePass->TracedParticles();
    if (!impl.tracedParticles) {
        traced.structure = {};
    }
    if (status) {
        status = impl.rayTracing->Reserve(frame.slot, view.draws.size()).and_then([&](std::uint32_t structure) {
            return impl.meshPass->Prepare(
                view, {.extent = renderExtent,
                       .targetExtent = sceneExtent,
                       // Textures keep the output's detail.
                       .mipBias = std::log2(renderScale),
                       .indirect = indirect,
                       .accelerationStructure = structure,
                       .jitter = jitter,
                       .frameIndex = frameIndex,
                       .reflections = reflections,
                       .background = impl.background,
                       .depth = impl.depth,
                       .exposure = preExposure,
                       .fog = fogTexture,
                       .time = static_cast<float>(impl.time),
                       .sharpReflections =
                           reflect ? std::min(GlobalIllumination::kSharpRoughness, impl.reflectionRoughness) : 0.0f,
                       .debugFlags = impl.debugFlags,
                       .particleData = traced.particles,
                       .particleEffects = traced.effects,
                       .particleCurves = traced.curves});
        });
        impl.particleSpawns = static_cast<std::uint32_t>(view.particleSpawns.size());
    }
    const bool hasFrame = status && view.camera.has_value();
    if (hasFrame) {
        status =
            impl.rayTracing->Prepare(impl.meshPass->TracedDraws(),
                                     {.structure = traced.structure, .boxes = traced.boxes, .count = traced.boxCount});
    }
    // Skinned draws are skinned once, for every pass that draws them and for rays.
    const rhi::BufferUse skinnedVertices{impl.meshPass->SkinnedVertices(), rhi::Access::VertexInputRead};
    if (status && hasFrame) {
        impl.meshPass->AddSkinning(graph);
        impl.rayTracing->AddBuild(graph);
        impl.meshPass->AddEmissiveLights(graph);
        // Before everything that draws lights: particles, the radiance cache, ReSTIR and glass (ADR-0124).
        if (impl.lightGrid) {
            impl.meshPass->AddLightGrid(graph);
        }
    }
    const rhi::BufferHandle lightGrid = impl.lightGrid ? impl.meshPass->LightGrid() : rhi::BufferHandle{};
    // Particles move before the G-buffer, which draws mesh particles (ADR-0053).
    if (status && hasFrame) {
        status = impl.particlePass->AddSimulation(graph, view, impl.meshPass->FrameDataIndex(),
                                                  {.structure = impl.rayTracing->Structure(),
                                                   .instanceBuffer = impl.meshPass->InstanceDataIndex(),
                                                   .materialBuffer = impl.meshPass->MaterialDataIndex(),
                                                   .skinnedVertices = impl.meshPass->SkinnedVertices(),
                                                   .emissiveTriangles = impl.meshPass->EmissiveTriangles(),
                                                   .lightGrid = lightGrid,
                                                   .lightMap = impl.meshPass->LightMapIndex(),
                                                   .previousLightCount = impl.meshPass->PreviousLightCount()},
                                                  *impl.meshPass);
    }
    // Indirect light (ADR-0040): the cache forgets the last scene after a cut, and whatever it held while it was
    // off. The fog in front of each point of the view (ADR-0051): a frame that does not follow the last one
    // fogged starts its history over.
    const bool illuminate = status && hasFrame && indirectLight;
    const bool fogs = status && hasFrame && fog;
    std::optional<GlobalIlluminationInputs> giInputs;
    if (illuminate) {
        giInputs = GlobalIlluminationInputs{
            .extent = renderExtent,
            .frameBuffer = impl.meshPass->FrameDataIndex(),
            .instanceBuffer = impl.meshPass->InstanceDataIndex(),
            .materialBuffer = impl.meshPass->MaterialDataIndex(),
            .structure = impl.rayTracing->Structure(),
            .skinnedVertices = impl.meshPass->SkinnedVertices(),
            .materialGrid = impl.meshPass->MaterialGrid(),
            .emissiveTriangles = impl.meshPass->EmissiveTriangles(),
            .lightGrid = lightGrid,
            .depth = impl.depth,
            .normals = impl.normals,
            // Where the denoiser barely knows a surface, more rays (ADR-0121); after a cut, it will forget all.
            .denoiserGuides =
                impl.denoise && impl.denoised && !view.cut ? impl.denoiser->PreviousGuides() : rhi::TextureHandle{},
            .motion = impl.motion,
            .frameIndex = frameIndex,
            .reflections = reflect,
            .reflectionRoughness = impl.reflectionRoughness,
            // The clamp is set in what the screen shows.
            .settings = {.bounces = impl.indirect.bounces,
                         .lightCandidates = impl.indirect.lightCandidates,
                         .clamp = impl.indirect.clamp / displayScale,
                         .rays = impl.indirect.rays,
                         .newRays = impl.indirect.newRays,
                         .pixelsPerRay = impl.indirect.pixelsPerRay,
                         .reflectionPixelsPerRay = impl.indirect.reflectionPixelsPerRay}};
    }
    const GlobalIllumination::Mode giMode =
        cacheView ? GlobalIllumination::Mode::Cache : GlobalIllumination::Mode::Indirect;
    if (status) {
        const bool meshParticles = hasFrame && impl.particlePass->HasMeshes();
        std::vector<rhi::BufferUse> buffers{skinnedVertices,
                                            {impl.meshPass->PreviousSkinnedPositions(), rhi::Access::VertexShaderRead}};
        // The material grid is read by the G-buffer and by every ray hit after it (ADR-0094).
        if (hasFrame) {
            impl.meshPass->AddMaterialGrid(graph);
            buffers.push_back(
                {impl.meshPass->MaterialGrid(), rhi::Access::FragmentShaderRead | rhi::Access::ComputeShaderRead});
            // So is the light grid, by glass and by every pass that draws lights after it (ADR-0124).
            buffers.push_back({lightGrid, rhi::Access::FragmentShaderRead | rhi::Access::ComputeShaderRead});
        }
        if (meshParticles) {
            std::ranges::copy(impl.particlePass->MeshBuffers(), std::back_inserter(buffers));
        }
        // What reads no G-buffer goes before it: on a GPU with a compute queue, the radiance cache and the fog
        // run there, beside the G-buffer (ADR-0095).
        if (illuminate) {
            if (!impl.illuminated || view.cut) {
                impl.globalIllumination->Reset();
            }
            impl.globalIllumination->AddCacheUpdate(graph, *giInputs, giMode);
        }
        if (fogs) {
            if (!impl.fogged || view.cut) {
                impl.volumetricFog->Reset();
            }
            impl.volumetricFog->AddTo(graph, {.frameBuffer = impl.meshPass->FrameDataIndex(),
                                              .instanceBuffer = impl.meshPass->InstanceDataIndex(),
                                              .materialBuffer = impl.meshPass->MaterialDataIndex(),
                                              .structure = impl.rayTracing->Structure(),
                                              .skinnedVertices = impl.meshPass->SkinnedVertices(),
                                              .emissiveTriangles = impl.meshPass->EmissiveTriangles(),
                                              .frameIndex = frameIndex,
                                              .lightMap = impl.meshPass->LightMapIndex(),
                                              .previousLightCount = impl.meshPass->PreviousLightCount(),
                                              .smoke = traced.smoke,
                                              .async = true});
        }
        graph.AddPass(
            {.name = "G-buffer",
             .rendering = MeshPass::GBufferRendering(impl.shading, impl.normals, impl.motion, impl.depth, renderExtent),
             .buffers = std::move(buffers)},
            [&impl, meshParticles](rhi::CommandList& commands) {
                impl.meshPass->RecordGBuffer(commands);
                if (meshParticles) {
                    impl.particlePass->RecordMeshes(commands, impl.meshPass->FrameDataIndex(), *impl.meshPass);
                }
            });
    }
    // Indirect light, after its cache's passes before the G-buffer.
    if (illuminate) {
        impl.globalIllumination->AddTo(graph, *giInputs, giMode);
    }
    impl.illuminated = illuminate;
    // The light of the frame's lights, and the indirect light, at each pixel of the G-buffer (ADR-0044). A
    // frame that does not follow the last one lit reuses nothing from it.
    const bool lightsView = impl.debugView == DebugView::Lights || impl.debugView == DebugView::LightCount;
    const bool light = status && hasFrame;
    if (light) {
        if (!impl.lit || view.cut) {
            impl.directLighting->Reset();
        }
        DirectLightingSettings settings = impl.restir;
        settings.debug = lightsView;
        settings.lightCount = impl.debugView == DebugView::LightCount;
        impl.directLighting->AddTo(graph,
                                   {.extent = renderExtent,
                                    .frameBuffer = impl.meshPass->FrameDataIndex(),
                                    .instanceBuffer = impl.meshPass->InstanceDataIndex(),
                                    .materialBuffer = impl.meshPass->MaterialDataIndex(),
                                    .structure = impl.rayTracing->Structure(),
                                    .skinnedVertices = impl.meshPass->SkinnedVertices(),
                                    .emissiveTriangles = impl.meshPass->EmissiveTriangles(),
                                    .lightGrid = lightGrid,
                                    .depth = impl.depth,
                                    .normals = impl.normals,
                                    .motion = impl.motion,
                                    .baseColor = impl.shading.baseColor,
                                    .emission = impl.shading.emission,
                                    .layers = impl.shading.layers,
                                    .indirect = illuminate ? indirect : rhi::TextureHandle{},
                                    .reflections = illuminate ? reflections : rhi::TextureHandle{},
                                    .diffuse = impl.shading.diffuse,
                                    .specular = impl.shading.specular,
                                    .lightMap = impl.meshPass->LightMapIndex(),
                                    .previousLightCount = impl.meshPass->PreviousLightCount(),
                                    .frameIndex = frameIndex},
                                   settings);
    }
    impl.lit = light;
    impl.fogged = fogs;
    // The lights' light, denoised, then put back together with the rest (ADR-0039). A frame that does not
    // follow the last one denoised starts the history over.
    const bool denoise = status && hasFrame && impl.denoise;
    if (denoise) {
        if (!impl.denoised || view.cut) {
            impl.denoiser->Reset();
        }
        impl.denoiser->AddTo(graph, {.extent = renderExtent,
                                     .frameBuffer = impl.meshPass->FrameDataIndex(),
                                     .diffuse = impl.shading.diffuse,
                                     .specular = impl.shading.specular,
                                     .depth = impl.depth,
                                     .normals = impl.normals,
                                     .motion = impl.motion,
                                     .reflections = illuminate ? reflections : rhi::TextureHandle{},
                                     .diffuseRepeats = illuminate ? impl.indirect.pixelsPerRay : 1U,
                                     .specularRepeats = illuminate ? impl.indirect.reflectionPixelsPerRay : 1U});
    }
    impl.denoised = denoise;
    if (status && hasFrame) {
        impl.meshPass->AddComposite(graph, impl.hdr,
                                    {.shading = &impl.shading,
                                     .diffuse = denoise ? impl.denoiser->Diffuse() : impl.shading.diffuse,
                                     .specular = denoise ? impl.denoiser->Specular() : impl.shading.specular,
                                     .depth = impl.depth,
                                     .fog = fogTexture,
                                     .fogOnly = fogView});
    }
    if (status && hasFrame) {
        const glm::mat4 cameraWorld = glm::inverse(view.camera->view);
        impl.particlePass->AddDraw(graph, impl.meshPass->FrameDataIndex(), impl.depth, impl.hdr, impl.reactive,
                                   impl.distortion, glm::vec3(cameraWorld[0]), glm::vec3(cameraWorld[1]), renderExtent,
                                   fogTexture);
        // Heat haze bends the frame before glass copies it, so that glass shows it too (ADR-0053).
        if (impl.particlePass->HasDistortion()) {
            impl.particlePass->AddDistortion(graph, impl.meshPass->FrameDataIndex(), impl.hdr, impl.background,
                                             impl.distortion, renderExtent);
        }
    }
    // Glass, over everything else, which it reads from a copy of the frame (ADR-0042).
    if (status && hasFrame && impl.meshPass->HasGlass()) {
        graph.AddPass(
            {.name = "Behind glass",
             .group = "Glass",
             .textures = {{impl.hdr, rhi::Access::TransferRead}, {impl.background, rhi::Access::TransferWrite, true}}},
            [&impl](rhi::CommandList& commands) { commands.CopyTexture(impl.hdr, impl.background); });
        graph.AddPass({.name = "Glass",
                       .group = "Glass",
                       .rendering = rhi::RenderingDesc{.colorTarget = impl.hdr, .area = renderExtent},
                       .textures = {{impl.hdr, rhi::Access::ColorTarget},
                                    {impl.background, rhi::Access::FragmentShaderRead},
                                    {impl.depth, rhi::Access::FragmentShaderRead},
                                    {fogTexture, rhi::Access::FragmentShaderRead}},
                       .buffers = {skinnedVertices,
                                   {skinnedVertices.buffer, rhi::Access::FragmentShaderRead},
                                   {impl.meshPass->EmissiveTriangles(), rhi::Access::FragmentShaderRead}},
                       .structures = {{impl.rayTracing->Structure(), rhi::Access::RayQueryRead}}},
                      [&impl](rhi::CommandList& commands) { impl.meshPass->RecordGlass(commands); });
    }
    // After everything the frame draws in HDR. A frame that does not follow the last one resolved starts
    // the history over.
    const Upscaler upscaler = resolution.upscaler;
    if (status && hasFrame && taa && upscaler == Upscaler::Fsr3) {
        status = impl.fsr3->Resize(sceneExtent);
    }
    const bool resolve = status && hasFrame && taa;
    if (resolve) {
        // A change of upscaler starts the new one's history over.
        const bool restart = !impl.resolved || view.cut || upscaler != impl.upscaledBy;
        if (upscaler == Upscaler::Fsr3) {
            if (restart) {
                impl.fsr3->Reset();
            }
            impl.fsr3->AddTo(graph, {.color = impl.hdr,
                                     .depth = impl.depth,
                                     .motion = impl.motion,
                                     .reactive = impl.reactive,
                                     .renderExtent = renderExtent,
                                     .jitter = jitter,
                                     .perspective = impl.meshPass->FramePerspective(),
                                     .seconds = seconds});
        } else {
            if (restart) {
                impl.temporalResolve->Reset();
            }
            impl.temporalResolve->AddTo(graph, {.frameBuffer = impl.meshPass->FrameDataIndex(),
                                                .color = impl.hdr,
                                                .depth = impl.depth,
                                                .motion = impl.motion,
                                                .reactive = impl.reactive});
        }
        impl.upscaledBy = upscaler;
        ++impl.frameNumber;
    }
    impl.resolved = resolve;
    const bool gbufferView = impl.debugView == DebugView::Motion || impl.debugView == DebugView::Normals ||
                             impl.debugView == DebugView::Layers || impl.debugView == DebugView::BaseColor;
    if (status && hasFrame && gbufferView) {
        const std::uint32_t mode = impl.debugView == DebugView::Normals     ? kShowNormals
                                   : impl.debugView == DebugView::Layers    ? kShowLayers
                                   : impl.debugView == DebugView::BaseColor ? kShowBaseColor
                                                                            : kShowMotion;
        const rhi::TextureHandle shown = mode == kShowNormals     ? impl.normals
                                         : mode == kShowLayers    ? impl.shading.layers
                                         : mode == kShowBaseColor ? impl.shading.baseColor
                                                                  : impl.motion;
        graph.AddPass(
            {.name = "G-buffer debug view",
             .rendering = rhi::RenderingDesc{.colorTarget = impl.hdr},
             .textures = {{impl.hdr, rhi::Access::ColorTarget, true}, {shown, rhi::Access::FragmentShaderRead}}},
            [&impl, sceneExtent, shown, mode](rhi::CommandList& commands) {
                commands.BindPipeline(impl.gbufferDebug);
                commands.Push(GBufferDebugPushConstants{
                    impl.device.BindlessIndex(shown), mode,
                    glm::vec2(static_cast<float>(sceneExtent.width), static_cast<float>(sceneExtent.height))});
                commands.Draw(3);
            });
    }
    if (status && hasFrame && impl.probeDepth) {
        const Perspective& perspective = impl.meshPass->FramePerspective();
        impl.depthProbe->AddTo(graph, frame.slot, impl.depth, renderExtent, perspective.nearPlane, perspective.farPlane,
                               impl.meshPass->ViewProjection());
    }
    if (status && hasFrame && impl.debugView == DebugView::Rays) {
        impl.rayTracing->AddDebugView(graph, impl.hdr, impl.meshPass->FrameDataIndex(),
                                      impl.meshPass->InstanceDataIndex());
    }

    // The path tracer's average goes on while the view, the size and the settings stay; the scene itself is
    // not compared, so that moving objects leave trails in it.
    const bool path = status && hasFrame && impl.debugView == DebugView::Path;
    if (path) {
        status = impl.pathTracer->Resize(sceneExtent);
    }
    if (path && status) {
        const PathTracerSettings& settings = impl.pathSettings;
        if (!impl.pathShown || impl.meshPass->ViewProjection() != impl.pathViewProjection ||
            settings.maxBounces != impl.pathAveraged.maxBounces ||
            settings.background != impl.pathAveraged.background) {
            impl.pathTracer->Reset();
        }
        impl.pathViewProjection = impl.meshPass->ViewProjection();
        impl.pathAveraged = settings;
        impl.pathTracer->AddTo(graph,
                               {.frameBuffer = impl.meshPass->FrameDataIndex(),
                                .instanceBuffer = impl.meshPass->InstanceDataIndex(),
                                .materialBuffer = impl.meshPass->MaterialDataIndex(),
                                .structure = impl.rayTracing->Structure(),
                                .skinnedVertices = impl.meshPass->SkinnedVertices()},
                               settings);
    }
    impl.pathShown = path;

    if (status && !hasFrame) {
        // Nothing to draw: black.
        graph.AddPass(
            {.name = "Clear",
             .rendering = rhi::RenderingDesc{.colorTarget = impl.hdr, .clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)}},
            [](rhi::CommandList& /*commands*/) {});
    }
    // The interface goes after the antialiasing, which would blur text, and under the overlay, in a layer
    // that the output pass puts over the image.
    bool layerDrawn = false;
    const auto layerRendering = [&impl, &layerDrawn] {
        rhi::RenderingDesc rendering{.colorTarget = impl.interfaceLayer};
        if (!layerDrawn) {
            rendering.clearColor = glm::vec4(0.0f);
            layerDrawn = true;
        }
        return rendering;
    };
    if (status && canvas != nullptr && !canvas->Commands().empty()) {
        graph.AddPass(
            {.name = "Interface", .rendering = layerRendering()},
            [&impl, &status, canvas](rhi::CommandList& commands) { status = impl.uiPass->Record(commands, *canvas); });
    }
    if (status && overlay) {
        graph.AddPass({.name = "Overlay", .group = "Interface", .rendering = layerRendering()},
                      [&overlay, &frame](rhi::CommandList& commands) { overlay(commands, frame.extent); });
    }

    rhi::PipelineHandle output;
    if (status) {
        core::Result<rhi::PipelineHandle> pipeline = impl.OutputPipeline(impl.swapchain.ColorFormat());
        if (pipeline) {
            output = *pipeline;
        } else {
            status = tl::unexpected(std::move(pipeline.error()));
        }
    }
    if (status) {
        rhi::TextureHandle source = path                                    ? impl.pathTracer->Output()
                                    : illuminate && reflectionView          ? impl.globalIllumination->Reflections()
                                    : illuminate && indirectView            ? impl.globalIllumination->Output()
                                    : light && lightsView                   ? impl.directLighting->DebugOutput()
                                    : resolve && upscaler == Upscaler::Fsr3 ? impl.fsr3->Output()
                                    : resolve                               ? impl.temporalResolve->Output()
                                                                            : impl.hdr;

        // The lens and the shutter (ADR-0063): depth of field and motion blur, on the lit scene only.
        if (hasFrame && resolve && impl.debugView == DebugView::Off && !path && view.camera.has_value()) {
            source = impl.cameraEffects->AddTo(graph, {.frameBuffer = impl.meshPass->FrameDataIndex(),
                                                       .color = source,
                                                       .depth = impl.depth,
                                                       .motion = impl.motion,
                                                       .aperture = view.camera->aperture,
                                                       .focusDistance = view.camera->focusDistance,
                                                       .verticalFov = impl.meshPass->FramePerspective().verticalFov,
                                                       .shutter = impl.image.motionBlur});
        }
        // The lit scene is metered for the next frames' exposure; debug views are not.
        if (adapts && hasFrame && impl.debugView == DebugView::Off) {
            impl.exposureMeter->AddTo(graph, frame.slot, source, sceneExtent, preExposure);
        }
        // Bloom spreads the lit scene's light; its debug view shows it alone.
        const bool bloomView = impl.debugView == DebugView::Bloom;
        const float bloomShare = bloomView ? 1.0f : std::clamp(impl.image.bloom, 0.0f, 1.0f);
        const bool blooms = hasFrame && bloomShare > 0.0f && (impl.debugView == DebugView::Off || bloomView);
        if (blooms) {
            impl.bloom->AddTo(graph, source);
        }
        const bool hdr10 = impl.swapchain.CurrentColorSpace() == rhi::ColorSpace::Hdr10;
        // The lens marks the lit scene, not debug views or the path tracer's reference.
        const bool lens = hasFrame && impl.debugView == DebugView::Off && !path;
        const OutputPushConstants push{
            .scene = impl.device.BindlessIndex(source),
            .bloom = blooms ? impl.device.BindlessIndex(impl.bloom->Output()) : kNoTexture,
            .layer = layerDrawn ? impl.device.BindlessIndex(impl.interfaceLayer) : kNoTexture,
            .toneMapper = impl.image.toneMapper == ToneMapper::AgX ? 0U : 1U,
            .scale = displayScale,
            .bloomShare = blooms ? bloomShare : 0.0f,
            .headroom = hdr10 ? peak / paperWhite : 1.0f,
            .paperWhite = paperWhite,
            .hdr10 = hdr10 ? 1U : 0U,
            // Sharpening undoes the softness of the temporal history.
            .sharpness = resolve ? std::clamp(impl.image.sharpness, 0.0f, 1.0f) : 0.0f,
            .sceneX = sceneOffset.x,
            .sceneY = sceneOffset.y,
            .grain = lens ? std::clamp(impl.image.filmGrain, 0.0f, 1.0f) : 0.0f,
            .vignette = lens ? std::clamp(impl.image.vignette, 0.0f, 1.0f) : 0.0f,
            .aberration = lens ? std::clamp(impl.image.chromaticAberration, 0.0f, 1.0f) : 0.0f,
            .lensDirt = lens && blooms ? std::clamp(impl.image.lensDirt, 0.0f, 1.0f) : 0.0f,
            .frame = static_cast<std::uint32_t>(impl.frameNumber),
            .colorVision = static_cast<std::uint32_t>(impl.image.colorVision),
            .padding = {}};
        std::vector<rhi::TextureUse> reads{{frame.backBuffer, rhi::Access::ColorTarget, true},
                                           {source, rhi::Access::FragmentShaderRead}};
        if (layerDrawn) {
            reads.push_back({impl.interfaceLayer, rhi::Access::FragmentShaderRead});
        }
        if (blooms) {
            reads.push_back({impl.bloom->Output(), rhi::Access::FragmentShaderRead});
        }
        // A photo (ADR-0077): the same image without the interface, in SDR, and the scene's light it comes from.
        // A video frame (ADR-0123) keeps the interface, where the fades are, and needs no light.
        if (impl.capture->Requested()) {
            const bool video = impl.capture->Video();
            core::Result<rhi::PipelineHandle> capturePipeline = impl.OutputPipeline(rhi::Format::Rgba8Srgb);
            core::Result<rhi::TextureHandle> target = impl.capture->DisplayTarget(frame.slot, frame.extent);
            if (capturePipeline && target) {
                OutputPushConstants photo = push;
                const bool withLayer = video && layerDrawn;
                photo.layer = withLayer ? push.layer : kNoTexture;
                photo.hdr10 = 0;
                photo.headroom = 1.0f;
                std::vector<rhi::TextureUse> photoReads{{*target, rhi::Access::ColorTarget, true},
                                                        {source, rhi::Access::FragmentShaderRead}};
                if (blooms) {
                    photoReads.push_back({impl.bloom->Output(), rhi::Access::FragmentShaderRead});
                }
                if (withLayer) {
                    photoReads.push_back({impl.interfaceLayer, rhi::Access::FragmentShaderRead});
                }
                graph.AddPass({.name = "Capture",
                               .rendering = rhi::RenderingDesc{.colorTarget = *target},
                               .textures = std::move(photoReads)},
                              [pipeline = *capturePipeline, photo](rhi::CommandList& commands) {
                                  commands.BindPipeline(pipeline);
                                  commands.Push(photo);
                                  commands.Draw(3);
                              });
                // The light is the lit scene's, or the path tracer's; other debug views have none worth keeping.
                const bool lit = !video && (impl.debugView == DebugView::Off || path);
                if (core::Result<void> copied = impl.capture->AddCopies(
                        graph, frame.slot, lit ? std::optional(source) : std::nullopt, sceneExtent, displayScale);
                    !copied) {
                    DAI_LOG_WARNING(kModule, "Cannot capture the frame: {}", core::Format(copied.error()));
                }
            } else {
                DAI_LOG_WARNING(kModule, "Cannot capture the frame: {}",
                                core::Format(capturePipeline ? target.error() : capturePipeline.error()));
            }
        }
        graph.AddPass({.name = "Output",
                       .rendering = rhi::RenderingDesc{.colorTarget = frame.backBuffer},
                       .textures = std::move(reads)},
                      [output, push](rhi::CommandList& commands) {
                          commands.BindPipeline(output);
                          commands.Push(push);
                          commands.Draw(3);
                      });
    }
    impl.timedGroups[frame.slot] = graph.Execute(frame.commands, impl.device, impl.computeGroups[frame.slot]);

    core::Result<void> ended = impl.device.EndFrame(&impl.swapchain);
    return status ? ended : status;
}

rhi::Device& Renderer::Device() {
    return m_impl->device;
}

rhi::Format Renderer::ColorFormat() const {
    return kInterfaceFormat;
}

const std::string& Renderer::ShaderError() const {
    return m_impl->shaderError;
}

} // namespace daidalos::renderer
