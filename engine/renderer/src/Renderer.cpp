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
#include <shaders/Tonemap.ps.spv.h>
#include <shaders/Tonemap.vs.spv.h>

#include <array>
#include <chrono>
#include <optional>
#include <utility>

#include "AmbientOcclusionPass.hpp"
#include "FxaaPass.hpp"
#include "MeshPass.hpp"
#include "ParticlePass.hpp"
#include "RayTracingScene.hpp"
#include "RenderGraph.hpp"
#include "ShaderCompiler.hpp"
#include "UiPass.hpp"

namespace daidalos::renderer {

namespace {

constexpr std::string_view kModule = "renderer";
constexpr rhi::Format kDepthFormat = rhi::Format::D32Float;
/// Lighting is computed in HDR, then tonemapped into the swapchain (ADR-0022).
constexpr rhi::Format kHdrFormat = rhi::Format::Rgba16Float;
/// The passes timed on the GPU, in order: each ends at a timestamp, after the one at the start.
// Matches TonemapPushConstants in shaders/Tonemap.slang.
struct TonemapPushConstants {
    std::uint32_t hdrTexture;
    float exposure;
    std::uint32_t encode;
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
        rayTracing.reset();
        meshPass.reset();
        ambientOcclusionPass.reset();
        particlePass.reset();
        fxaaPass.reset();
        uiPass.reset();
        device.DestroyPipeline(tonemap);
        device.DestroyPipeline(tonemapEncoded);
        device.DestroyTexture(tonemapped);
        device.DestroyTexture(depth);
        device.DestroyTexture(normals);
        device.DestroyTexture(hdr);
        DestroyMultisampledTargets();
    }

    void DestroyMultisampledTargets() {
        device.DestroyTexture(multisampledDepth);
        device.DestroyTexture(multisampledNormals);
        device.DestroyTexture(multisampledHdr);
        multisampledDepth = {};
        multisampledNormals = {};
        multisampledHdr = {};
    }

