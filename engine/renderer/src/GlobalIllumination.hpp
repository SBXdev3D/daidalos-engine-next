// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/rhi/Device.hpp>

#include <array>
#include <cstdint>
#include <optional>

namespace daidalos::renderer {

class RenderGraph;

/// How the indirect diffuse light is traced (ADR-0045).
struct GlobalIlluminationSettings {
    /// Surfaces lit by the frame's lights before the cache is read, along each pixel's ray, from 0 to 3. 0
    /// reads the cache where the ray first hits.
    std::uint32_t bounces = 1;
    /// Lights weighed at each of those surfaces before one is kept, from 1 to 8.
    std::uint32_t lightCandidates = 4;
    /// The luminance bounced light is clamped to, pre-exposed; 0 for none. Direct light is never clamped.
    float clamp = 16.0f;
    /// Rays per pixel, from 1 to 16, averaged.
    std::uint32_t rays = 1;
    /// Rays per pixel, from rays to 16, where the denoiser has little history of the surface (ADR-0121): it
    /// just came into view, so its light is one frame's, which the denoiser can only blur.
    std::uint32_t newRays = 1;
    /// Pixels per traced pixel (ADR-0093): 1 traces every pixel; 2 half of them, in a checkerboard; 4 one of
    /// each two by two. They take turns, and the others take their traced neighbours' light.
    std::uint32_t pixelsPerRay = 1;
    /// Pixels per reflection ray on rough surfaces (ADR-0096): 1 traces every pixel; 2 half of them, in a
    /// checkerboard that takes turns, the others brought forward from last frame or taken from their neighbours.
    /// Sharp reflections trace every pixel.
    std::uint32_t reflectionPixelsPerRay = 1;
};

/// What global illumination reads each frame.
struct GiPushConstants;

struct GlobalIlluminationInputs {
    /// The pixels the frame draws, in the top-left corner of the targets (ADR-0049); Resize's size when zero.
    rhi::Extent extent;
    /// Storage indices of the frame's FrameData, instances and material table.
    std::uint32_t frameBuffer = 0;
    std::uint32_t instanceBuffer = 0;
    std::uint32_t materialBuffer = 0;
    /// The frame's top-level structure, built before.
    rhi::AccelerationStructureHandle structure;
    /// The skinned vertices that rays read, or null.
    rhi::BufferHandle skinnedVertices;
    /// Which decals and wet volumes touch each cell (ADR-0094), which ray hits read, or null.
    rhi::BufferHandle materialGrid;
    /// The frame's emissive triangles, which light sampling reads (MeshPass::EmissiveTriangles).
    rhi::BufferHandle emissiveTriangles;
    /// The sources that light each cell most, which light sampling reads (MeshPass::LightGrid), or null.
    rhi::BufferHandle lightGrid;
    /// The G-buffer's depth and normals.
    rhi::TextureHandle depth;
    rhi::TextureHandle normals;
    /// The denoiser's guides of last frame (Denoiser::PreviousGuides), where pixels whose surface it barely
    /// knows get settings.newRays rays, found through the G-buffer's motion vectors. Null: every pixel gets
    /// settings.rays.
    rhi::TextureHandle denoiserGuides;
    rhi::TextureHandle motion;
    /// Counts the frames: the paths change with it.
    std::uint32_t frameIndex = 0;
    /// Traces the reflection of each pixel's specular lobe (ADR-0042) into Reflections().
    bool reflections = true;
    /// Rougher surfaces trace no reflection: their indirect diffuse light stands in for it.
    float reflectionRoughness = 0.6f;
    GlobalIlluminationSettings settings;
};

/// The engine's global illumination (ADR-0040, ADR-0042, ADR-0045): a radiance cache, a hash grid of world
/// space filled by a few paths from the camera each frame; for the indirect diffuse light, a ray per pixel
/// whose hit is lit by the frame's lights, for a few bounces, before the cache is read; and one ray per pixel
/// along its specular lobe, for its reflection. Light bounces from frame to frame, through the cache, without
/// limit. The Device must outlive it.
class GlobalIllumination {
public:
    static core::Result<GlobalIllumination> Create(rhi::Device& device);

    GlobalIllumination(GlobalIllumination&& other) noexcept;
    GlobalIllumination& operator=(GlobalIllumination&& other) = delete;
    GlobalIllumination(const GlobalIllumination&) = delete;
    GlobalIllumination& operator=(const GlobalIllumination&) = delete;
    ~GlobalIllumination();

