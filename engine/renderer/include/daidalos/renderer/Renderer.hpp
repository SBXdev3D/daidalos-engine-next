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

/// How the edges of what is drawn are smoothed (ADR-0037).
enum class Antialiasing {
    Off,
    /// Temporal antialiasing: the camera moves by a fraction of a pixel each frame, and each frame is
    /// blended with the earlier ones, reprojected with motion vectors.
    Taa,
};

/// What the frame shows instead of the lit scene, to check the renderer.
enum class DebugView {
    Off,
    /// The frame's acceleration structure traced from the camera: each instance in its own color (ADR-0033).
    Rays,
    /// The reference path tracer (ADR-0034), which averages a path per pixel per frame while the view stays.
    Path,
    /// The motion vectors of the pre-pass, in color (ADR-0037): grey where nothing moved.
    Motion,
    /// The indirect diffuse light of each pixel, as its ray found it, before the denoiser (ADR-0040).
    Indirect,
    /// The light the radiance cache holds at each pixel's surface (ADR-0040): its cells show.
    RadianceCache,
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
    /// Light that bounces off surfaces (ADR-0040). Off leaves direct light only: what no light reaches is black.
    bool indirectLight = true;
    /// Filters the noise of soft shadows and indirect light (ADR-0039). Off shows the light as the rays
    /// found it.
    bool denoiser = true;
    /// Radians across the view, for cameras that set no field of view of their own.
    float fieldOfView = glm::radians(90.0f);
    /// Meters: farther, nothing is drawn or lit.
    float viewDistance = 150.0f;
    Antialiasing antialiasing = Antialiasing::Taa;
    DebugView debugView = DebugView::Off;
    /// The path tracer's settings: surfaces a path meets at most (1 is direct light only), paths per pixel
    /// after which the image stops changing, and the radiance a path that leaves the scene sees.
    std::uint32_t pathBounces = 4;
    std::uint32_t pathSamples = 4096;
    glm::vec3 pathBackground{0.0f};
};

/// What the renderer did in the last frame it drew, and what it holds, for diagnostics.
struct FrameStats {
    /// Draws in the view, and those within the view distance.
    std::uint32_t draws = 0;
    std::uint32_t drawnDraws = 0;
    /// Triangles of the drawn draws, once each: the pre-pass draws them again.
    std::uint64_t triangles = 0;
    std::uint32_t lights = 0;
    std::uint32_t particleSpawns = 0;
    /// Instances of the frame's acceleration structure, and how many skinned draws had one built.
    std::uint32_t rayInstances = 0;
    std::uint32_t skinnedStructures = 0;
    /// Paths per pixel the path tracer has averaged, with DebugView::Path.
    std::uint32_t pathSamples = 0;
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
    void SetIndirectLight(bool enabled);
    void SetDenoiser(bool enabled);
    /// From the next frame; see RendererConfig.
    void SetFieldOfView(float radians);
    void SetViewDistance(float meters);
    void SetAntialiasing(Antialiasing antialiasing);
    void SetDebugView(DebugView view);
    /// The path tracer starts over when they change; see RendererConfig.
    void SetPathTracer(std::uint32_t bounces, std::uint32_t samples);

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
