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
#include "ShaderCompiler.hpp"

namespace daidalos::renderer {

namespace {

constexpr std::string_view kModule = "renderer";
constexpr rhi::Format kDepthFormat = rhi::Format::D32Float;
/// Lighting is computed in HDR, then tonemapped into the swapchain (ADR-0022).
constexpr rhi::Format kHdrFormat = rhi::Format::Rgba16Float;
/// The passes timed on the GPU, in order: each ends at a timestamp, after the one at the start.
constexpr std::array<std::string_view, 9> kTimedPasses{"Shadows",   "Prepass", "Occlusion",    "Simulation", "Meshes",
                                                       "Particles", "Tonemap", "Antialiasing", "Overlay"};

// Matches TonemapPushConstants in shaders/Tonemap.hlsl.
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
        meshPass.reset();
        ambientOcclusionPass.reset();
        particlePass.reset();
        fxaaPass.reset();
        device.DestroyPipeline(tonemap);
        device.DestroyPipeline(tonemapEncoded);
        device.DestroyTexture(tonemapped);
        device.DestroyTexture(depth);
        device.DestroyTexture(normals);
        device.DestroyTexture(hdr);
    }

    /// The targets follow the size of the back buffer.
    core::Result<void> ResizeTargets(rhi::Extent extent);
    /// Reads the timestamps of the last finished frame into gpuTimes.
    void ReadGpuTimes();
    void ReloadShaders();

