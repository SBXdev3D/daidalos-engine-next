// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/rhi/Device.hpp>

#include <cstdint>

namespace daidalos::renderer {

class RenderGraph;

/// What global illumination reads each frame.
struct GlobalIlluminationInputs {
    /// Storage indices of the frame's FrameData, instances and material table.
    std::uint32_t frameBuffer = 0;
    std::uint32_t instanceBuffer = 0;
    std::uint32_t materialBuffer = 0;
    /// The frame's top-level structure, built before.
    rhi::AccelerationStructureHandle structure;
    /// The skinned vertices that rays read, or null.
    rhi::BufferHandle skinnedVertices;
    /// The frame's emissive triangles, which light sampling reads (MeshPass::EmissiveTriangles).
    rhi::BufferHandle emissiveTriangles;
    /// The pre-pass's depth and normals.
    rhi::TextureHandle depth;
    rhi::TextureHandle normals;
    /// Counts the frames: the paths change with it.
    std::uint32_t frameIndex = 0;
    /// Traces the reflection of each pixel's specular lobe (ADR-0042) into Reflections().
    bool reflections = true;
    /// Rougher surfaces trace no reflection: their indirect diffuse light stands in for it.
    float reflectionRoughness = 0.6f;
};

/// The engine's global illumination (ADR-0040, ADR-0042): a radiance cache, a hash grid of world space filled
/// by a few paths from the camera each frame, one ray per pixel that reads it where it hits, for the
/// indirect diffuse light, and one ray per pixel along its specular lobe, for its reflection. Light
/// bounces from frame to frame, through the cache, without limit. The Device must outlive it.
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
    void Reset() { m_reset = true; }
    /// What Output() holds.
    enum class Mode : std::uint32_t {
        /// The indirect diffuse light of each pixel, per unit of diffuse color.
        Indirect = 0,
        /// A debug view: the light the cache holds at each pixel's surface.
        Cache = 1,
    };
    /// Adds the passes that update the cache and trace the frame's rays, as "Global illumination", and the
    /// reflections, as "Reflections". Valid after Resize; the frame's structure must be built before.
    void AddTo(RenderGraph& graph, const GlobalIlluminationInputs& inputs, Mode mode = Mode::Indirect);

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

    rhi::Device* m_device;
    rhi::PipelineHandle m_update;
    rhi::PipelineHandle m_resolve;
    rhi::PipelineHandle m_trace;
    rhi::PipelineHandle m_reflect;
    rhi::BufferHandle m_keys;
    rhi::BufferHandle m_accumulation;
    rhi::BufferHandle m_radiance;
    rhi::BufferHandle m_ages;
    rhi::BufferHandle m_anchors;
    rhi::TextureHandle m_output;
    rhi::TextureHandle m_reflections;
    rhi::Extent m_extent;
    /// The cache's buffers start with whatever memory held: the first frame empties them.
    bool m_reset = true;
};

} // namespace daidalos::renderer
