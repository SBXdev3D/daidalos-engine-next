// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/core/SlotPool.hpp>
#include <daidalos/renderer/Particles.hpp>
#include <daidalos/rhi/Device.hpp>

#include <glm/vec3.hpp>

#include <array>
#include <cstdint>
#include <span>

namespace daidalos::renderer {

class RenderGraph;

/// Particles simulated on the GPU (ADR-0025): one pool of kMaxParticles, with a dead list and two alive
/// lists. Each frame emits, simulates, sorts by depth and draws them with one indirect draw; the CPU
/// never reads them back. The Device must outlive it.
class ParticlePass {
public:
    static core::Result<ParticlePass> Create(rhi::Device& device, rhi::Format colorFormat);

    ParticlePass(ParticlePass&& other) noexcept;
    ParticlePass& operator=(ParticlePass&& other) = delete;
    ParticlePass(const ParticlePass&) = delete;
    ParticlePass& operator=(const ParticlePass&) = delete;
    ~ParticlePass();

    [[nodiscard]] core::Result<ParticleEffectHandle> CreateEffect(const ParticleEffectDesc& desc);
    /// Particles of the effect still alive keep using its slot, which a new effect may reuse.
    void DestroyEffect(ParticleEffectHandle effect);

    /// Adds the passes that emit the spawns, move every particle by timeStep and sort them, in the group
    /// "Simulation". depth and normals come from the G-buffer; frameBuffer is the storage index of the
    /// frame's FrameData. Spawns of destroyed effects are skipped.
    [[nodiscard]] core::Result<void> AddSimulation(RenderGraph& graph, std::span<const ParticleSpawn> spawns,
                                                   float timeStep, std::uint32_t frameBuffer, rhi::TextureHandle depth,
                                                   rhi::TextureHandle normals);

    /// What the particles leave in their second target, the reactive mask of temporal antialiasing
    /// (ADR-0037): how much they cover each pixel, where the history is not to be trusted.
    static constexpr rhi::Format kReactiveFormat = rhi::Format::R8Unorm;

    /// What lit particles cast their shadow rays against (ADR-0044): the frame's structure, built before,
    /// the storage indices of its instances and materials, and the skinned vertices that rays read, or null.
    struct Shadows {
        rhi::AccelerationStructureHandle structure;
        std::uint32_t instanceBuffer = 0;
        std::uint32_t materialBuffer = 0;
        rhi::BufferHandle skinnedVertices;
        /// The frame's emissive triangles, which light sampling reads.
        rhi::BufferHandle emissiveTriangles;
    };
    /// Adds the pass that draws the particles over target, as "Particles", and their coverage into
    /// reactive, which it clears. They read depth themselves, to fade softly where they meet surfaces.
    void AddDraw(RenderGraph& graph, std::uint32_t frameBuffer, rhi::TextureHandle depth, rhi::TextureHandle target,
                 rhi::TextureHandle reactive, const glm::vec3& cameraRight, const glm::vec3& cameraUp,
                 const Shadows& shadows);

    [[nodiscard]] std::size_t EffectCount() const { return m_effects.Size(); }

private:
    struct Effect {};
    /// Spawn requests of one frame in flight.
    struct FrameSlot {
        rhi::BufferHandle spawns;
        std::uint64_t spawnCapacity = 0;
    };

    explicit ParticlePass(rhi::Device& device) : m_device(&device) {}

    [[nodiscard]] core::Result<void> CreateResources(rhi::Format colorFormat);
    [[nodiscard]] core::Result<void> ReserveEffectSlot(std::uint32_t index);

    rhi::Device* m_device;
    rhi::PipelineHandle m_emit;
    rhi::PipelineHandle m_simulate;
    rhi::PipelineHandle m_finish;
    rhi::PipelineHandle m_sort;
    rhi::PipelineHandle m_draw;
    rhi::BufferHandle m_particles;
    rhi::BufferHandle m_dead;
    std::array<rhi::BufferHandle, 2> m_alive;
    rhi::BufferHandle m_keys;
    rhi::BufferHandle m_counters;
    rhi::BufferHandle m_arguments;
    /// Which of m_alive holds the particles alive now.
    std::uint32_t m_current = 0;
    core::SlotPool<Effect, ParticleEffectTag> m_effects;
    rhi::BufferHandle m_effectTable;
    std::uint32_t m_effectCapacity = 0;
    std::array<FrameSlot, rhi::kFramesInFlight> m_frameSlots;
    std::uint32_t m_nextFrameSlot = 0;
    std::uint32_t m_frame = 0;
};

} // namespace daidalos::renderer