    platform::Window* window;
    rhi::Device device;
    rhi::Swapchain swapchain;
    std::optional<MeshPass> meshPass;
    std::optional<AmbientOcclusionPass> ambientOcclusionPass;
    std::optional<ParticlePass> particlePass;
    std::optional<FxaaPass> fxaaPass;
    bool ambientOcclusion = true;
    Antialiasing antialiasing = Antialiasing::Fxaa;
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
    std::array<PassTime, kTimedPasses.size()> gpuTimes{};
    bool gpuTimesValid = false;
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

void Renderer::Impl::ReadGpuTimes() {
    const std::span<const double> timestamps = device.CompletedTimestamps();
    gpuTimesValid = timestamps.size() == kTimedPasses.size() + 1;
    if (!gpuTimesValid) {
        return;
    }
    for (std::size_t pass = 0; pass < kTimedPasses.size(); ++pass) {
        gpuTimes[pass] = {kTimedPasses[pass], timestamps[pass + 1] - timestamps[pass]};
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
    const std::filesystem::path source = std::filesystem::path(DAI_SHADER_SOURCE_DIR) / "Mesh.hlsl";
    core::Result<void> reloaded =
        CompileShader(DAI_DXC_PATH, source, ShaderStage::Vertex).and_then([&](std::vector<std::uint32_t> vertexShader) {
            return CompileShader(DAI_DXC_PATH, source, ShaderStage::Pixel)
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
    impl->antialiasing = config.antialiasing;
    DAI_TRY_ASSIGN(MeshPass meshPass, MeshPass::Create(impl->device, kHdrFormat, kDepthFormat,
                                                       {.shadowLights = config.shadowLights,
                                                        .shadowResolution = config.shadowResolution}));
    impl->meshPass.emplace(std::move(meshPass));
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
    if (std::filesystem::is_directory(DAI_SHADER_SOURCE_DIR) && std::filesystem::exists(DAI_DXC_PATH)) {
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

void Renderer::SetNormalStrength(float strength) {
    m_impl->meshPass->SetNormalStrength(strength);
}

std::span<const PassTime> Renderer::GpuTimes() const {
    return m_impl->gpuTimesValid ? std::span<const PassTime>(m_impl->gpuTimes) : std::span<const PassTime>();
}

core::Result<void> Renderer::Render(const SceneView& view, const OverlayRecorder& overlay) {
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
    impl.ReadGpuTimes();

    // A failure from here on still ends the frame: the acquired image must be presented.
    core::Result<void> status = impl.ResizeTargets(frame.extent);
    rhi::CommandList& commands = frame.commands;
    commands.WriteTimestamp();
    if (status) {
        const rhi::TextureHandle occlusion =
            impl.ambientOcclusion ? impl.ambientOcclusionPass->Output() : rhi::TextureHandle{};
        status = impl.meshPass->Prepare(view, {.extent = frame.extent, .occlusion = occlusion});
    }
    if (status) {
        impl.meshPass->RecordShadows(commands);
    }
    commands.WriteTimestamp();
    if (status) {
        commands.Discard(impl.normals, rhi::TextureState::ColorTarget);
        commands.Discard(impl.depth, rhi::TextureState::DepthTarget);
        commands.BeginRendering({.colorTarget = impl.normals,
                                 .clearColor = glm::vec4(0.5f, 1.0f, 0.5f, 1.0f),
                                 .depthTarget = impl.depth,
                                 .clearDepth = 0.0f});
        impl.meshPass->RecordPrepass(commands);
        commands.EndRendering();
    }
    commands.WriteTimestamp();
    const bool hasFrame = status && view.camera.has_value();
    if (hasFrame) {
        commands.Transition(impl.depth, rhi::TextureState::ShaderRead);
        commands.Transition(impl.normals, rhi::TextureState::ShaderRead);
    }
    if (hasFrame && impl.ambientOcclusion) {
        impl.ambientOcclusionPass->Record(commands, impl.meshPass->FrameDataIndex(), impl.depth, impl.normals);
    }
    commands.WriteTimestamp();
    if (hasFrame) {
        // Particles bounce off the pre-pass's surfaces.
        status = impl.particlePass->Simulate(commands, view.particleSpawns, view.timeStep,
                                             impl.meshPass->FrameDataIndex(), impl.depth, impl.normals);
        commands.Transition(impl.depth, rhi::TextureState::DepthTarget);
    }
    commands.WriteTimestamp();
    if (status) {
        commands.Discard(impl.hdr, rhi::TextureState::ColorTarget);
        commands.BeginRendering({.colorTarget = impl.hdr,
                                 .clearColor = glm::vec4(view.atmosphere.fogColor, 1.0f),
                                 .depthTarget = impl.depth});
        impl.meshPass->Record(commands);
        commands.EndRendering();
    }
    commands.WriteTimestamp();
    if (status && hasFrame) {
        // Over the meshes, back to front; they read the depth to fade into surfaces.
        const glm::mat4 cameraWorld = glm::inverse(view.camera->view);
        commands.Transition(impl.depth, rhi::TextureState::ShaderRead);
        commands.BeginRendering({.colorTarget = impl.hdr});
        impl.particlePass->Draw(commands, impl.meshPass->FrameDataIndex(), impl.depth, glm::vec3(cameraWorld[0]),
                                glm::vec3(cameraWorld[1]));
        commands.EndRendering();
    }
    commands.WriteTimestamp();

    // With FXAA, tonemapping goes to an image that the antialiasing reads, and FXAA to the back buffer.
    const bool fxaa = impl.antialiasing == Antialiasing::Fxaa;
    commands.Transition(frame.backBuffer, rhi::TextureState::ColorTarget);
    if (status) {
        commands.Transition(impl.hdr, rhi::TextureState::ShaderRead);
        if (fxaa) {
            commands.Discard(impl.tonemapped, rhi::TextureState::ColorTarget);
        }
        commands.BeginRendering({.colorTarget = fxaa ? impl.tonemapped : frame.backBuffer});
        commands.BindPipeline(fxaa ? impl.tonemapEncoded : impl.tonemap);
        commands.Push(TonemapPushConstants{impl.device.BindlessIndex(impl.hdr), impl.exposure, fxaa ? 1U : 0U});
        commands.Draw(3);
        commands.EndRendering();
    }
    commands.WriteTimestamp();
    if (status && fxaa) {
        commands.Transition(impl.tonemapped, rhi::TextureState::ShaderRead);
        commands.BeginRendering({.colorTarget = frame.backBuffer});
        impl.fxaaPass->Record(commands, impl.tonemapped, frame.extent);
        commands.EndRendering();
    }
    commands.WriteTimestamp();

    if (status && overlay) {
        commands.BeginRendering({.colorTarget = frame.backBuffer});
        overlay(commands, frame.extent);
        commands.EndRendering();
    }
    commands.WriteTimestamp();

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