    /// Samples per pixel of the antialiasing mode, as many as the device supports.
    [[nodiscard]] std::uint32_t SamplesOf(Antialiasing mode) const {
        const std::uint32_t wanted = mode == Antialiasing::Msaa2   ? 2
                                     : mode == Antialiasing::Msaa4 ? 4
                                     : mode == Antialiasing::Msaa8 ? 8
                                                                   : 1;
        return std::min(wanted, device.Info().maxSamples);
    }
    /// Follows a change of antialiasing to or from multisampling, between two frames.
    core::Result<void> ApplySamples();

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
    std::optional<FxaaPass> fxaaPass;
    std::optional<UiPass> uiPass;
    /// Bursts and emissions of the last view drawn, for FrameStats.
    std::uint32_t particleSpawns = 0;
    bool ambientOcclusion = true;
    Antialiasing antialiasing = Antialiasing::Fxaa;
    DebugView debugView = DebugView::Off;
    std::optional<RayTracingScene> rayTracing;
    /// Samples per pixel of the pre-pass and the mesh pass. With more than one, they draw into the
    /// multisampled targets, which resolve into depth, normals and hdr.
    std::uint32_t samples = 1;
    rhi::TextureHandle multisampledDepth;
    rhi::TextureHandle multisampledNormals;
    rhi::TextureHandle multisampledHdr;
    rhi::PipelineHandle tonemap;
    /// Tonemaps for FXAA, into tonemapped.
    rhi::PipelineHandle tonemapEncoded;
    rhi::TextureHandle tonemapped;
    rhi::TextureHandle hdr;
    rhi::TextureHandle depth;
    /// World normals of the pre-pass.
    rhi::TextureHandle normals;
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

core::Result<void> Renderer::Impl::ApplySamples() {
    const std::uint32_t wanted = SamplesOf(antialiasing);
    if (wanted == samples) {
        return {};
    }
    // Between two frames: those in flight still use the old pipelines and targets.
    DAI_TRY(device.WaitIdle());
    DAI_TRY(meshPass->SetSamples(wanted));
    samples = wanted;
    targetExtent = {};
    DAI_LOG_INFO(kModule, "Multisampling with {} samples per pixel", samples);
    return {};
}

core::Result<void> Renderer::Impl::ResizeTargets(rhi::Extent extent) {
    if (depth && hdr && targetExtent == extent) {
        return {};
    }
    DestroyMultisampledTargets();
    if (samples > 1) {
        const auto multisampled = [&](rhi::Format format, rhi::TextureUsage usage, const char* name,
                                      rhi::TextureHandle& texture) -> core::Result<void> {
            DAI_TRY_ASSIGN(texture,
                           device.CreateTexture(
                               {.extent = extent, .format = format, .usage = usage, .samples = samples, .name = name}));
            return {};
        };
        DAI_TRY(multisampled(kDepthFormat, rhi::TextureUsage::DepthTarget, "Multisampled depth", multisampledDepth));
        DAI_TRY(multisampled(MeshPass::kNormalFormat, rhi::TextureUsage::ColorTarget, "Multisampled normals",
                             multisampledNormals));
        DAI_TRY(multisampled(kHdrFormat, rhi::TextureUsage::ColorTarget, "Multisampled HDR color", multisampledHdr));
    }
    device.DestroyTexture(depth);
    device.DestroyTexture(normals);
    device.DestroyTexture(hdr);
    device.DestroyTexture(tonemapped);
    depth = {};
    normals = {};
    hdr = {};
    tonemapped = {};
    DAI_TRY_ASSIGN(tonemapped,
                   device.CreateTexture({.extent = extent,
                                         .format = FxaaPass::kSourceFormat,
                                         .usage = rhi::TextureUsage::ColorTarget | rhi::TextureUsage::Sampled,
                                         .name = "Tonemapped"}));
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
    DAI_TRY_ASSIGN(impl->tonemapEncoded, impl->device.CreatePipeline({
                                             .vertexShader = rhi::ToSpirv(kTonemapVsSpirv),
                                             .fragmentShader = rhi::ToSpirv(kTonemapPsSpirv),
                                             .colorFormat = FxaaPass::kSourceFormat,
                                             .cull = rhi::CullMode::None,
                                             .name = "Tonemap for FXAA",
                                         }));
    DAI_TRY_ASSIGN(FxaaPass fxaaPass, FxaaPass::Create(impl->device, impl->swapchain.ColorFormat()));
    impl->fxaaPass.emplace(std::move(fxaaPass));
    DAI_TRY_ASSIGN(UiPass uiPass, UiPass::Create(impl->device, impl->swapchain.ColorFormat()));
    impl->uiPass.emplace(std::move(uiPass));
    impl->antialiasing = config.antialiasing;
    impl->samples = impl->SamplesOf(config.antialiasing);
    DAI_TRY_ASSIGN(MeshPass meshPass, MeshPass::Create(impl->device, kHdrFormat, kDepthFormat,
                                                       {.shadowLights = config.shadowLights,
                                                        .shadowResolution = config.shadowResolution,
                                                        .samples = impl->samples}));
    impl->meshPass.emplace(std::move(meshPass));
    DAI_TRY_ASSIGN(RayTracingScene rayTracing, RayTracingScene::Create(impl->device, kHdrFormat));
    impl->rayTracing.emplace(std::move(rayTracing));
    impl->debugView = config.debugView;
    DAI_TRY_ASSIGN(AmbientOcclusionPass ambientOcclusionPass, AmbientOcclusionPass::Create(impl->device));
    impl->ambientOcclusionPass.emplace(std::move(ambientOcclusionPass));
    impl->ambientOcclusion = config.ambientOcclusion;
    DAI_TRY_ASSIGN(ParticlePass particlePass, ParticlePass::Create(impl->device, kHdrFormat));
    impl->particlePass.emplace(std::move(particlePass));
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
            .shadowedLights = counts.shadowedLights,
            .shadowDraws = counts.shadowDraws,
            .particleSpawns = impl.particleSpawns,
            .rayInstances = impl.rayTracing->LastStats().instances,
            .skinnedStructures = impl.rayTracing->LastStats().skinnedStructures,
            .meshes = impl.meshPass->MeshCount(),
            .materials = impl.meshPass->MaterialCount(),
            .particleEffects = impl.particlePass->EffectCount()};
}

core::Result<void> Renderer::Render(const SceneView& view, const ui::Canvas* canvas, const OverlayRecorder& overlay) {
    DAI_PROFILE_SCOPE("renderer::Render");
    Impl& impl = *m_impl;

    impl.ReloadShaders();
    DAI_TRY(impl.ApplySamples());

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
    const bool multisampled = impl.samples > 1;
    RenderGraph& graph = impl.graph;
    const rhi::TextureHandle occlusion =
        impl.ambientOcclusion ? impl.ambientOcclusionPass->Output() : rhi::TextureHandle{};
    if (status) {
        status = impl.rayTracing->Reserve(frame.slot, view.draws.size()).and_then([&](std::uint32_t structure) {
            return impl.meshPass->Prepare(
                view, {.extent = frame.extent, .occlusion = occlusion, .accelerationStructure = structure});
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
        impl.meshPass->AddShadows(graph);
        // With multisampling, the samples resolve into the targets that later passes read.
        graph.AddPass(
            {.name = "Prepass",
             .rendering = rhi::RenderingDesc{.colorTarget = multisampled ? impl.multisampledNormals : impl.normals,
                                             .clearColor = glm::vec4(0.5f, 1.0f, 0.5f, 1.0f),
                                             .depthTarget = multisampled ? impl.multisampledDepth : impl.depth,
                                             .clearDepth = 0.0f,
                                             .colorResolve = multisampled ? impl.normals : rhi::TextureHandle{},
                                             .depthResolve = multisampled ? impl.depth : rhi::TextureHandle{}},
             .buffers = {skinnedVertices}},
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
        graph.AddPass(
            {.name = "Meshes",
             .rendering = rhi::RenderingDesc{.colorTarget = multisampled ? impl.multisampledHdr : impl.hdr,
                                             .clearColor = glm::vec4(view.atmosphere.fogColor, 1.0f),
                                             .depthTarget = multisampled ? impl.multisampledDepth : impl.depth,
                                             .colorResolve = multisampled ? impl.hdr : rhi::TextureHandle{}},
             .textures = {{impl.meshPass->ShadowAtlas(), rhi::Access::FragmentShaderRead},
                          {occlusion, rhi::Access::FragmentShaderRead}},
             .buffers = {skinnedVertices}},
            [&impl](rhi::CommandList& commands) { impl.meshPass->Record(commands); });
    }
    if (status && hasFrame) {
        const glm::mat4 cameraWorld = glm::inverse(view.camera->view);
        impl.particlePass->AddDraw(graph, impl.meshPass->FrameDataIndex(), impl.depth, impl.hdr,
                                   glm::vec3(cameraWorld[0]), glm::vec3(cameraWorld[1]));
    }
    if (status && hasFrame && impl.debugView == DebugView::Rays) {
        impl.rayTracing->AddDebugView(graph, impl.hdr, impl.meshPass->FrameDataIndex(),
                                      impl.meshPass->InstanceDataIndex());
    }

    // With FXAA, tonemapping goes to an image that the antialiasing reads, and FXAA to the back buffer.
    const bool fxaa = impl.antialiasing == Antialiasing::Fxaa;
    if (status) {
        const rhi::TextureHandle tonemapTarget = fxaa ? impl.tonemapped : frame.backBuffer;
        graph.AddPass({.name = "Tonemap",
                       .rendering = rhi::RenderingDesc{.colorTarget = tonemapTarget},
                       .textures = {{tonemapTarget, rhi::Access::ColorTarget, true},
                                    {impl.hdr, rhi::Access::FragmentShaderRead}}},
                      [&impl, fxaa](rhi::CommandList& commands) {
                          commands.BindPipeline(fxaa ? impl.tonemapEncoded : impl.tonemap);
                          commands.Push(
                              TonemapPushConstants{impl.device.BindlessIndex(impl.hdr), impl.exposure, fxaa ? 1U : 0U});
                          commands.Draw(3);
                      });
    }
    if (status && fxaa) {
        graph.AddPass({.name = "Antialiasing",
                       .rendering = rhi::RenderingDesc{.colorTarget = frame.backBuffer},
                       .textures = {{frame.backBuffer, rhi::Access::ColorTarget, true},
                                    {impl.tonemapped, rhi::Access::FragmentShaderRead}}},
                      [&impl, &frame](rhi::CommandList& commands) {
                          impl.fxaaPass->Record(commands, impl.tonemapped, frame.extent);
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
