// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/platform/Window.hpp>
#include <daidalos/renderer/Material.hpp>
#include <daidalos/renderer/Primitives.hpp>
#include <daidalos/renderer/SceneView.hpp>
#include <daidalos/rhi/Device.hpp>

#include <functional>
#include <memory>
#include <string>

namespace daidalos::renderer {

struct RendererConfig {
    /// Enables the Vulkan validation layers if they are installed.
    bool validation = false;
    bool vsync = true;
};

/// Records into the back buffer after the scene, inside a rendering scope without depth.
using OverlayRecorder = std::function<void(rhi::CommandList& commands, rhi::Extent extent)>;

/// Draws scenes to a window. The window must have been created with WindowConfig::vulkan and must
/// outlive the renderer. Meshes and materials are released with it.
class Renderer {
public:
    /// Returns the errors of rhi::Device::Create and rhi::Swapchain::Create.
    static core::Result<Renderer> Create(platform::Window& window, const RendererConfig& config);

    Renderer(Renderer&& other) noexcept;
    Renderer& operator=(Renderer&& other) noexcept;
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    ~Renderer();

    [[nodiscard]] core::Result<MeshHandle> CreateMesh(const resources::MeshData& data, const std::string& name);
    /// Safe at any time: the GPU buffers go away once frames in flight are finished.
    void DestroyMesh(MeshHandle mesh);

    [[nodiscard]] core::Result<MaterialHandle> CreateMaterial(const MaterialDesc& desc);
    /// Draws using the material fall back to the default one.
    void DestroyMaterial(MaterialHandle material);

    void SetVsync(bool vsync);

    /// Draws one frame, following the window's size. A minimized window draws nothing. An error means
    /// that the device is unusable and the application should stop.
    [[nodiscard]] core::Result<void> Render(const SceneView& view, const OverlayRecorder& overlay = {});

    [[nodiscard]] rhi::Device& Device();
    [[nodiscard]] rhi::Format ColorFormat() const;
    /// Compiler output of the last failed shader reload; empty when the shaders are up to date.
    /// Reloads exist in builds with DAI_DEV_TOOLS only.
    [[nodiscard]] const std::string& ShaderError() const;

private:
    struct Impl;

    explicit Renderer(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};

} // namespace daidalos::renderer
