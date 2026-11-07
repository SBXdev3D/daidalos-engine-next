// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/rhi/Device.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "MeshPass.hpp"

namespace daidalos::renderer {

class RenderGraph;

/// Instance masks: what rays see of a draw. A shadow ray queries with kMaskShadowCasters only.
inline constexpr std::uint8_t kMaskShadowCasters = 0x01;
inline constexpr std::uint8_t kMaskOthers = 0x02;

/// The frame's draws as rays see them (ADR-0033): a top-level acceleration structure built each frame
/// over the meshes' bottom-level structures, and a bottom-level structure per skinned draw, refitted each
/// frame to its skinned vertices. Also draws the debug view that traces it from the camera. The Device must
/// outlive it.
class RayTracingScene {
public:
    static core::Result<RayTracingScene> Create(rhi::Device& device, rhi::Format colorFormat);

    RayTracingScene(RayTracingScene&& other) noexcept;
    RayTracingScene& operator=(RayTracingScene&& other) = delete;
    RayTracingScene(const RayTracingScene&) = delete;
    RayTracingScene& operator=(const RayTracingScene&) = delete;
    ~RayTracingScene();

    /// Makes room for this many draws in the structure of a frame slot, and returns the index shaders trace
    /// it by, for FrameData. Call before MeshPass::Prepare.
    [[nodiscard]] core::Result<std::uint32_t> Reserve(std::uint32_t slot, std::size_t draws);
    /// Writes the instances of the frame and makes the skinned draws' structures. Call after
    /// MeshPass::Prepare, with its traced draws.
    [[nodiscard]] core::Result<void> Prepare(std::span<const TracedDraw> draws);
    /// Adds the passes that refit the skinned draws' structures and build the frame's top-level structure, in the
    /// group "Acceleration structures". They must come after MeshPass::AddSkinning and before any pass that
    /// traces rays.
    void AddBuild(RenderGraph& graph);
    /// Adds the debug view over target, as "Ray debug". frameBuffer and instanceBuffer are the storage
    /// indices of the frame's FrameData and instances.
    void AddDebugView(RenderGraph& graph, rhi::TextureHandle target, std::uint32_t frameBuffer,
                      std::uint32_t instanceBuffer);

    /// The structure built for the prepared frame, which passes that trace rays declare.
    [[nodiscard]] rhi::AccelerationStructureHandle Structure() const { return m_slots[m_slot].structure; }

    struct Stats {
        std::uint32_t instances = 0;
        std::uint32_t skinnedStructures = 0;
    };
    [[nodiscard]] const Stats& LastStats() const { return m_stats; }

private:
    struct Slot {
        rhi::AccelerationStructureHandle structure;
        rhi::BufferHandle instances;
        std::uint32_t capacity = 0;
    };
    /// A skinned draw's structure, refitted while the skinned draw in its position keeps the same geometry.
    struct SkinnedStructure {
        rhi::TriangleGeometry geometry;
        rhi::AccelerationStructureHandle structure;
    };

    explicit RayTracingScene(rhi::Device& device);

    rhi::Device* m_device;
    rhi::PipelineHandle m_debugPipeline;
    std::array<Slot, rhi::kFramesInFlight> m_slots;
    std::uint32_t m_slot = 0;
    std::vector<SkinnedStructure> m_skinned;
    /// What the build passes use, for the prepared frame.
    std::uint32_t m_instanceCount = 0;
    std::uint32_t m_skinnedCount = 0;
    std::vector<rhi::AccelerationStructureUse> m_bottomLevel;
    std::vector<rhi::AccelerationStructureHandle> m_skinnedHandles;
    std::vector<rhi::AccelerationStructureInstance> m_instances;
    Stats m_stats;
};

} // namespace daidalos::renderer
