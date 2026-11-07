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
#include <daidalos/ui/Canvas.hpp>

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace daidalos::renderer {

/// How the edges of what is drawn are smoothed (ADR-0026).
enum class Antialiasing {
    Off,
    /// Fast approximate antialiasing, a pass over the final image.
    Fxaa,
    /// Multisampling, with 2, 4 or 8 samples per pixel: geometry edges only, at a higher cost. Devices
    /// that support fewer samples use their most.
    Msaa2,
    Msaa4,
    Msaa8,
};

struct RendererConfig {
    /// Enables the Vulkan validation layers if they are installed.
    bool validation = false;
    /// With validation, also checks that barriers order every use of every resource. Slow.
    bool synchronizationValidation = false;
    bool vsync = true;
    /// Multiplies the light before tonemapping.
    float exposure = 1.0f;
    /// Anisotropic filtering of textures, from 1 (off) to 16.
    float anisotropy = 16.0f;
    /// Multiplies the tilt of every normal map: 0 flattens them, 1 keeps them as authored.
    float normalStrength = 1.0f;
    /// Darkens the ambient light in creases and corners (ADR-0024).
    bool ambientOcclusion = true;
    /// Spot lights with a shadow map, from 0 to kMaxShadowedLights: the nearest that ask for one.
    std::uint32_t shadowLights = 4;
    /// Texels across each light's shadow map, from 256 to 4096.
    std::uint32_t shadowResolution = 1024;
    /// Radians across the view, for cameras that set no field of view of their own.
    float fieldOfView = glm::radians(90.0f);
    /// Meters: farther, nothing is drawn or lit.
    float viewDistance = 150.0f;
    Antialiasing antialiasing = Antialiasing::Fxaa;
};

/// What the renderer did in the last frame it drew, and what it holds, for diagnostics.
struct FrameStats {
    /// Draws in the view, and those within the view distance.
    std::uint32_t draws = 0;
    std::uint32_t drawnDraws = 0;
    /// Triangles of the drawn draws, once each: the pre-pass and the shadow maps draw them again.
    std::uint64_t triangles = 0;
    std::uint32_t lights = 0;
    std::uint32_t shadowedLights = 0;
    /// Draws into the shadow maps, all lights together.
    std::uint32_t shadowDraws = 0;
    std::uint32_t particleSpawns = 0;
    /// Objects the renderer holds.
    std::size_t meshes = 0;
    std::size_t materials = 0;
    std::size_t particleEffects = 0;
};

/// How long a pass took on the GPU.
struct PassTime {
    std::string_view name;
    double milliseconds = 0.0;
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

    /// The texture must outlive the effect.
    [[nodiscard]] core::Result<ParticleEffectHandle> CreateParticleEffect(const ParticleEffectDesc& desc);
    void DestroyParticleEffect(ParticleEffectHandle effect);

    void SetVsync(bool vsync);
    void SetExposure(float exposure);
    void SetNormalStrength(float strength);
    void SetAmbientOcclusion(bool enabled);
    /// From the next frame; see RendererConfig.
    void SetFieldOfView(float radians);
    void SetViewDistance(float meters);
    void SetAntialiasing(Antialiasing antialiasing);

    /// GPU time of each pass of a recent frame, which the GPU finished a frame or two ago. Empty on a
    /// device without timestamps, and for the first frames.
    [[nodiscard]] std::span<const PassTime> GpuTimes() const;
    /// Counts of the last frame drawn, and of the objects held now.
    [[nodiscard]] FrameStats Stats() const;

    /// Draws one frame, following the window's size: the scene, then the canvas if there is one, then
    /// the overlay. A minimized window draws nothing. An error means that the device is unusable and the
    /// application should stop.
    [[nodiscard]] core::Result<void> Render(const SceneView& view, const ui::Canvas* canvas = nullptr,
                                            const OverlayRecorder& overlay = {});

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
