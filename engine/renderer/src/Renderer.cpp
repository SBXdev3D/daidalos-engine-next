// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/core/Assert.hpp>
#include <daidalos/core/Profile.hpp>
#include <daidalos/logging/Log.hpp>
#include <daidalos/renderer/Renderer.hpp>
#include <daidalos/rhi/Spirv.hpp>
#include <daidalos/rhi/Swapchain.hpp>

#include <glm/matrix.hpp>
#include <glm/vec4.hpp>
#include <shaders/MotionDebug.ps.spv.h>
#include <shaders/MotionDebug.vs.spv.h>
#include <shaders/Tonemap.ps.spv.h>
#include <shaders/Tonemap.vs.spv.h>

#include <array>
#include <chrono>
#include <optional>
#include <utility>

#include "AmbientOcclusionPass.hpp"
#include "Denoiser.hpp"
#include "MeshPass.hpp"
#include "ParticlePass.hpp"
#include "PathTracerPass.hpp"
#include "RayTracingScene.hpp"
#include "RenderGraph.hpp"
#include "ShaderCompiler.hpp"
#include "TemporalResolvePass.hpp"
#include "UiPass.hpp"

namespace daidalos::renderer {

namespace {

constexpr std::string_view kModule = "renderer";
constexpr rhi::Format kDepthFormat = rhi::Format::D32Float;
/// Lighting is computed in HDR, then tonemapped into the swapchain (ADR-0022).
constexpr rhi::Format kHdrFormat = rhi::Format::Rgba16Float;
// Matches TonemapPushConstants in shaders/Tonemap.slang.
struct TonemapPushConstants {
    std::uint32_t hdrTexture;
    float exposure;
};

// Matches MotionDebugPushConstants in shaders/MotionDebug.slang.
struct MotionDebugPushConstants {
    std::uint32_t motionTexture;
    std::uint32_t padding;
    glm::vec2 extent;
};

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
        temporalResolve.reset();
        denoiser.reset();
        if (meshPass.has_value()) {
            meshPass->DestroyShadingTargets(shading);
        }
        rayTracing.reset();
        meshPass.reset();
        ambientOcclusionPass.reset();
        particlePass.reset();
        uiPass.reset();
        device.DestroyPipeline(tonemap);
        device.DestroyPipeline(motionDebug);
        device.DestroyTexture(motion);
        device.DestroyTexture(reactive);
        device.DestroyTexture(depth);
        device.DestroyTexture(normals);
        device.DestroyTexture(hdr);
    }

    /// The targets follow the size of the back buffer.
    core::Result<void> ResizeTargets(rhi::Extent extent);
    /// Reads the timestamps of the last finished frame recorded in this slot into gpuTimes.
    void ReadGpuTimes(std::uint32_t slot);
    void ReloadShaders();