    /// Follows the size of the frame.
    [[nodiscard]] core::Result<void> Resize(rhi::Extent extent);
    /// Empties the cache, when the next frame is added: after a change of scene, nothing is left from the last.
    void Reset() {
        m_reset = true;
        m_historyFrame.reset();
    }
    /// What Output() holds.
    enum class Mode : std::uint32_t {
        /// The indirect diffuse light of each pixel, per unit of diffuse color.
        Indirect = 0,
        /// A debug view: the light the cache holds at each pixel's surface.
        Cache = 1,
    };
    /// Adds the passes that update the cache, as "Radiance cache", that trace the frame's rays and fill the
    /// pixels that traced none (GlobalIlluminationSettings::pixelsPerRay), as "Indirect light", and the reflections, as
    /// "Reflections". Valid after Resize; the frame's structure must be built before.
    void AddTo(RenderGraph& graph, const GlobalIlluminationInputs& inputs, Mode mode = Mode::Indirect);
    /// Adds the cache's passes now, as async passes (ADR-0095), which the next AddTo then leaves out. The
    /// cache reads no G-buffer: it can run on the compute queue beside it.
    void AddCacheUpdate(RenderGraph& graph, const GlobalIlluminationInputs& inputs, Mode mode = Mode::Indirect);

    /// Output of the last frame added (kOutputFormat). Valid after Resize; its index does not change until
    /// the next Resize.
    [[nodiscard]] rhi::TextureHandle Output() const { return m_output; }
    /// The light each pixel's specular lobe reflects, from the last frame added with reflections
    /// (kOutputFormat), and the distance to what it reflects in alpha: 0 where the indirect diffuse light
    /// stands in for it. Valid after Resize.
    [[nodiscard]] rhi::TextureHandle Reflections() const { return m_reflections; }

    static constexpr rhi::Format kOutputFormat = rhi::Format::Rgba16Float;
    /// Cells of the cache: matches kCacheCapacity in shaders/GlobalIllumination.slangh.
    static constexpr std::uint32_t kCacheCapacity = 1U << 20U;
    /// One update path per kPathSpacing by kPathSpacing pixels.
    static constexpr std::uint32_t kPathSpacing = 4;
    /// Smoother surfaces trace sharp reflections, which shade what they find: matches kSharpRoughness in
    /// shaders/GiReflect.slang.
    static constexpr float kSharpRoughness = 0.3f;

private:
    explicit GlobalIllumination(rhi::Device& device) : m_device(&device) {}
    [[nodiscard]] GiPushConstants PushConstants(const GlobalIlluminationInputs& inputs, Mode mode) const;
    void AddCache(RenderGraph& graph, const GlobalIlluminationInputs& inputs, Mode mode, bool async);

    rhi::Device* m_device;
    rhi::PipelineHandle m_update;
    rhi::PipelineHandle m_resolve;
    rhi::PipelineHandle m_trace;
    rhi::PipelineHandle m_fill;
    rhi::PipelineHandle m_reflect;
    rhi::PipelineHandle m_reflectFill;
    rhi::BufferHandle m_keys;
    rhi::BufferHandle m_accumulation;
    rhi::BufferHandle m_radiance;
    rhi::BufferHandle m_ages;
    rhi::BufferHandle m_anchors;
    rhi::TextureHandle m_output;
    rhi::TextureHandle m_reflections;
    /// Each pixel's last sample of indirect light, which the fill brings forward (ADR-0093): this frame's and
    /// last frame's, in turns.
    std::array<rhi::TextureHandle, 2> m_history{};
    rhi::Extent m_extent;
    /// The cache's buffers start with whatever memory held: the first frame empties them.
    bool m_reset = true;
    /// The frame that last wrote the history: the next one may read it.
    std::optional<std::uint32_t> m_historyFrame;
    /// Each pixel's reflection on rough surfaces, which their fill brings forward (ADR-0096), in turns, and the
    /// frame that last wrote it.
    std::array<rhi::TextureHandle, 2> m_reflectionHistory{};
    std::optional<std::uint32_t> m_reflectionHistoryFrame;
    /// AddCacheUpdate added the cache's passes of the frame being built.
    bool m_cacheAdded = false;
};

} // namespace daidalos::renderer
