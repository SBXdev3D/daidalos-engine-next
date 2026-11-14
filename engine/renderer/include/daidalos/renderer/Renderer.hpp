// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/platform/Window.hpp>
#include <daidalos/renderer/DisplayTransform.hpp>
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
    /// The motion vectors of the G-buffer, in color (ADR-0037): grey where nothing moved.
    Motion,
    /// The world normals of the G-buffer, normal maps included, in color: times 0.5 plus 0.5.
    Normals,
    /// The indirect diffuse light of each pixel, as its ray found it, before the denoiser (ADR-0040).
    Indirect,
    /// The light the radiance cache holds at each pixel's surface (ADR-0040): its cells show.
    RadianceCache,
    /// The light each pixel's specular lobe reflects, as its ray found it, before the denoiser (ADR-0042).
    Reflections,
    /// The light each pixel's reservoir chose, one hue per light (ADR-0044); black where none.
    Lights,
    /// The bloom alone, as the output pass would mix it in (ADR-0048).
    Bloom,
};

/// How indirect light is traced (ADR-0045).
struct IndirectLightConfig {
    /// Surfaces lit by the frame's lights along each pixel's ray before the radiance cache is read, from 0
    /// to 3; 0 reads the cache where the ray first hits.
    std::uint32_t bounces = 1;
    /// Lights weighed at each of those surfaces before one is kept, from 1 to 8.
    std::uint32_t lightCandidates = 4;
    /// The luminance bounced light is clamped to, as shown on screen before tone mapping; 0 for none.
    /// Direct light is never clamped.
    float clamp = 16.0f;
    /// Rays per pixel, from 1 to 16: less noise, for their cost.
    std::uint32_t rays = 1;
};

/// How the frame's light becomes the image a display shows (ADR-0048).
struct ImageConfig {
    /// The eye adapts to the light, within the range the scene's atmosphere sets. Off, it stays at the
    /// atmosphere's ev100.
    bool autoExposure = true;
    /// The share of the image that is its bloom, from 0 (none) to 1.
    float bloom = 0.04f;
    ToneMapper toneMapper = ToneMapper::AgX;
    /// HDR10 output on a display with HDR turned on; sRGB otherwise.
    bool hdr = true;
    /// Nits of paper white and of the display's peak in HDR10; 0 takes the system's.
    float hdrPaperWhite = 0.0f;
    float hdrPeak = 0.0f;
    /// Contrast-adaptive sharpening of the antialiased image, from 0 (none) to 1 (ADR-0049).
    float sharpness = 0.25f;
};

/// The resolution the scene is rendered at, below the output's, and how it moves to hold the frame rate
/// (ADR-0049). Temporal antialiasing upscales it to the output; without it, and in debug views, the scene
/// renders at the output's resolution.
/// What upscales the frame from the render resolution to the output's, with temporal antialiasing.
enum class Upscaler : std::uint8_t {
    /// The engine's own (ADR-0049).
    Engine,
    /// AMD FSR 3.1, run by the engine (ADR-0050).
    Fsr3,
};

struct ResolutionConfig {
    /// Render size over output size, per axis, from 0.25 to 1: the scale without dynamic resolution, its
    /// ceiling with it.
    float scale = 1.0f;
    /// The scale moves between minScale and scale to hold the target frame rate on the GPU.
    bool dynamic = true;
    float minScale = 0.5f;
    /// Frames per second the GPU's budget is set for: 90% of a frame's time.
    float targetFps = 60.0f;
    Upscaler upscaler = Upscaler::Engine;
};

struct RendererConfig {
    /// Enables the Vulkan validation layers if they are installed.
    bool validation = false;
    /// With validation, also checks that barriers order every use of every resource. Slow.
    bool synchronizationValidation = false;
    bool vsync = true;
    /// Stops added to the scene's exposure (Atmosphere::ev100): +1 shows the scene twice as bright (ADR-0043).
    float exposureCompensation = 0.0f;
    ImageConfig image;
    ResolutionConfig resolution;
    /// Anisotropic filtering of textures, from 1 (off) to 16.
    float anisotropy = 16.0f;
    /// Multiplies the tilt of every normal map: 0 flattens them, 1 keeps them as authored.
    float normalStrength = 1.0f;
    /// Light that bounces off surfaces (ADR-0040). Off leaves direct light only: what no light reaches is black.
    bool indirectLight = true;
    /// Filters the noise of soft shadows and indirect light (ADR-0039). Off shows the light as the rays
    /// found it.
    bool denoiser = true;
    /// Reflections traced along each pixel's specular lobe (ADR-0042), with indirect light. Off, the indirect
    /// diffuse light stands in for them.
    bool reflections = true;
    /// Surfaces rougher than this trace no reflection, from 0 (none do) to 1 (all do).
    float reflectionRoughness = 0.6f;
    /// ReSTIR DI (ADR-0044): pixels reuse the lights their neighbours chose, as well as their own past.
    bool restirSpatial = true;
    /// ReSTIR DI tests every sample it reuses for visibility, for comparisons with the path tracer: twice the
    /// rays.
    bool restirUnbiased = false;
    /// How indirect light is traced (ADR-0045).
    IndirectLightConfig indirect;
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
    /// Triangles of the drawn draws, once each.
    std::uint64_t triangles = 0;
    /// Analytic lights and emissive triangles that light (ADR-0044).
    std::uint32_t lights = 0;
    std::uint32_t particleSpawns = 0;
    /// Instances of the frame's acceleration structure, and how many skinned draws had one built.
    std::uint32_t rayInstances = 0;
    std::uint32_t skinnedStructures = 0;
    /// Paths per pixel the path tracer has averaged, with DebugView::Path.
    std::uint32_t pathSamples = 0;
    /// The eye's exposure, what the frame last metered (NaN before the first metering, or without
    /// auto-exposure), and the range the eye adapts within (ADR-0048).
    float ev100 = 0.0f;
    float meteredEv100 = 0.0f;
    float ev100Min = 0.0f;
    float ev100Max = 0.0f;
    /// The size the last frame was rendered at, and its scale of the output's (ADR-0049).
    std::uint32_t renderWidth = 0;
    std::uint32_t renderHeight = 0;
    float renderScale = 1.0f;
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

/// Records into the interface layer after the canvas, inside a rendering scope without depth.
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
    void SetExposureCompensation(float stops);
    void SetImage(const ImageConfig& config);
    void SetResolution(const ResolutionConfig& config);
    void SetNormalStrength(float strength);
    void SetIndirectLight(bool enabled);
    void SetDenoiser(bool enabled);
    void SetReflections(bool enabled);
    void SetReflectionRoughness(float roughness);
    /// See RendererConfig::restirSpatial and restirUnbiased.
    void SetRestir(bool spatial, bool unbiased);
    void SetIndirectLightConfig(const IndirectLightConfig& config);
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
    /// The last luminance histogram auto-exposure read back: weights by bin, from black to bright, in log2
    /// nits from -10 to 16 (ADR-0048). Empty without auto-exposure.
    [[nodiscard]] std::span<const std::uint32_t> ExposureHistogram() const;

    /// Draws one frame, following the window's size: the scene, then the canvas if there is one, then
    /// the overlay. A minimized window draws nothing. An error means that the device is unusable and the
    /// application should stop.
    [[nodiscard]] core::Result<void> Render(const SceneView& view, const ui::Canvas* canvas = nullptr,
                                            const OverlayRecorder& overlay = {});

    [[nodiscard]] rhi::Device& Device();
    /// The format of the layer that the interface and the overlay draw into: sRGB, whatever the display's.
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