    platform::Window* window;
    rhi::Device device;
    rhi::Swapchain swapchain;
    std::optional<MeshPass> meshPass;
    std::optional<AmbientOcclusionPass> ambientOcclusionPass;
    std::optional<ParticlePass> particlePass;
    std::optional<UiPass> uiPass;
    std::optional<TemporalResolvePass> temporalResolve;
    std::optional<Denoiser> denoiser;
    /// Counts the frames, for the jitter of temporal antialiasing.
    std::uint64_t frameNumber = 0;
    /// Counts every frame drawn, for the noise of soft shadows.
    std::uint32_t frameIndex = 0;
    /// The last frame went through the denoiser: its history can be reprojected.
    bool denoised = false;
    bool denoise = true;
    /// The last frame went through the temporal resolve: its history can be reprojected.
    bool resolved = false;
    /// Bursts and emissions of the last view drawn, for FrameStats.
    std::uint32_t particleSpawns = 0;
    bool ambientOcclusion = true;
    Antialiasing antialiasing = Antialiasing::Taa;
    DebugView debugView = DebugView::Off;
    std::optional<RayTracingScene> rayTracing;
    std::optional<PathTracerPass> pathTracer;
    PathTracerSettings pathSettings;
    /// What the path tracer's average was made with: a change starts it over.
    glm::mat4 pathViewProjection{0.0f};
    PathTracerSettings pathAveraged;
    bool pathShown = false;
    rhi::PipelineHandle tonemap;
    rhi::PipelineHandle motionDebug;
    /// What the mesh pass shades into, which the composite puts together into hdr (ADR-0039).
    ShadingTargets shading;
    rhi::TextureHandle hdr;
    rhi::TextureHandle depth;
    /// World normals of the pre-pass.
    rhi::TextureHandle normals;
    /// Motion vectors of the pre-pass (ADR-0037).
    rhi::TextureHandle motion;
    /// The particles' coverage, where temporal antialiasing trusts its history less.
    rhi::TextureHandle reactive;
    rhi::Extent targetExtent;
    bool vsync;
    float exposure = 1.0f;
    RenderGraph graph;
    /// The timed groups of the frame recorded in each slot, in the order of their timestamps.
    std::array<std::vector<std::string_view>, rhi::kFramesInFlight> timedGroups;
    std::vector<PassTime> gpuTimes;
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
    device.DestroyTexture(hdr);
    meshPass->DestroyShadingTargets(shading);
    depth = {};
    normals = {};
    motion = {};
    reactive = {};
    hdr = {};
    DAI_TRY(temporalResolve->Resize(extent));
    DAI_TRY(denoiser->Resize(extent));
    DAI_TRY_ASSIGN(shading, meshPass->CreateShadingTargets(extent));
    DAI_TRY_ASSIGN(reactive, device.CreateTexture({.extent = extent,
                                                   .format = ParticlePass::kReactiveFormat,
                                                   .usage = rhi::TextureUsage::ColorTarget | rhi::TextureUsage::Sampled,
                                                   .name = "Reactive"}));
    DAI_TRY_ASSIGN(motion, device.CreateTexture({.extent = extent,
                                                 .format = MeshPass::kMotionFormat,
                                                 .usage = rhi::TextureUsage::ColorTarget | rhi::TextureUsage::Sampled,
                                                 .name = "Motion"}));
    DAI_TRY_ASSIGN(normals, device.CreateTexture({.extent = extent,
                                                  .format = MeshPass::kNormalFormat,
                                                  .usage = rhi::TextureUsage::ColorTarget | rhi::TextureUsage::Sampled,
                                                  .name = "Normals"}));
    DAI_TRY(ambientOcclusionPass->Resize(extent));
    DAI_TRY_ASSIGN(depth, device.CreateTexture({.extent = extent,
                                                .format = kDepthFormat,
                                                .usage = rhi::TextureUsage::DepthTarget | rhi::TextureUsage::Sampled,
                                                .name = "Depth"}));
    DAI_TRY_ASSIGN(hdr, device.CreateTexture({.extent = extent,
                                              .format = kHdrFormat,
                                              .usage = rhi::TextureUsage::ColorTarget | rhi::TextureUsage::Sampled,
                                              .name = "HDR color"}));
    targetExtent = extent;
    return {};
}

