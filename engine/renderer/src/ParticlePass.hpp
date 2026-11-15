// Daidalos Engine Next
// Copyright (C) 2011-2026 StÃ©phane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/core/SlotPool.hpp>
#include <daidalos/renderer/Particles.hpp>
#include <daidalos/renderer/SceneView.hpp>
#include <daidalos/rhi/Device.hpp>

#include <glm/vec3.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_set>
#include <vector>

namespace daidalos::renderer {

class RenderGraph;

/// Particles simulated on the GPU (ADR-0025, ADR-0053): one pool of kMaxParticles, with a dead list and two alive
/// lists. Each frame spawns what the last frame's particles asked for, emits, simulates, sorts by depth and
/// draws them with one indirect draw. The CPU reads back only the impacts that effects report, two frames
/// late. The Device must outlive it.
class ParticlePass {
public:
    static core::Result<ParticlePass> Create(rhi::Device& device, rhi::Format colorFormat);

    ParticlePass(ParticlePass&& other) noexcept;
    ParticlePass& operator=(ParticlePass&& other) = delete;
    ParticlePass(const ParticlePass&) = delete;
    ParticlePass& operator=(const ParticlePass&) = delete;
    ~ParticlePass();

    [[nodiscard]] core::Result<ParticleEffectHandle> CreateEffect(const ParticleEffectDesc& desc);
    /// Changes an effect in place, as when its file is edited: particles alive take the change. False if
    /// the effect is gone.
    bool UpdateEffect(ParticleEffectHandle effect, const ParticleEffectDesc& desc);
    /// Particles of the effect still alive keep using its slot, which a new effect may reuse.
    void DestroyEffect(ParticleEffectHandle effect);

    /// Impacts a frame can report to the CPU; more are dropped.
    static constexpr std::uint32_t kMaxHits = 1024;

    /// What colliding particles meet and lit particles cast their shadow rays against (ADR-0044,
    /// ADR-0053): the frame's structure, built before, the storage indices of its instances and materials,
    /// and the skinned vertices that rays read, or null.
    struct Shadows {
        rhi::AccelerationStructureHandle structure;
        std::uint32_t instanceBuffer = 0;
        std::uint32_t materialBuffer = 0;
        rhi::BufferHandle skinnedVertices;
        /// The frame's emissive triangles, which light sampling reads.
        rhi::BufferHandle emissiveTriangles;
        /// Where the last frame's lights are in this frame's list (MeshPass::LightMapIndex): particles keep
        /// the light they chose.
        std::uint32_t lightMap = 0;
        std::uint32_t previousLightCount = 0;
    };
    /// Adds the passes that spawn the view's particles and those the last frame asked for, move every
    /// particle by the view's time step, in the level's wind, the view's wind volumes and attractors, against
    /// the frame's structure, and sort them, in the group "Simulation". frameBuffer is the storage index of
    /// the frame's FrameData. Spawns of destroyed effects are skipped. The simulation also lights lit
    /// particles, each keeping the light it chose, with its shadows (ADR-0044).
    [[nodiscard]] core::Result<void> AddSimulation(RenderGraph& graph, const SceneView& view, std::uint32_t frameBuffer,
                                                   const Shadows& shadows);

    /// An impact reported by an effect (ParticleHit), with the material as an index of the frame's table.
    struct Hit {
        glm::vec3 position{0.0f};
        glm::vec3 normal{0.0f};
        ParticleEffectHandle effect;
        std::uint32_t material = 0;
    };
    /// The impacts of the simulation recorded kFramesInFlight AddSimulation calls before the last, which the
    /// GPU has finished since: what the last call found.
    [[nodiscard]] std::span<const Hit> Hits() const { return m_hits; }
    /// The particles alive after that same simulation.
    [[nodiscard]] std::uint32_t AliveCount() const { return m_aliveCount; }

