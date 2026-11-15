// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/rhi/Device.hpp>

#include <array>
#include <cstdint>

namespace daidalos::renderer {

class RenderGraph;

/// What the fog passes read each frame.
struct VolumetricFogInputs {
    /// Storage indices of the frame's FrameData, instances and material table.
    std::uint32_t frameBuffer = 0;
    std::uint32_t instanceBuffer = 0;
    std::uint32_t materialBuffer = 0;
    /// The frame's top-level structure, built before, and the skinned vertices that rays read, or null.
    rhi::AccelerationStructureHandle structure;
    rhi::BufferHandle skinnedVertices;
    /// The frame's emissive triangles (MeshPass::EmissiveTriangles), written before.
    rhi::BufferHandle emissiveTriangles;
    /// Counts the frames: the points drawn in each froxel change with it.
    std::uint32_t frameIndex = 0;
};

/// The size of the froxel grid.
struct FroxelGrid {
    /// Pixels of the output across a froxel's side; the grid covers the view whatever the render scale.
    std::uint32_t tileSize = 16;
    /// Froxels in depth, from the near plane to the far plane.
    std::uint32_t slices = 64;

    bool operator==(const FroxelGrid&) const = default;
};

/// Volumetric fog (ADR-0051): a grid of froxels that follows the camera, sliced by depth exponentially. A
/// first compute pass draws a point in each froxel, takes the medium there, the level's fog and its boxes,
/// and the light it scatters towards the camera from one of the frame's lights, chosen by resampling, with
/// one shadow ray; it blends it with what the froxel held last frame. A second adds the froxels up from the
/// camera, so that each holds the fog in front of it: Output, which FrameTargets::fog names. The Device must
/// outlive it.
class VolumetricFog {
public:
    static core::Result<VolumetricFog> Create(rhi::Device& device);

    VolumetricFog(VolumetricFog&& other) noexcept;
    VolumetricFog& operator=(VolumetricFog&& other) = delete;
    VolumetricFog(const VolumetricFog&) = delete;
    VolumetricFog& operator=(const VolumetricFog&) = delete;
    ~VolumetricFog();

    /// Follows the size of the output. A new size or grid drops the history.
    [[nodiscard]] core::Result<void> Resize(rhi::Extent output, const FroxelGrid& grid = {});
    /// Drops the history: the next frame reuses nothing from the past.
    void Reset() { m_hasHistory = false; }
    /// Adds the passes as "Volumetric fog". Valid after Resize; the frame's structure must be built before.
    void AddTo(RenderGraph& graph, const VolumetricFogInputs& inputs);

    /// The fog in front of each froxel: the light scattered towards the camera, and in alpha the fraction of
    /// the light from behind that crosses it (kFormat).
    [[nodiscard]] rhi::TextureHandle Output() const { return m_integrated; }
    /// Froxels across, down and in depth.
    [[nodiscard]] std::array<std::uint32_t, 3> Size() const { return {m_size.width, m_size.height, m_slices}; }

    static constexpr rhi::Format kFormat = rhi::Format::Rgba16Float;
    /// The share of the last frame in each froxel's light: the noise of one ray per froxel averages over
    /// about twenty frames.
    static constexpr float kHistoryWeight = 0.95f;

private:
    explicit VolumetricFog(rhi::Device& device) : m_device(&device) {}

    void DestroyResources();

    rhi::Device* m_device;
    rhi::PipelineHandle m_lighting;
    rhi::PipelineHandle m_integrate;
    /// The light each froxel scatters, per meter, and its extinction: this frame's and the last.
    std::array<rhi::TextureHandle, 2> m_scattering;
    rhi::TextureHandle m_integrated;
    std::uint32_t m_current = 0;
    rhi::Extent m_output;
    FroxelGrid m_grid;
    rhi::Extent m_size;
    std::uint32_t m_slices = 0;
    bool m_hasHistory = false;
};

} // namespace daidalos::renderer
