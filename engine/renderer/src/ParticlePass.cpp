// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "ParticlePass.hpp"

#include <daidalos/core/Profile.hpp>
#include <daidalos/rhi/Spirv.hpp>

#include <glm/geometric.hpp>
#include <glm/vec4.hpp>
#include <shaders/ParticleDraw.ps.spv.h>
#include <shaders/ParticleDraw.vs.spv.h>
#include <shaders/ParticleEmit.cs.spv.h>
#include <shaders/ParticleFinish.cs.spv.h>
#include <shaders/ParticleSimulate.cs.spv.h>
#include <shaders/ParticleSort.cs.spv.h>

#include <algorithm>
#include <bit>
#include <cstring>
#include <numeric>
#include <utility>
#include <vector>

#include "RenderGraph.hpp"

namespace daidalos::renderer {

namespace {

// These structures match their twins in shaders/Particles.slangh.
struct GpuParticle {
    glm::vec3 position;
    float age;
    glm::vec3 velocity;
    float lifetime;
    float size;
    float rotation;
    float rotationSpeed;
    std::uint32_t effect;
    glm::vec3 light;
    float lightSamples;
    std::array<std::uint32_t, 4> reservoir;
};
static_assert(sizeof(GpuParticle) == 80);

constexpr std::uint32_t kEffectLit = 1;
constexpr std::uint32_t kEffectCollides = 2;
constexpr std::uint32_t kEffectAdditive = 4;

struct GpuEffect {
    glm::vec4 startColor;
    glm::vec4 endColor;
    glm::vec3 spawnBox;
    float spawnRadius;
    glm::vec3 direction;
    float spread;
    float speedMin;
    float speedMax;
    float lifeMin;
    float lifeMax;
    float sizeMin;
    float sizeMax;
    float growth;
    float gravity;
    float drag;
    float rotationSpeed;
    float softness;
    float bounce;
    std::uint32_t texture;
    std::uint32_t cell;
    std::uint32_t columns;
    std::uint32_t flags;
};
static_assert(sizeof(GpuEffect) == 128);

struct GpuSpawn {
    glm::vec3 position;
    std::uint32_t effect;
    glm::vec4 rotation;
    std::uint32_t first;
    std::uint32_t count;
    std::array<std::uint32_t, 2> padding;
};
static_assert(sizeof(GpuSpawn) == 48);

struct ParticlePushConstants {
    std::uint32_t particles;
    std::uint32_t dead;
    std::uint32_t aliveIn;
    std::uint32_t aliveOut;
    std::uint32_t keys;
    std::uint32_t counters;
    std::uint32_t arguments;
    std::uint32_t effects;
    std::uint32_t spawns;
    std::uint32_t spawnCount;
    std::uint32_t emitCount;
    std::uint32_t frameBuffer;
    float timeStep;
    std::uint32_t seed;
    std::uint32_t depthTexture;
    std::uint32_t normalTexture;
    std::uint32_t sortBlock;
    std::uint32_t sortStep;
    std::uint32_t capacity;
    std::uint32_t instanceBuffer;
    glm::vec3 cameraRight;
    std::uint32_t materialBuffer;
    glm::vec3 cameraUp;
    float padding3;
    std::uint32_t lightMap;
    std::uint32_t previousLightCount;
};
static_assert(sizeof(ParticlePushConstants) <= rhi::kMaxPushConstantSize);

constexpr std::uint32_t kGroupSize = 64;
constexpr std::uint32_t kGroups = (kMaxParticles + kGroupSize - 1) / kGroupSize;
constexpr std::uint32_t kInitialEffectCapacity = 16;
constexpr std::uint64_t kInitialSpawnCapacity = 64;
// The sort's groups, as in ParticleSort.slang: each sorts kSortLocal keys in its own memory.
constexpr std::uint32_t kSortThreads = 1024;
constexpr std::uint32_t kSortLocal = 2 * kSortThreads;
static_assert(std::has_single_bit(kMaxParticles) && kMaxParticles >= kSortLocal,
              "The bitonic sort needs a power of two, of whole groups");

} // namespace

ParticlePass::ParticlePass(ParticlePass&& other) noexcept
    : m_device(other.m_device), m_emit(std::exchange(other.m_emit, {})),
      m_simulate(std::exchange(other.m_simulate, {})), m_finish(std::exchange(other.m_finish, {})),
      m_sort(std::exchange(other.m_sort, {})), m_draw(std::exchange(other.m_draw, {})),
      m_particles(std::exchange(other.m_particles, {})), m_dead(std::exchange(other.m_dead, {})),
      m_alive(std::exchange(other.m_alive, {})), m_keys(std::exchange(other.m_keys, {})),
      m_counters(std::exchange(other.m_counters, {})), m_arguments(std::exchange(other.m_arguments, {})),
      m_current(other.m_current), m_effects(std::exchange(other.m_effects, {})),
      m_effectTable(std::exchange(other.m_effectTable, {})), m_effectCapacity(std::exchange(other.m_effectCapacity, 0)),
      m_frameSlots(std::exchange(other.m_frameSlots, {})), m_nextFrameSlot(other.m_nextFrameSlot),
      m_frame(other.m_frame) {}

ParticlePass::~ParticlePass() {
    for (const rhi::PipelineHandle pipeline : {m_emit, m_simulate, m_finish, m_sort, m_draw}) {
        m_device->DestroyPipeline(pipeline);
    }
    for (const rhi::BufferHandle buffer :
         {m_particles, m_dead, m_alive[0], m_alive[1], m_keys, m_counters, m_arguments, m_effectTable}) {
        m_device->DestroyBuffer(buffer);
    }
    for (const FrameSlot& slot : m_frameSlots) {
        m_device->DestroyBuffer(slot.spawns);
    }
}

core::Result<ParticlePass> ParticlePass::Create(rhi::Device& device, rhi::Format colorFormat) {
    ParticlePass pass(device);
    DAI_TRY(pass.CreateResources(colorFormat));
    return pass;
}

core::Result<void> ParticlePass::CreateResources(rhi::Format colorFormat) {
    const auto compute = [this](std::span<const std::uint32_t> shader, const char* name,
                                rhi::PipelineHandle& pipeline) -> core::Result<void> {
        DAI_TRY_ASSIGN(pipeline, m_device->CreateComputePipeline({.shader = shader, .name = name}));
        return {};
    };
    DAI_TRY(compute(rhi::ToSpirv(kParticleEmitCsSpirv), "Particle emission", m_emit));
    DAI_TRY(compute(rhi::ToSpirv(kParticleSimulateCsSpirv), "Particle simulation", m_simulate));
    DAI_TRY(compute(rhi::ToSpirv(kParticleFinishCsSpirv), "Particle finish", m_finish));
    DAI_TRY(compute(rhi::ToSpirv(kParticleSortCsSpirv), "Particle sort", m_sort));
    DAI_TRY_ASSIGN(m_draw, m_device->CreatePipeline({
                               .vertexShader = rhi::ToSpirv(kParticleDrawVsSpirv),
                               .fragmentShader = rhi::ToSpirv(kParticleDrawPsSpirv),
                               .colorFormat = colorFormat,
                               .moreColorFormats = {kReactiveFormat},
                               .cull = rhi::CullMode::None,
                               .blend = rhi::BlendMode::Premultiplied,
                               .name = "Particles",
                           }));

    const auto buffer = [this](std::uint64_t size, rhi::BufferUsage usage, const char* name,
                               rhi::BufferHandle& handle) -> core::Result<void> {
        DAI_TRY_ASSIGN(handle, m_device->CreateBuffer({.size = size, .usage = usage, .name = name}));
        return {};
    };
    constexpr std::uint64_t kListSize = std::uint64_t{kMaxParticles} * sizeof(std::uint32_t);
    DAI_TRY(buffer(std::uint64_t{kMaxParticles} * sizeof(GpuParticle), rhi::BufferUsage::Storage, "Particles",
                   m_particles));
    DAI_TRY(buffer(kListSize, rhi::BufferUsage::Storage | rhi::BufferUsage::TransferDst, "Dead particles", m_dead));
    DAI_TRY(buffer(kListSize, rhi::BufferUsage::Storage, "Alive particles", m_alive[0]));
    DAI_TRY(buffer(kListSize, rhi::BufferUsage::Storage, "Alive particles", m_alive[1]));
    DAI_TRY(buffer(std::uint64_t{kMaxParticles} * 2 * sizeof(std::uint32_t),
                   rhi::BufferUsage::Storage | rhi::BufferUsage::TransferDst, "Particle sort keys", m_keys));
    DAI_TRY(buffer(4 * sizeof(std::uint32_t), rhi::BufferUsage::Storage | rhi::BufferUsage::TransferDst,
                   "Particle counters", m_counters));
    DAI_TRY(buffer(4 * sizeof(std::uint32_t),
                   rhi::BufferUsage::Storage | rhi::BufferUsage::Indirect | rhi::BufferUsage::TransferDst,
                   "Particle draw", m_arguments));

    // Every slot starts dead, nothing is alive, and nothing is drawn.
    std::vector<std::uint32_t> dead(kMaxParticles);
    std::iota(dead.begin(), dead.end(), 0U);
    DAI_TRY(m_device->UploadBuffer(m_dead, std::as_bytes(std::span(dead))));
    const std::array<std::uint32_t, 4> counters{kMaxParticles, 0, 0, 0};
    DAI_TRY(m_device->UploadBuffer(m_counters, std::as_bytes(std::span(counters))));
    const std::array<std::uint32_t, 4> arguments{6, 0, 0, 0};
    DAI_TRY(m_device->UploadBuffer(m_arguments, std::as_bytes(std::span(arguments))));
    const std::vector<std::uint32_t> keys(std::size_t{kMaxParticles} * 2, 0);
    DAI_TRY(m_device->UploadBuffer(m_keys, std::as_bytes(std::span(keys))));
    return ReserveEffectSlot(0);
}

core::Result<void> ParticlePass::ReserveEffectSlot(std::uint32_t index) {
    if (index < m_effectCapacity) {
        return {};
    }
    const std::uint32_t capacity = std::max({kInitialEffectCapacity, m_effectCapacity * 2, index + 1});
    DAI_TRY_ASSIGN(rhi::BufferHandle table, m_device->CreateBuffer({.size = capacity * sizeof(GpuEffect),
                                                                    .usage = rhi::BufferUsage::Storage,
                                                                    .memory = rhi::MemoryLocation::CpuToGpu,
                                                                    .name = "Particle effects"}));
    if (m_effectTable) {
        // Frames in flight keep reading the old table, which goes away after them.
        std::memcpy(m_device->Mapped(table).data(), m_device->Mapped(m_effectTable).data(),
                    m_effectCapacity * sizeof(GpuEffect));
        m_device->DestroyBuffer(m_effectTable);
    }
    m_effectTable = table;
    m_effectCapacity = capacity;
    return {};
}

core::Result<ParticleEffectHandle> ParticlePass::CreateEffect(const ParticleEffectDesc& desc) {
    DAI_ASSERT(desc.texture, "A particle effect needs a texture");
    DAI_ASSERT(desc.columns > 0 && desc.cell < desc.columns * desc.columns, "The cell must be in the grid");
    const std::uint32_t flags =
        (desc.lit ? kEffectLit : 0U) | (desc.collides ? kEffectCollides : 0U) | (desc.additive ? kEffectAdditive : 0U);
    const GpuEffect data{desc.startColor,
                         desc.endColor,
                         desc.spawnBox,
                         desc.spawnRadius,
                         glm::length(desc.direction) > 0.0f ? glm::normalize(desc.direction) : glm::vec3(0, 1, 0),
                         desc.spread,
                         desc.speed.x,
                         desc.speed.y,
                         desc.lifetime.x,
                         desc.lifetime.y,
                         desc.size.x,
                         desc.size.y,
                         desc.growth,
                         desc.gravity,
                         desc.drag,
                         desc.rotationSpeed,
                         desc.softness,
                         desc.bounce,
                         m_device->BindlessIndex(desc.texture),
                         desc.cell,
                         desc.columns,
                         flags};
    const ParticleEffectHandle effect = m_effects.Insert({});
    if (core::Result<void> reserved = ReserveEffectSlot(effect.Index()); !reserved) {
        m_effects.Remove(effect);
        return tl::unexpected(std::move(reserved.error()));
    }
    std::memcpy(m_device->Mapped(m_effectTable).data() + std::size_t{effect.Index()} * sizeof(GpuEffect), &data,
                sizeof(data));
    return effect;
}

void ParticlePass::DestroyEffect(ParticleEffectHandle effect) {
    m_effects.Remove(effect);
}

core::Result<void> ParticlePass::AddSimulation(RenderGraph& graph, std::span<const ParticleSpawn> spawns,
                                               float timeStep, std::uint32_t frameBuffer, rhi::TextureHandle depth,
                                               rhi::TextureHandle normals, const Shadows& shadows) {
    DAI_PROFILE_SCOPE("renderer::ParticlePass::AddSimulation");
    FrameSlot& slot = m_frameSlots[m_nextFrameSlot];
    m_nextFrameSlot = (m_nextFrameSlot + 1) % rhi::kFramesInFlight;

    // The requests of the frame, each with the number of particles before it. Beyond the pool's size,
    // the GPU drops them anyway.
    std::vector<GpuSpawn> requests;
    std::uint32_t emitted = 0;
    for (const ParticleSpawn& spawn : spawns) {
        if (spawn.count == 0 || m_effects.TryGet(spawn.effect) == nullptr || emitted >= kMaxParticles) {
            continue;
        }
        const std::uint32_t count = std::min(spawn.count, kMaxParticles - emitted);
        requests.push_back({spawn.position,
                            spawn.effect.Index(),
                            glm::vec4(spawn.rotation.x, spawn.rotation.y, spawn.rotation.z, spawn.rotation.w),
                            emitted,
                            count,
                            {}});
        emitted += count;
    }
    if (slot.spawnCapacity < std::max<std::size_t>(requests.size(), 1)) {
        const std::uint64_t capacity =
            std::max<std::uint64_t>({kInitialSpawnCapacity, requests.size(), slot.spawnCapacity * 2});
        DAI_TRY_ASSIGN(rhi::BufferHandle created, m_device->CreateBuffer({.size = capacity * sizeof(GpuSpawn),
                                                                          .usage = rhi::BufferUsage::Storage,
                                                                          .memory = rhi::MemoryLocation::CpuToGpu,
                                                                          .name = "Particle spawns"}));
        m_device->DestroyBuffer(slot.spawns);
        slot.spawns = created;
        slot.spawnCapacity = capacity;
    }
    if (!requests.empty()) {
        std::memcpy(m_device->Mapped(slot.spawns).data(), requests.data(), requests.size() * sizeof(GpuSpawn));
    }

    const std::uint32_t next = 1 - m_current;
    ParticlePushConstants push{};
    push.particles = m_device->StorageIndex(m_particles);
    push.dead = m_device->StorageIndex(m_dead);
    push.aliveIn = m_device->StorageIndex(m_alive[m_current]);
    push.aliveOut = m_device->StorageIndex(m_alive[next]);
    push.keys = m_device->StorageIndex(m_keys);
    push.counters = m_device->StorageIndex(m_counters);
    push.arguments = m_device->StorageIndex(m_arguments);
    push.effects = m_device->StorageIndex(m_effectTable);
    push.spawns = m_device->StorageIndex(slot.spawns);
    push.spawnCount = static_cast<std::uint32_t>(requests.size());
    push.emitCount = emitted;
    push.frameBuffer = frameBuffer;
    push.timeStep = std::max(timeStep, 0.0f);
    push.seed = ++m_frame;
    push.depthTexture = m_device->BindlessIndex(depth);
    push.normalTexture = m_device->BindlessIndex(normals);
    push.capacity = kMaxParticles;
    push.instanceBuffer = shadows.instanceBuffer;
    push.materialBuffer = shadows.materialBuffer;
    push.lightMap = shadows.lightMap;
    push.previousLightCount = shadows.previousLightCount;

    // Emission, simulation and finish read and write most of the pool's buffers.
    const std::vector<rhi::BufferUse> pool{{m_particles, rhi::Access::ComputeShaderWrite},
                                           {m_dead, rhi::Access::ComputeShaderWrite},
                                           {m_alive[m_current], rhi::Access::ComputeShaderWrite},
                                           {m_alive[next], rhi::Access::ComputeShaderWrite},
                                           {m_keys, rhi::Access::ComputeShaderWrite},
                                           {m_counters, rhi::Access::ComputeShaderWrite},
                                           {m_arguments, rhi::Access::ComputeShaderWrite}};
    const auto dispatch = [push](rhi::PipelineHandle pipeline, std::uint32_t groups) {
        return [push, pipeline, groups](rhi::CommandList& commands) {
            commands.BindPipeline(pipeline);
            commands.Push(push);
            commands.Dispatch(groups);
        };
    };
    if (emitted > 0) {
        graph.AddPass({.name = "Particle emission", .group = "Simulation", .buffers = pool},
                      dispatch(m_emit, (emitted + kGroupSize - 1) / kGroupSize));
    }
    // Particles bounce off the G-buffer's surfaces, and lit ones cast shadow rays.
    std::vector<rhi::BufferUse> simulated = pool;
    simulated.push_back({shadows.skinnedVertices, rhi::Access::ComputeShaderRead});
    simulated.push_back({shadows.emissiveTriangles, rhi::Access::ComputeShaderRead});
    graph.AddPass({.name = "Particle simulation",
                   .group = "Simulation",
                   .textures = {{depth, rhi::Access::ComputeShaderRead}, {normals, rhi::Access::ComputeShaderRead}},
                   .buffers = std::move(simulated),
                   .structures = {{shadows.structure, rhi::Access::RayQueryRead}}},
                  dispatch(m_simulate, kGroups));
    graph.AddPass({.name = "Particle finish", .group = "Simulation", .buffers = pool}, dispatch(m_finish, kGroups));

    // Bitonic sort over the whole pool: the slots past the alive particles sort last. Groups sort
    // kSortLocal keys in their own memory; only the longer steps need a dispatch each.
    const auto sort = [&](std::uint32_t block, std::uint32_t step) {
        ParticlePushConstants sortPush = push;
        sortPush.sortBlock = block;
        sortPush.sortStep = step;
        const std::uint32_t groups = step == 0 ? kMaxParticles / kSortLocal : kMaxParticles / 2 / kSortThreads;
        graph.AddPass(
            {.name = "Particle sort", .group = "Simulation", .buffers = {{m_keys, rhi::Access::ComputeShaderWrite}}},
            [this, sortPush, groups](rhi::CommandList& commands) {
                commands.BindPipeline(m_sort);
                commands.Push(sortPush);
                commands.Dispatch(groups);
            });
    };
    sort(std::min(kSortLocal, kMaxParticles), 0);
    for (std::uint32_t block = kSortLocal * 2; block <= kMaxParticles; block *= 2) {
        for (std::uint32_t step = block / 2; step >= kSortLocal; step /= 2) {
            sort(block, step);
        }
        sort(block, 0);
    }
    m_current = next;
    return {};
}

void ParticlePass::AddDraw(RenderGraph& graph, std::uint32_t frameBuffer, rhi::TextureHandle depth,
                           rhi::TextureHandle target, rhi::TextureHandle reactive, const glm::vec3& cameraRight,
                           const glm::vec3& cameraUp, std::optional<rhi::Extent> area) {
    ParticlePushConstants push{};
    push.particles = m_device->StorageIndex(m_particles);
    push.keys = m_device->StorageIndex(m_keys);
    push.effects = m_device->StorageIndex(m_effectTable);
    push.frameBuffer = frameBuffer;
    push.depthTexture = m_device->BindlessIndex(depth);
    push.capacity = kMaxParticles;
    push.cameraRight = cameraRight;
    push.cameraUp = cameraUp;
    // Over the meshes, back to front; they read the depth to fade into surfaces.
    graph.AddPass(
        {.name = "Particles",
         .rendering =
             rhi::RenderingDesc{.colorTarget = target, .moreColorTargets = {{reactive, glm::vec4(0.0f)}}, .area = area},
         .textures = {{depth, rhi::Access::FragmentShaderRead}},
         .buffers = {{m_particles, rhi::Access::VertexShaderRead},
                     {m_keys, rhi::Access::VertexShaderRead},
                     {m_arguments, rhi::Access::IndirectRead}}},
        [this, push](rhi::CommandList& commands) {
            DAI_PROFILE_SCOPE("renderer::ParticlePass::Draw");
            commands.BindPipeline(m_draw);
            commands.Push(push);
            commands.DrawIndirect(m_arguments);
        });
}

} // namespace daidalos::renderer