void Renderer::Impl::ReadGpuTimes(std::uint32_t slot) {
    const std::span<const double> timestamps = device.CompletedTimestamps();
    const std::vector<std::string_view>& groups = timedGroups[slot];
    gpuTimes.clear();
    if (timestamps.size() != groups.size() + 1) {
        return;
    }
    for (std::size_t group = 0; group < groups.size(); ++group) {
        gpuTimes.push_back({groups[group], timestamps[group + 1] - timestamps[group]});
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
    const std::filesystem::path source = std::filesystem::path(DAI_SHADER_SOURCE_DIR) / "Mesh.slang";
    core::Result<void> reloaded = CompileShader(DAI_SLANGC_PATH, source, ShaderStage::Vertex)
                                      .and_then([&](std::vector<std::uint32_t> vertexShader) {
                                          return CompileShader(DAI_SLANGC_PATH, source, ShaderStage::Pixel)
                                              .and_then([&](std::vector<std::uint32_t> fragmentShader) {
                                                  return meshPass->ReplaceShaders(vertexShader, fragmentShader);
                                              });
                                      });
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
    DAI_TRY_ASSIGN(rhi::Device device, rhi::Device::Create(deviceConfig));

    const platform::Extent size = window.PixelSize();
    DAI_TRY_ASSIGN(rhi::Swapchain swapchain,
                   rhi::Swapchain::Create(
                       device, [&window](std::uintptr_t instance) { return window.CreateVulkanSurface(instance); },
                       {{size.width, size.height}, config.vsync}));

    auto impl = std::make_unique<Impl>(window, std::move(device), std::move(swapchain), config.vsync);
    impl->exposure = config.exposure;
    DAI_TRY_ASSIGN(impl->tonemap, impl->device.CreatePipeline({
                                      .vertexShader = rhi::ToSpirv(kTonemapVsSpirv),
                                      .fragmentShader = rhi::ToSpirv(kTonemapPsSpirv),
                                      .colorFormat = impl->swapchain.ColorFormat(),
                                      .cull = rhi::CullMode::None,
                                      .name = "Tonemap",
                                  }));
    DAI_TRY_ASSIGN(impl->motionDebug, impl->device.CreatePipeline({
                                          .vertexShader = rhi::ToSpirv(kMotionDebugVsSpirv),
                                          .fragmentShader = rhi::ToSpirv(kMotionDebugPsSpirv),
                                          .colorFormat = kHdrFormat,
                                          .cull = rhi::CullMode::None,
                                          .name = "Motion debug view",
                                      }));
    DAI_TRY_ASSIGN(UiPass uiPass, UiPass::Create(impl->device, impl->swapchain.ColorFormat()));
    impl->uiPass.emplace(std::move(uiPass));
    impl->antialiasing = config.antialiasing;
    DAI_TRY_ASSIGN(MeshPass meshPass, MeshPass::Create(impl->device, kHdrFormat, kDepthFormat));
    impl->meshPass.emplace(std::move(meshPass));
    DAI_TRY_ASSIGN(RayTracingScene rayTracing, RayTracingScene::Create(impl->device, kHdrFormat));
    impl->rayTracing.emplace(std::move(rayTracing));
    impl->debugView = config.debugView;
    DAI_TRY_ASSIGN(PathTracerPass pathTracer, PathTracerPass::Create(impl->device));
    impl->pathTracer.emplace(std::move(pathTracer));
    impl->pathSettings = {
        .maxBounces = config.pathBounces, .maxSamples = config.pathSamples, .background = config.pathBackground};
    DAI_TRY_ASSIGN(AmbientOcclusionPass ambientOcclusionPass, AmbientOcclusionPass::Create(impl->device));
    impl->ambientOcclusionPass.emplace(std::move(ambientOcclusionPass));
    impl->ambientOcclusion = config.ambientOcclusion;
    DAI_TRY_ASSIGN(ParticlePass particlePass, ParticlePass::Create(impl->device, kHdrFormat));
    impl->particlePass.emplace(std::move(particlePass));
    DAI_TRY_ASSIGN(TemporalResolvePass temporalResolve, TemporalResolvePass::Create(impl->device));
    impl->temporalResolve.emplace(std::move(temporalResolve));
    DAI_TRY_ASSIGN(Denoiser denoiser, Denoiser::Create(impl->device));
    impl->denoiser.emplace(std::move(denoiser));
    impl->denoise = config.denoiser;
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

void Renderer::DestroyMaterial(MaterialHandle material) {
    m_impl->meshPass->DestroyMaterial(material);
}

core::Result<ParticleEffectHandle> Renderer::CreateParticleEffect(const ParticleEffectDesc& desc) {
    return m_impl->particlePass->CreateEffect(desc);
}

void Renderer::DestroyParticleEffect(ParticleEffectHandle effect) {
    m_impl->particlePass->DestroyEffect(effect);
}

void Renderer::SetVsync(bool vsync) {
    m_impl->vsync = vsync;
}

void Renderer::SetExposure(float exposure) {
    m_impl->exposure = exposure;
}

void Renderer::SetAmbientOcclusion(bool enabled) {
    m_impl->ambientOcclusion = enabled;
}

void Renderer::SetDenoiser(bool enabled) {
    m_impl->denoise = enabled;
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
    m_impl->debugView = view;
}

void Renderer::SetPathTracer(std::uint32_t bounces, std::uint32_t samples) {
    m_impl->pathSettings.maxBounces = bounces;
    m_impl->pathSettings.maxSamples = samples;
}

void Renderer::SetNormalStrength(float strength) {
    m_impl->meshPass->SetNormalStrength(strength);
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
            .rayInstances = impl.rayTracing->LastStats().instances,
            .skinnedStructures = impl.rayTracing->LastStats().skinnedStructures,
            .pathSamples = impl.debugView == DebugView::Path ? impl.pathTracer->Samples() : 0,
            .meshes = impl.meshPass->MeshCount(),
            .materials = impl.meshPass->MaterialCount(),
            .particleEffects = impl.particlePass->EffectCount()};
}

core::Result<void> Renderer::Render(const SceneView& view, const ui::Canvas* canvas, const OverlayRecorder& overlay) {
    DAI_PROFILE_SCOPE("renderer::Render");
    Impl& impl = *m_impl;

    impl.ReloadShaders();

    const platform::Extent size = impl.window->PixelSize();
    impl.swapchain.Configure({{size.width, size.height}, impl.vsync});

    DAI_TRY_ASSIGN(std::optional<rhi::Frame> begun, impl.device.BeginFrame(&impl.swapchain));
    if (!begun.has_value()) {
        return {}; // Nothing can be drawn this frame.
    }
    rhi::Frame& frame = *begun;
    impl.ReadGpuTimes(frame.slot);

    // A failure from here on still ends the frame: the acquired image must be presented.
    core::Result<void> status = impl.ResizeTargets(frame.extent);
    // Temporal antialiasing jitters the camera; the debug views show their own images unjittered.
    const bool taa = impl.antialiasing == Antialiasing::Taa && impl.debugView == DebugView::Off;
    RenderGraph& graph = impl.graph;
    const rhi::TextureHandle occlusion =
        impl.ambientOcclusion ? impl.ambientOcclusionPass->Output() : rhi::TextureHandle{};
    if (status) {
        status = impl.rayTracing->Reserve(frame.slot, view.draws.size()).and_then([&](std::uint32_t structure) {
            return impl.meshPass->Prepare(view, {.extent = frame.extent,
                                                 .occlusion = occlusion,
                                                 .accelerationStructure = structure,
                                                 .jitter = taa ? Jitter(impl.frameNumber) : glm::vec2(0.0f),
                                                 .frameIndex = impl.frameIndex++});
        });
        impl.particleSpawns = static_cast<std::uint32_t>(view.particleSpawns.size());
    }
    const bool hasFrame = status && view.camera.has_value();
    if (hasFrame) {
        status = impl.rayTracing->Prepare(impl.meshPass->TracedDraws());
    }
    // Skinned draws are skinned once, for every pass that draws them and for rays.
    const rhi::BufferUse skinnedVertices{impl.meshPass->SkinnedVertices(), rhi::Access::VertexInputRead};
    if (status && hasFrame) {
        impl.meshPass->AddSkinning(graph);
        impl.rayTracing->AddBuild(graph);
    }
    if (status) {
        // Where nothing is drawn, nothing moved: the sky's motion comes from the camera alone.
        graph.AddPass(
            {.name = "Prepass",
             .rendering = rhi::RenderingDesc{.colorTarget = impl.normals,
                                             .clearColor = glm::vec4(0.5f, 1.0f, 0.5f, 1.0f),
                                             .moreColorTargets = {{impl.motion, glm::vec4(0.0f)}},
                                             .depthTarget = impl.depth,
                                             .clearDepth = 0.0f},
             .buffers = {skinnedVertices, {impl.meshPass->PreviousSkinnedPositions(), rhi::Access::VertexShaderRead}}},
            [&impl](rhi::CommandList& commands) { impl.meshPass->RecordPrepass(commands); });
    }
    if (hasFrame && impl.ambientOcclusion) {
        impl.ambientOcclusionPass->AddTo(graph, impl.meshPass->FrameDataIndex(), impl.depth, impl.normals);
    }
    if (hasFrame) {
        status = impl.particlePass->AddSimulation(graph, view.particleSpawns, view.timeStep,
                                                  impl.meshPass->FrameDataIndex(), impl.depth, impl.normals);
    }
    if (status) {
        graph.AddPass({.name = "Meshes",
                       .rendering = MeshPass::ShadingRendering(impl.shading, impl.depth,
                                                               glm::vec4(view.atmosphere.fogColor, 1.0f)),
                       .textures = {{occlusion, rhi::Access::FragmentShaderRead}},
                       // Shadow rays: the frame's structure, and the vertices of the skinned draws they test for alpha.
                       .buffers = {skinnedVertices, {skinnedVertices.buffer, rhi::Access::FragmentShaderRead}},
                       .structures = {{hasFrame ? impl.rayTracing->Structure() : rhi::AccelerationStructureHandle{},
                                       rhi::Access::RayQueryRead}}},
                      [&impl](rhi::CommandList& commands) { impl.meshPass->Record(commands); });
    }
    // The lights' light, denoised, then put back together with the rest (ADR-0039). A frame that does not
    // follow the last one denoised starts the history over.
    const bool denoise = status && hasFrame && impl.denoise;
    if (denoise) {
        if (!impl.denoised || view.cut) {
            impl.denoiser->Reset();
        }
        impl.denoiser->AddTo(graph, {.frameBuffer = impl.meshPass->FrameDataIndex(),
                                     .diffuse = impl.shading.diffuse,
                                     .specular = impl.shading.specular,
                                     .depth = impl.depth,
                                     .normals = impl.normals,
                                     .motion = impl.motion});
    }
    impl.denoised = denoise;
    if (status && hasFrame) {
        impl.meshPass->AddComposite(graph, impl.hdr,
                                    {.shading = &impl.shading,
                                     .diffuse = denoise ? impl.denoiser->Diffuse() : impl.shading.diffuse,
                                     .specular = denoise ? impl.denoiser->Specular() : impl.shading.specular,
                                     .depth = impl.depth});
    }
    if (status && hasFrame) {
        const glm::mat4 cameraWorld = glm::inverse(view.camera->view);
        impl.particlePass->AddDraw(graph, impl.meshPass->FrameDataIndex(), impl.depth, impl.hdr, impl.reactive,
                                   glm::vec3(cameraWorld[0]), glm::vec3(cameraWorld[1]));
    }
    // After everything the frame draws in HDR. A frame that does not follow the last one resolved starts
    // the history over.
    const bool resolve = status && hasFrame && taa;
    if (resolve) {
        if (!impl.resolved || view.cut) {
            impl.temporalResolve->Reset();
        }
        impl.temporalResolve->AddTo(graph, {.frameBuffer = impl.meshPass->FrameDataIndex(),
                                            .color = impl.hdr,
                                            .depth = impl.depth,
                                            .motion = impl.motion,
                                            .reactive = impl.reactive});
        ++impl.frameNumber;
    }
    impl.resolved = resolve;
    if (status && hasFrame && impl.debugView == DebugView::Motion) {
        graph.AddPass(
            {.name = "Motion debug view",
             .rendering = rhi::RenderingDesc{.colorTarget = impl.hdr},
             .textures = {{impl.hdr, rhi::Access::ColorTarget, true}, {impl.motion, rhi::Access::FragmentShaderRead}}},
            [&impl, &frame](rhi::CommandList& commands) {
                commands.BindPipeline(impl.motionDebug);
                commands.Push(MotionDebugPushConstants{
                    impl.device.BindlessIndex(impl.motion), 0,
                    glm::vec2(static_cast<float>(frame.extent.width), static_cast<float>(frame.extent.height))});
                commands.Draw(3);
            });
    }
    if (status && hasFrame && impl.debugView == DebugView::Rays) {
        impl.rayTracing->AddDebugView(graph, impl.hdr, impl.meshPass->FrameDataIndex(),
                                      impl.meshPass->InstanceDataIndex());
    }

    // The path tracer's average goes on while the view, the size and the settings stay; the scene itself is
    // not compared, so that moving objects leave trails in it.
    const bool path = status && hasFrame && impl.debugView == DebugView::Path;
    if (path) {
        status = impl.pathTracer->Resize(frame.extent);
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

    if (status) {
        // Without a camera, the mesh pass only cleared its base to the fog's color.
        const rhi::TextureHandle source = path       ? impl.pathTracer->Output()
                                          : resolve  ? impl.temporalResolve->Output()
                                          : hasFrame ? impl.hdr
                                                     : impl.shading.base;

        graph.AddPass({.name = "Tonemap",
                       .rendering = rhi::RenderingDesc{.colorTarget = frame.backBuffer},
                       .textures = {{frame.backBuffer, rhi::Access::ColorTarget, true},
                                    {source, rhi::Access::FragmentShaderRead}}},
                      [&impl, source](rhi::CommandList& commands) {
                          commands.BindPipeline(impl.tonemap);
                          commands.Push(TonemapPushConstants{impl.device.BindlessIndex(source), impl.exposure});
                          commands.Draw(3);
                      });
    }

    // The interface goes after the antialiasing, which would blur text, and under the debug overlay.
    if (status && canvas != nullptr && !canvas->Commands().empty()) {
        graph.AddPass(
            {.name = "Interface", .rendering = rhi::RenderingDesc{.colorTarget = frame.backBuffer}},
            [&impl, &status, canvas](rhi::CommandList& commands) { status = impl.uiPass->Record(commands, *canvas); });
    }
    if (status && overlay) {
        graph.AddPass({.name = "Overlay", .rendering = rhi::RenderingDesc{.colorTarget = frame.backBuffer}},
                      [&overlay, &frame](rhi::CommandList& commands) { overlay(commands, frame.extent); });
    }
    impl.timedGroups[frame.slot] = graph.Execute(frame.commands);

    core::Result<void> ended = impl.device.EndFrame(&impl.swapchain);
    return status ? ended : status;
}

rhi::Device& Renderer::Device() {
    return m_impl->device;
}

rhi::Format Renderer::ColorFormat() const {
    return m_impl->swapchain.ColorFormat();
}

const std::string& Renderer::ShaderError() const {
    return m_impl->shaderError;
}

} // namespace daidalos::renderer