    /// What the particles leave in their second target, the reactive mask of temporal antialiasing
    /// (ADR-0037): how much they cover each pixel, where the history is not to be trusted.
    static constexpr rhi::Format kReactiveFormat = rhi::Format::R8Unorm;
    /// And in their third, how far distortion effects bend the frame behind them (ADR-0053).
    static constexpr rhi::Format kDistortionFormat = rhi::Format::Rg16Float;

    /// Adds the pass that draws the particles over target, as "Particles", their coverage into reactive and
    /// their distortion into distortion, which it clears, in the top-left corner of this area. They read
    /// depth themselves, to fade softly where they meet surfaces, and the frame's fog (FrameTargets::fog), if
    /// any, which veils them.
    void AddDraw(RenderGraph& graph, std::uint32_t frameBuffer, rhi::TextureHandle depth, rhi::TextureHandle target,
                 rhi::TextureHandle reactive, rhi::TextureHandle distortion, const glm::vec3& cameraRight,
                 const glm::vec3& cameraUp, std::optional<rhi::Extent> area = std::nullopt,
                 rhi::TextureHandle fog = {});

    /// Whether an effect bends the frame: only then is the distortion pass worth adding.
    [[nodiscard]] bool HasDistortion() const { return !m_distorting.empty(); }
    /// Adds the passes that copy target into background and draw it back bent by distortion, as "Particle
    /// distortion", in the top-left corner of this area.
    void AddDistortion(RenderGraph& graph, std::uint32_t frameBuffer, rhi::TextureHandle target,
                       rhi::TextureHandle background, rhi::TextureHandle distortion,
                       std::optional<rhi::Extent> area = std::nullopt);

    [[nodiscard]] std::size_t EffectCount() const { return m_effects.Size(); }

private:
    struct Effect {};
    /// Spawn requests, forces and parameters of one frame in flight, and the hits it reports.
    struct FrameSlot {
        rhi::BufferHandle spawns;
        std::uint64_t spawnCapacity = 0;
        rhi::BufferHandle forces;
        std::uint64_t forceCapacity = 0;
        rhi::BufferHandle parameters;
        rhi::BufferHandle hits;
        /// The GPU has written its hits: a simulation was recorded with this slot.
        bool used = false;
    };

    explicit ParticlePass(rhi::Device& device) : m_device(&device) {}

    [[nodiscard]] core::Result<void> CreateResources(rhi::Format colorFormat);
    /// Reads the hits the slot's last simulation reported.
    void ReadHits(const FrameSlot& slot);
    [[nodiscard]] core::Result<void> ReserveEffectSlot(std::uint32_t index);
    void WriteEffect(std::uint32_t index, const ParticleEffectDesc& desc);

    rhi::Device* m_device;
    rhi::PipelineHandle m_subEmit;
    rhi::PipelineHandle m_emit;
    rhi::PipelineHandle m_simulate;
    rhi::PipelineHandle m_finish;
    rhi::PipelineHandle m_sort;
    rhi::PipelineHandle m_draw;
    rhi::PipelineHandle m_distort;
    rhi::BufferHandle m_particles;
    rhi::BufferHandle m_dead;
    std::array<rhi::BufferHandle, 2> m_alive;
    /// What particles ask the next frame to spawn: one list is written while the other is spawned.
    std::array<rhi::BufferHandle, 2> m_subSpawns;
    rhi::BufferHandle m_keys;
    rhi::BufferHandle m_counters;
    rhi::BufferHandle m_arguments;
    /// Which of m_alive holds the particles alive now.
    std::uint32_t m_current = 0;
    core::SlotPool<Effect, ParticleEffectTag> m_effects;
    rhi::BufferHandle m_effectTable;
    rhi::BufferHandle m_curveTable;
    std::uint32_t m_effectCapacity = 0;
    std::array<FrameSlot, rhi::kFramesInFlight> m_frameSlots;
    std::uint32_t m_nextFrameSlot = 0;
    std::uint32_t m_frame = 0;
    std::vector<Hit> m_hits;
    std::uint32_t m_aliveCount = 0;
    /// The indices of the effects that bend the frame.
    std::unordered_set<std::uint32_t> m_distorting;
};

} // namespace daidalos::renderer
