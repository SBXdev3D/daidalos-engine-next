// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/core/Assert.hpp>
#include <daidalos/core/Profile.hpp>
#include <daidalos/logging/Log.hpp>
#include <daidalos/renderer/Renderer.hpp>
#include <daidalos/rhi/Swapchain.hpp>

#include <glm/vec4.hpp>

#include <chrono>
#include <optional>
#include <utility>

#include "MeshPass.hpp"
#include "ShaderCompiler.hpp"

namespace daidalos::renderer {

namespace {

constexpr std::string_view kModule = "renderer";
constexpr rhi::Format kDepthFormat = rhi::Format::D32Float;

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
        device.DestroyTexture(depth);
    }

    core::Result<void> ResizeDepth(rhi::Extent extent);
    void ReloadShaders();

    platform::Window* window;
    rhi::Device device;
    rhi::Swapchain swapchain;
    std::optional<MeshPass> meshPass;
    rhi::TextureHandle depth;
    rhi::Extent depthExtent;
    bool vsync;
    std::string shaderError;
#if DAI_DEV_TOOLS
    std::optional<ShaderWatcher> shaderWatcher;
    std::chrono::steady_clock::time_point nextShaderPoll;
#endif
};

core::Result<void> Renderer::Impl::ResizeDepth(rhi::Extent extent) {
    if (depth && depthExtent == extent) {
        return {};
    }
    device.DestroyTexture(depth);
    depth = {};
    DAI_TRY_ASSIGN(
        rhi::TextureHandle created,
        device.CreateTexture(
            {.extent = extent, .format = kDepthFormat, .usage = rhi::TextureUsage::DepthTarget, .name = "Depth"}));
    depth = created;
    depthExtent = extent;
    return {};
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
    DAI_TRY_ASSIGN(rhi::Device device, rhi::Device::Create(deviceConfig));

    const platform::Extent size = window.PixelSize();
    DAI_TRY_ASSIGN(rhi::Swapchain swapchain,
                   rhi::Swapchain::Create(
                       device, [&window](std::uintptr_t instance) { return window.CreateVulkanSurface(instance); },
                       {{size.width, size.height}, config.vsync}));

    auto impl = std::make_unique<Impl>(window, std::move(device), std::move(swapchain), config.vsync);
    DAI_TRY_ASSIGN(MeshPass meshPass, MeshPass::Create(impl->device, impl->swapchain.ColorFormat(), kDepthFormat));
    impl->meshPass.emplace(std::move(meshPass));

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

void Renderer::SetVsync(bool vsync) {
    m_impl->vsync = vsync;
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

    // A failure from here on still ends the frame: the acquired image must be presented.
    core::Result<void> status = impl.ResizeDepth(frame.extent);
    rhi::CommandList& commands = frame.commands;
    commands.Transition(frame.backBuffer, rhi::TextureState::ColorTarget);
    if (status) {
        commands.Discard(impl.depth, rhi::TextureState::DepthTarget);
        commands.BeginRendering({.colorTarget = frame.backBuffer,
                                 .clearColor = glm::vec4(view.atmosphere.fogColor, 1.0f),
                                 .depthTarget = impl.depth,
                                 .clearDepth = 0.0f});
        const float aspect = static_cast<float>(frame.extent.width) / static_cast<float>(frame.extent.height);
        status = impl.meshPass->Record(commands, view, aspect);
        commands.EndRendering();

        if (overlay) {
            commands.BeginRendering({.colorTarget = frame.backBuffer});
            overlay(commands, frame.extent);
            commands.EndRendering();
        }
    }

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
