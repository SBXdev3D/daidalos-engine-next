// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/rhi/Device.hpp>

#include <array>
#include <cstdint>

namespace daidalos::renderer {

class RenderGraph;

/// What the lighting pass reads and writes each frame.
struct DirectLightingInputs {
    /// Storage indices of the frame's FrameData, instances and material table.
    std::uint32_t frameBuffer = 0;
    std::uint32_t instanceBuffer = 0;
    std::uint32_t materialBuffer = 0;
    /// The frame's top-level structure, built before, and the skinned vertices that rays read, or null.
    rhi::AccelerationStructureHandle structure;
    rhi::BufferHandle skinnedVertices;
    /// The frame's emissive triangles (MeshPass::EmissiveTriangles), written before.
    rhi::BufferHandle emissiveTriangles;
    /// The G-buffer.
    rhi::TextureHandle depth;
    rhi::TextureHandle normals;
    rhi::TextureHandle motion;
    rhi::TextureHandle baseColor;
    rhi::TextureHandle emission;
    /// The indirect light and the reflections that the lighting adds, which FrameData names; null for none.
    rhi::TextureHandle indirect;
    rhi::TextureHandle reflections;
    /// Storage images the lighting writes (MeshPass::kLightFormat).
    rhi::TextureHandle diffuse;
    rhi::TextureHandle specular;
    /// Where the last frame's lights are in this frame's list (MeshPass::LightMapIndex).
    std::uint32_t lightMap = 0;
    std::uint32_t previousLightCount = 0;
    /// Counts the frames: the samples change with it.
    std::uint32_t frameIndex = 0;
};

/// How ReSTIR DI reuses its samples.
struct DirectLightingSettings {
    /// Neighbours are reused, as well as the past.
    bool spatial = true;
    /// Samples are tested for visibility wherever they are weighed, for comparisons with the path tracer:
    /// twice the rays.
    bool unbiased = false;
    /// Each pixel's chosen light is also written to DebugOutput, one hue per light.
    bool debug = false;
};

/// The light of the frame's lights at each pixel of the G-buffer (ADR-0044): ReSTIR DI (Bitterli et al.
/// 2020) in four compute passes, candidates, temporal reuse, spatial reuse and shading, with one shadow ray
/// per pixel that reaches the image, whatever the number of lights. It adds the indirect light and the
/// reflections, and writes the light without the surface's colors, diffuse and specular, for the denoiser.
/// The Device must outlive it.
class DirectLighting {
public:
    static core::Result<DirectLighting> Create(rhi::Device& device);

    DirectLighting(DirectLighting&& other) noexcept;
    DirectLighting& operator=(DirectLighting&& other) = delete;
    DirectLighting(const DirectLighting&) = delete;
    DirectLighting& operator=(const DirectLighting&) = delete;
    ~DirectLighting();

    /// Follows the size of the frame. A new size drops the history.
    [[nodiscard]] core::Result<void> Resize(rhi::Extent extent);
    /// Drops the history: the next frame reuses nothing from the past.
    void Reset() { m_hasHistory = false; }
    /// Adds the passes as "Direct lighting". Valid after Resize; the frame's structure must be built before.
    void AddTo(RenderGraph& graph, const DirectLightingInputs& inputs, const DirectLightingSettings& settings);

    /// Each pixel's chosen light, one hue per light, when the last frame was added with debug (kDebugFormat).
    [[nodiscard]] rhi::TextureHandle DebugOutput() const { return m_debug; }

    static constexpr rhi::Format kDebugFormat = rhi::Format::Rgba16Float;
    /// Bytes per pixel of a reservoir: a light, a point on it, a weight and a confidence.
    static constexpr std::uint64_t kReservoirSize = 16;

private:
    explicit DirectLighting(rhi::Device& device) : m_device(&device) {}

    void DestroyResources();

    rhi::Device* m_device;
    rhi::PipelineHandle m_candidates;
    rhi::PipelineHandle m_temporal;
    rhi::PipelineHandle m_spatial;
    rhi::PipelineHandle m_shade;
    /// The reservoirs of the candidates and temporal passes, and those the spatial pass makes of them, which
    /// the shading uses and the next frame's temporal pass reads.
    rhi::BufferHandle m_reservoirs;
    rhi::BufferHandle m_finalReservoirs;
    /// Each pixel's normal and linear depth; this frame's and the last.
    std::array<rhi::TextureHandle, 2> m_guides;
    rhi::TextureHandle m_debug;
    std::uint32_t m_current = 0;
    rhi::Extent m_extent;
    bool m_hasHistory = false;
};

} // namespace daidalos::renderer
