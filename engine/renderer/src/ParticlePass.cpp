// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "ParticlePass.hpp"

#include <daidalos/core/Profile.hpp>
#include <daidalos/logging/Log.hpp>
#include <daidalos/rhi/Spirv.hpp>

#include <glm/geometric.hpp>
#include <glm/matrix.hpp>
#include <glm/vec4.hpp>
#include <shaders/ParticleDistortion.ps.spv.h>
#include <shaders/ParticleDistortion.vs.spv.h>
#include <shaders/ParticleDraw.ps.spv.h>
#include <shaders/ParticleDraw.vs.spv.h>
#include <shaders/ParticleEmit.cs.spv.h>
#include <shaders/ParticleFinish.cs.spv.h>
#include <shaders/ParticleMesh.ps.spv.h>
#include <shaders/ParticleMesh.vs.spv.h>
#include <shaders/ParticleSimulate.cs.spv.h>
#include <shaders/ParticleSort.cs.spv.h>
#include <shaders/ParticleSubEmit.cs.spv.h>

#include <algorithm>
#include <bit>
#include <cstring>
#include <numeric>
#include <utility>
#include <vector>

#include "MeshPass.hpp"
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
    glm::vec3 axis;
    float startFrame;
    glm::vec3 lightDirection;
    std::uint32_t state;
};
static_assert(sizeof(GpuParticle) == 112);

constexpr std::uint32_t kEffectLit = 1;
constexpr std::uint32_t kEffectReportsHits = 2;
constexpr std::uint32_t kEffectAdditive = 4;
constexpr std::uint32_t kEffectLoop = 8;
constexpr std::uint32_t kEffectRandomStart = 16;

struct GpuEffect {
    glm::vec3 spawnBox;
    float spawnRadius;
    glm::vec3 direction;
    float spread;
    glm::vec3 axis;
    float stretch;
    float speedMin;
    float speedMax;
    float lifeMin;
    float lifeMax;
    float sizeMin;
    float sizeMax;
    float gravity;
    float drag;
    float rotationSpeed;
    float softness;
    float bounce;
    float fadeIn;
    float inheritVelocity;
    float frameRate;
    std::uint32_t shape;
    std::uint32_t orientation;
    std::uint32_t texture;
    std::uint32_t cell;
    std::uint32_t columns;
    std::uint32_t rows;
    std::uint32_t frames;
    std::uint32_t flags;
    std::uint32_t collision;
    std::uint32_t deathEffect;
    std::uint32_t deathCount;
    std::uint32_t hitEffect;
    std::uint32_t hitCount;
    float turbulence;
    float turbulenceScale;
    float anisotropy;
    float distortion;
    std::uint32_t lightPositive;
    std::uint32_t lightNegative;
    std::uint32_t meshSlot;
    float meshRest;
    float padding;
};
static_assert(sizeof(GpuEffect) == 192);

// No texture, as the shaders' kNoTexture.
constexpr std::uint32_t kNoTexture = 0xFFFFFFFF;

struct DistortionPushConstants {
    std::uint32_t frameBuffer;
    std::uint32_t background;
    std::uint32_t distortion;
    std::uint32_t padding;
};

// No effect, as the shaders' kNoEffect.
constexpr std::uint32_t kNoEffect = 0xFFFFFFFF;

struct GpuSubSpawn {
    glm::vec3 position;
    std::uint32_t effect;
    glm::vec3 direction;
    std::uint32_t count;
    glm::vec3 velocity;
    std::uint32_t padding;
};
static_assert(sizeof(GpuSubSpawn) == 48);

constexpr std::uint32_t kForceWind = 0;
constexpr std::uint32_t kForceAttractor = 1;
struct GpuForce {
    std::array<glm::vec4, 3> toBox;
    glm::vec3 vector;
    std::uint32_t kind;
    glm::vec3 axis;
    std::uint32_t effect;
    float strength;
    float vortex;
    float radius;
    float gust;
    float gustFrequency;
    float edge;
    std::array<float, 2> padding;
};
static_assert(sizeof(GpuForce) == 112);

struct GpuHit {
    glm::vec3 position;
    std::uint32_t effect;
    glm::vec3 normal;
    std::uint32_t material;
};
static_assert(sizeof(GpuHit) == 32);

struct GpuFrame {
    glm::vec3 wind;
    std::uint32_t forceCount;
    float windGust;
    float windGustFrequency;
    std::uint32_t forces;
    std::uint32_t hits;
    std::uint32_t subSpawnsIn;
    std::uint32_t subSpawnsOut;
    std::uint32_t subCounterIn;
    std::uint32_t subCounterOut;
    std::uint32_t hitCapacity;
    std::uint32_t ribbons;
    std::uint32_t ribbonPoints;
    std::uint32_t ribbonSegments;
    std::uint32_t ribbonSegmentCount;
    std::uint32_t meshLists;
    std::array<std::uint32_t, 2> padding;
    std::array<std::uint32_t, ParticlePass::kMaxMeshEffects> meshIndexCounts;
};
static_assert(sizeof(GpuFrame) == 208);

struct GpuRibbonPoint {
    glm::vec3 position;
    float width;
    glm::vec4 color;
    glm::vec3 tangent;
    float u;
    std::uint32_t ribbon;
    std::array<std::uint32_t, 3> padding;
};
static_assert(sizeof(GpuRibbonPoint) == 64);

struct GpuRibbon {
    std::uint32_t texture;
    std::uint32_t flags;
    float softness;
    std::uint32_t padding;
};
static_assert(sizeof(GpuRibbon) == 16);

// The G-buffer's push constants, then the mesh particles' (ParticleMesh.slang).
struct MeshPushConstants {
    std::uint32_t frameBuffer;
    std::uint32_t instanceBuffer;
    std::uint32_t materialBuffer;
    std::uint32_t instanceIndex;
    std::uint32_t materialIndex;
    std::uint32_t particles;
    std::uint32_t list;
    std::uint32_t listStart;
    std::uint32_t effects;
    std::uint32_t curves;
    float timeStep;
    std::uint32_t padding;
};

struct GpuSpawn {
    glm::vec3 position;
    std::uint32_t effect;
    glm::vec4 rotation;
    glm::vec3 previous;
    std::uint32_t first;
    glm::vec3 velocity;
    std::uint32_t count;
    glm::vec3 scale;
    std::uint32_t padding;
};
static_assert(sizeof(GpuSpawn) == 80);

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
    std::uint32_t parameters;
    std::uint32_t sortBlock;
    std::uint32_t sortStep;
    std::uint32_t capacity;
    std::uint32_t instanceBuffer;
    glm::vec3 cameraRight;
    std::uint32_t materialBuffer;
    glm::vec3 cameraUp;
    std::uint32_t curves;
    std::uint32_t lightMap;
    std::uint32_t previousLightCount;
};
static_assert(sizeof(ParticlePushConstants) <= rhi::kMaxPushConstantSize);

constexpr std::uint32_t kGroupSize = 64;
constexpr std::uint32_t kGroups = (kMaxParticles + kGroupSize - 1) / kGroupSize;
constexpr std::uint32_t kInitialEffectCapacity = 16;
constexpr std::uint64_t kInitialSpawnCapacity = 64;
constexpr std::uint64_t kInitialForceCapacity = 8;
// As the shaders' kMaxSubSpawns: what a frame can ask of the next.
constexpr std::uint32_t kMaxSubSpawns = 4096;
// The counters: dead, alive, alive after the simulation, the two lists of sub-spawns, the mesh particles,
// and from kMeshCounters, the particles of each mesh effect.
constexpr std::uint32_t kMeshCounters = 8;
constexpr std::uint32_t kCounters = kMeshCounters + ParticlePass::kMaxMeshEffects;
constexpr std::uint32_t kSubCounter = 3;
// As the shaders' kMeshCapacity: the particles each mesh effect draws.
constexpr std::uint32_t kMeshCapacity = 4096;
// The draws' arguments: the quads', then each mesh effect's indexed draw.
constexpr std::uint32_t kMeshArguments = 4;
constexpr std::uint32_t kArguments = kMeshArguments + 5 * ParticlePass::kMaxMeshEffects;
// An effect that draws no mesh.
constexpr std::uint32_t kNoMeshSlot = 0xFFFFFFFF;
constexpr std::uint64_t kInitialRibbonCapacity = 8;
constexpr std::uint64_t kInitialPointCapacity = 256;
// Each effect's curves: color, then size, emission and speed (BakeParticleCurves).
constexpr std::uint64_t kCurveBytes = std::uint64_t{2} * kParticleCurveSamples * sizeof(glm::vec4);

glm::vec3 Normalized(const glm::vec3& direction) {
    return glm::length(direction) > 0.0f ? glm::normalize(direction) : glm::vec3(0.0f, 1.0f, 0.0f);
}

GpuEffect ToGpu(const ParticleEffectDesc& desc, std::uint32_t texture, std::uint32_t deathEffect,
                std::uint32_t hitEffect, std::uint32_t lightPositive, std::uint32_t lightNegative,
                std::uint32_t meshSlot, float meshRest) {
    const std::uint32_t flags = (desc.lit ? kEffectLit : 0U) | (desc.reportHits ? kEffectReportsHits : 0U) |
                                (desc.additive ? kEffectAdditive : 0U) | (desc.loop ? kEffectLoop : 0U) |
                                (desc.randomStartFrame ? kEffectRandomStart : 0U);
    return {.spawnBox = desc.spawnBox,
            .spawnRadius = desc.spawnRadius,
            .direction = Normalized(desc.direction),
            .spread = desc.spread,
            .axis = Normalized(desc.axis),
            .stretch = desc.stretch,
            .speedMin = desc.speed.x,
            .speedMax = desc.speed.y,
            .lifeMin = desc.lifetime.x,
            .lifeMax = desc.lifetime.y,
            .sizeMin = desc.size.x,
            .sizeMax = desc.size.y,
            .gravity = desc.gravity,
            .drag = desc.drag,
            .rotationSpeed = desc.rotationSpeed,
            .softness = desc.softness,
            .bounce = desc.bounce,
            .fadeIn = desc.fadeIn,
            .inheritVelocity = desc.inheritVelocity,
            .frameRate = desc.frameRate,
            .shape = static_cast<std::uint32_t>(desc.shape),
            .orientation = static_cast<std::uint32_t>(desc.orientation),
            .texture = texture,
            .cell = desc.cell,
            .columns = desc.columns,
            .rows = desc.rows == 0 ? desc.columns : desc.rows,
            .frames = std::max(desc.frames, 1U),
            .flags = flags,
            .collision = static_cast<std::uint32_t>(desc.collision),
            .deathEffect = deathEffect,
            .deathCount = desc.onDeath.count,
            .hitEffect = hitEffect,
            .hitCount = desc.onHit.count,
            .turbulence = desc.turbulence,
            .turbulenceScale = desc.turbulenceScale,
            .anisotropy = desc.anisotropy,
            .distortion = desc.distortion,
            .lightPositive = lightPositive,
            .lightNegative = lightNegative,
            .meshSlot = meshSlot,
            .meshRest = meshRest,
            .padding = 0.0f};
}
// The sort's groups, as in ParticleSort.slang: each sorts kSortLocal keys in its own memory.
constexpr std::uint32_t kSortThreads = 1024;
constexpr std::uint32_t kSortLocal = 2 * kSortThreads;
static_assert(std::has_single_bit(kMaxParticles) && kMaxParticles >= kSortLocal,
              "The bitonic sort needs a power of two, of whole groups");

} // namespace

ParticlePass::ParticlePass(ParticlePass&& other) noexcept
    : m_device(other.m_device), m_subEmit(std::exchange(other.m_subEmit, {})), m_emit(std::exchange(other.m_emit, {})),
      m_simulate(std::exchange(other.m_simulate, {})), m_finish(std::exchange(other.m_finish, {})),
      m_sort(std::exchange(other.m_sort, {})), m_draw(std::exchange(other.m_draw, {})),
      m_distort(std::exchange(other.m_distort, {})), m_meshDraw(std::exchange(other.m_meshDraw, {})),
      m_meshDrawDoubleSided(std::exchange(other.m_meshDrawDoubleSided, {})),
      m_meshLists(std::exchange(other.m_meshLists, {})), m_meshSlots(other.m_meshSlots),
      m_parameters(other.m_parameters), m_timeStep(other.m_timeStep), m_particles(std::exchange(other.m_particles, {})),
      m_dead(std::exchange(other.m_dead, {})), m_alive(std::exchange(other.m_alive, {})),
      m_subSpawns(std::exchange(other.m_subSpawns, {})), m_keys(std::exchange(other.m_keys, {})),
      m_counters(std::exchange(other.m_counters, {})), m_arguments(std::exchange(other.m_arguments, {})),
      m_current(other.m_current), m_effects(std::exchange(other.m_effects, {})),
      m_effectTable(std::exchange(other.m_effectTable, {})), m_curveTable(std::exchange(other.m_curveTable, {})),
      m_effectCapacity(std::exchange(other.m_effectCapacity, 0)), m_frameSlots(std::exchange(other.m_frameSlots, {})),
      m_nextFrameSlot(other.m_nextFrameSlot), m_frame(other.m_frame), m_hits(std::move(other.m_hits)),
      m_aliveCount(other.m_aliveCount), m_distorting(std::move(other.m_distorting)) {}

ParticlePass::~ParticlePass() {
    for (const rhi::PipelineHandle pipeline :
         {m_subEmit, m_emit, m_simulate, m_finish, m_sort, m_draw, m_distort, m_meshDraw, m_meshDrawDoubleSided}) {
        m_device->DestroyPipeline(pipeline);
    }
    for (const rhi::BufferHandle buffer : {m_particles, m_dead, m_alive[0], m_alive[1], m_subSpawns[0], m_subSpawns[1],
                                           m_keys, m_counters, m_arguments, m_effectTable, m_curveTable, m_meshLists}) {
        m_device->DestroyBuffer(buffer);
    }
    for (const FrameSlot& slot : m_frameSlots) {
        for (const rhi::BufferHandle buffer : {slot.spawns, slot.forces, slot.parameters, slot.hits, slot.ribbons,
                                               slot.ribbonPoints, slot.ribbonSegments}) {
            m_device->DestroyBuffer(buffer);
        }
    }
}

core::Result<ParticlePass> ParticlePass::Create(rhi::Device& device, rhi::Format colorFormat, rhi::Format depthFormat) {
    ParticlePass pass(device);
    DAI_TRY(pass.CreateResources(colorFormat, depthFormat));
    return pass;
}

core::Result<void> ParticlePass::CreateResources(rhi::Format colorFormat, rhi::Format depthFormat) {
    const auto compute = [this](std::span<const std::uint32_t> shader, const char* name,
                                rhi::PipelineHandle& pipeline) -> core::Result<void> {
        DAI_TRY_ASSIGN(pipeline, m_device->CreateComputePipeline({.shader = shader, .name = name}));
        return {};
    };
    DAI_TRY(compute(rhi::ToSpirv(kParticleSubEmitCsSpirv), "Particle sub-emission", m_subEmit));
    DAI_TRY(compute(rhi::ToSpirv(kParticleEmitCsSpirv), "Particle emission", m_emit));
    DAI_TRY(compute(rhi::ToSpirv(kParticleSimulateCsSpirv), "Particle simulation", m_simulate));
    DAI_TRY(compute(rhi::ToSpirv(kParticleFinishCsSpirv), "Particle finish", m_finish));
    DAI_TRY(compute(rhi::ToSpirv(kParticleSortCsSpirv), "Particle sort", m_sort));
    DAI_TRY_ASSIGN(m_draw, m_device->CreatePipeline({
                               .vertexShader = rhi::ToSpirv(kParticleDrawVsSpirv),
                               .fragmentShader = rhi::ToSpirv(kParticleDrawPsSpirv),
                               .colorFormat = colorFormat,
                               .moreColorFormats = {kReactiveFormat, kDistortionFormat},
                               .cull = rhi::CullMode::None,
                               .blend = rhi::BlendMode::Premultiplied,
                               .name = "Particles",
                           }));
    DAI_TRY_ASSIGN(m_distort, m_device->CreatePipeline({
                                  .vertexShader = rhi::ToSpirv(kParticleDistortionVsSpirv),
                                  .fragmentShader = rhi::ToSpirv(kParticleDistortionPsSpirv),
                                  .colorFormat = colorFormat,
                                  .cull = rhi::CullMode::None,
                                  .name = "Particle distortion",
                              }));
    // Mesh particles: the G-buffer's vertices and targets (MeshPass).
    for (const auto& [pipeline, cull] :
         {std::pair{&m_meshDraw, rhi::CullMode::Back}, std::pair{&m_meshDrawDoubleSided, rhi::CullMode::None}}) {
        using resources::Vertex;
        DAI_TRY_ASSIGN(
            *pipeline,
            m_device->CreatePipeline(
                {.vertexShader = rhi::ToSpirv(kParticleMeshVsSpirv),
                 .fragmentShader = rhi::ToSpirv(kParticleMeshPsSpirv),
                 .vertexStride = sizeof(Vertex),
                 .vertexAttributes =
                     {{0, rhi::Format::Rgb32Float, static_cast<std::uint32_t>(offsetof(Vertex, position))},
                      {1, rhi::Format::Rgb32Float, static_cast<std::uint32_t>(offsetof(Vertex, normal))},
                      {2, rhi::Format::Rg32Float, static_cast<std::uint32_t>(offsetof(Vertex, uv))},
                      {3, rhi::Format::Rgba32Float, static_cast<std::uint32_t>(offsetof(Vertex, tangent))}},
                 .colorFormat = MeshPass::kNormalFormat,
                 .moreColorFormats = {MeshPass::kMotionFormat, MeshPass::kBaseColorFormat, MeshPass::kEmissionFormat},
                 .depthFormat = depthFormat,
                 .cull = cull,
                 .name = "Mesh particles"}));
    }

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
    DAI_TRY(buffer(kCounters * sizeof(std::uint32_t), rhi::BufferUsage::Storage | rhi::BufferUsage::TransferDst,
                   "Particle counters", m_counters));
    for (rhi::BufferHandle& list : m_subSpawns) {
        DAI_TRY(buffer(std::uint64_t{kMaxSubSpawns} * sizeof(GpuSubSpawn), rhi::BufferUsage::Storage,
                       "Particle sub-spawns", list));
    }
    for (FrameSlot& slot : m_frameSlots) {
        DAI_TRY_ASSIGN(slot.parameters, m_device->CreateBuffer({.size = sizeof(GpuFrame),
                                                                .usage = rhi::BufferUsage::Storage,
                                                                .memory = rhi::MemoryLocation::CpuToGpu,
                                                                .name = "Particle frame"}));
        DAI_TRY_ASSIGN(slot.hits, m_device->CreateBuffer({.size = (std::uint64_t{kMaxHits} + 1) * sizeof(GpuHit),
                                                          .usage = rhi::BufferUsage::Storage,
                                                          .memory = rhi::MemoryLocation::GpuToCpu,
                                                          .name = "Particle hits"}));
    }
    DAI_TRY(buffer(kArguments * sizeof(std::uint32_t),
                   rhi::BufferUsage::Storage | rhi::BufferUsage::Indirect | rhi::BufferUsage::TransferDst,
                   "Particle draw", m_arguments));
    DAI_TRY(buffer(std::uint64_t{kMaxMeshEffects} * kMeshCapacity * sizeof(std::uint32_t), rhi::BufferUsage::Storage,
                   "Mesh particles", m_meshLists));

    // Every slot starts dead, nothing is alive, and nothing is drawn.
    std::vector<std::uint32_t> dead(kMaxParticles);
    std::iota(dead.begin(), dead.end(), 0U);
    DAI_TRY(m_device->UploadBuffer(m_dead, std::as_bytes(std::span(dead))));
    std::array<std::uint32_t, kCounters> counters{};
    counters[0] = kMaxParticles;
    DAI_TRY(m_device->UploadBuffer(m_counters, std::as_bytes(std::span(counters))));
    std::array<std::uint32_t, kArguments> arguments{};
    arguments[0] = 6;
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
    DAI_TRY_ASSIGN(rhi::BufferHandle curves, m_device->CreateBuffer({.size = capacity * kCurveBytes,
                                                                     .usage = rhi::BufferUsage::Storage,
                                                                     .memory = rhi::MemoryLocation::CpuToGpu,
                                                                     .name = "Particle curves"}));
    if (m_effectTable) {
        // Frames in flight keep reading the old tables, which go away after them.
        std::memcpy(m_device->Mapped(table).data(), m_device->Mapped(m_effectTable).data(),
                    m_effectCapacity * sizeof(GpuEffect));
        std::memcpy(m_device->Mapped(curves).data(), m_device->Mapped(m_curveTable).data(),
                    m_effectCapacity * kCurveBytes);
        m_device->DestroyBuffer(m_effectTable);
        m_device->DestroyBuffer(m_curveTable);
    }
    m_curveTable = curves;
    m_effectTable = table;
    m_effectCapacity = capacity;
    return {};
}

core::Result<ParticleEffectHandle> ParticlePass::CreateEffect(const ParticleEffectDesc& desc, const MeshPass& meshes) {
    const ParticleEffectHandle effect = m_effects.Insert({});
    if (core::Result<void> reserved = ReserveEffectSlot(effect.Index()); !reserved) {
        m_effects.Remove(effect);
        return tl::unexpected(std::move(reserved.error()));
    }
    WriteEffect(effect, desc, meshes);
    return effect;
}

bool ParticlePass::UpdateEffect(ParticleEffectHandle effect, const ParticleEffectDesc& desc, const MeshPass& meshes) {
    if (m_effects.TryGet(effect) == nullptr) {
        return false;
    }
    WriteEffect(effect, desc, meshes);
    return true;
}

std::uint32_t ParticlePass::AssignMeshSlot(ParticleEffectHandle effect, const ParticleEffectDesc& desc) {
    const auto held = std::ranges::find(m_meshSlots, effect, &MeshSlot::effect);
    if (desc.mesh.IsNull()) {
        if (held != m_meshSlots.end()) {
            *held = {};
        }
        return kNoMeshSlot;
    }
    const auto slot =
        held != m_meshSlots.end() ? held : std::ranges::find(m_meshSlots, ParticleEffectHandle{}, &MeshSlot::effect);
    if (slot == m_meshSlots.end()) {
        DAI_LOG_WARNING("renderer", "More than {} mesh particle effects: the new one draws nothing", kMaxMeshEffects);
        return kMaxMeshEffects;
    }
    *slot = {.effect = effect, .mesh = desc.mesh, .material = desc.material};
    return static_cast<std::uint32_t>(slot - m_meshSlots.begin());
}

void ParticlePass::WriteEffect(ParticleEffectHandle effect, const ParticleEffectDesc& desc, const MeshPass& meshes) {
    const std::uint32_t index = effect.Index();
    DAI_ASSERT(desc.texture || !desc.mesh.IsNull(), "A particle effect needs a texture or a mesh");
    const std::uint32_t rows = desc.rows == 0 ? desc.columns : desc.rows;
    DAI_ASSERT(desc.columns > 0 && desc.cell + std::max(desc.frames, 1U) <= desc.columns * rows,
               "The flipbook must be in the grid");
    const auto index_of = [this](ParticleEffectHandle effect) {
        return m_effects.TryGet(effect) != nullptr ? effect.Index() : kNoEffect;
    };
    const bool sixWay = desc.lightPositive && desc.lightNegative;
    const std::uint32_t meshSlot = AssignMeshSlot(effect, desc);
    // A resting mesh lies on its lowest vertex.
    const std::optional<MeshPass::Geometry> geometry = desc.mesh.IsNull() ? std::nullopt : meshes.GeometryOf(desc.mesh);
    const float meshRest = geometry ? std::max(-geometry->lowest, 0.0f) : 0.0f;
    const GpuEffect data =
        ToGpu(desc, desc.texture ? m_device->BindlessIndex(desc.texture) : kNoTexture, index_of(desc.onDeath.effect),
              index_of(desc.onHit.effect), sixWay ? m_device->BindlessIndex(desc.lightPositive) : kNoTexture,
              sixWay ? m_device->BindlessIndex(desc.lightNegative) : kNoTexture, meshSlot, meshRest);
    if (desc.distortion > 0.0f && desc.mesh.IsNull()) {
        m_distorting.insert(index);
    } else {
        m_distorting.erase(index);
    }
    std::memcpy(m_device->Mapped(m_effectTable).data() + std::size_t{index} * sizeof(GpuEffect), &data, sizeof(data));
    const std::vector<glm::vec4> curves = BakeParticleCurves(desc);
    std::memcpy(m_device->Mapped(m_curveTable).data() + index * kCurveBytes, curves.data(), kCurveBytes);
}

void ParticlePass::DestroyEffect(ParticleEffectHandle effect) {
    if (m_effects.Remove(effect)) {
        m_distorting.erase(effect.Index());
        if (const auto held = std::ranges::find(m_meshSlots, effect, &MeshSlot::effect); held != m_meshSlots.end()) {
            *held = {};
        }
    }
}

void ParticlePass::ReadHits(const FrameSlot& slot) {
    m_hits.clear();
    if (!slot.used) {
        return;
    }
    const std::span<const std::byte> bytes = m_device->Mapped(slot.hits);
    std::array<std::uint32_t, 2> header{};
    std::memcpy(header.data(), bytes.data(), sizeof(header));
    std::uint32_t count = header[0];
    m_aliveCount = header[1];
    count = std::min(count, kMaxHits);
    m_hits.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        GpuHit hit{};
        std::memcpy(&hit, bytes.data() + (std::size_t{i} + 1) * sizeof(GpuHit), sizeof(hit));
        m_hits.push_back({.position = hit.position,
                          .normal = hit.normal,
                          .effect = m_effects.HandleAt(hit.effect),
                          .material = hit.material});
    }
}

core::Result<std::uint32_t> ParticlePass::WriteRibbons(FrameSlot& slot, std::span<const Ribbon> ribbons) {
    // Each ribbon of at least two points, its points with their tangents, and a segment per pair of them;
    // no more segments than the pool has slots.
    std::vector<GpuRibbon> written;
    std::vector<GpuRibbonPoint> points;
    std::vector<std::uint32_t> segments;
    for (const Ribbon& ribbon : ribbons) {
        const std::size_t count = ribbon.points.size();
        if (count < 2 || segments.size() + count - 1 > kMaxParticles) {
            continue;
        }
        const auto index = static_cast<std::uint32_t>(written.size());
        written.push_back({.texture = ribbon.texture ? m_device->BindlessIndex(ribbon.texture) : kNoTexture,
                           .flags = ribbon.additive ? kEffectAdditive : 0U,
                           .softness = ribbon.softness,
                           .padding = 0});
        const auto first = static_cast<std::uint32_t>(points.size());
        for (std::size_t i = 0; i < count; ++i) {
            const RibbonPoint& point = ribbon.points[i];
            const glm::vec3 along =
                ribbon.points[std::min(i + 1, count - 1)].position - ribbon.points[i > 0 ? i - 1 : 0].position;
            points.push_back(
                {.position = point.position,
                 .width = point.width,
                 .color = point.color,
                 .tangent = glm::length(along) > 0.0f ? glm::normalize(along) : glm::vec3(0.0f, 1.0f, 0.0f),
                 .u = point.u,
                 .ribbon = index,
                 .padding = {}});
            if (i + 1 < count) {
                segments.push_back(first + static_cast<std::uint32_t>(i));
            }
        }
    }
    const auto upload = [this](rhi::BufferHandle& buffer, std::uint64_t& capacity, std::uint64_t initial,
                               std::span<const std::byte> bytes, std::uint64_t elementSize,
                               const char* name) -> core::Result<void> {
        const std::uint64_t needed = std::max<std::uint64_t>(bytes.size() / elementSize, 1);
        if (capacity < needed) {
            const std::uint64_t grown = std::max({initial, needed, capacity * 2});
            DAI_TRY_ASSIGN(rhi::BufferHandle created, m_device->CreateBuffer({.size = grown * elementSize,
                                                                              .usage = rhi::BufferUsage::Storage,
                                                                              .memory = rhi::MemoryLocation::CpuToGpu,
                                                                              .name = name}));
            m_device->DestroyBuffer(buffer);
            buffer = created;
            capacity = grown;
        }
        if (!bytes.empty()) {
            std::memcpy(m_device->Mapped(buffer).data(), bytes.data(), bytes.size());
        }
        return {};
    };
    DAI_TRY(upload(slot.ribbons, slot.ribbonCapacity, kInitialRibbonCapacity, std::as_bytes(std::span(written)),
                   sizeof(GpuRibbon), "Ribbons"));
    DAI_TRY(upload(slot.ribbonPoints, slot.pointCapacity, kInitialPointCapacity, std::as_bytes(std::span(points)),
                   sizeof(GpuRibbonPoint), "Ribbon points"));
    DAI_TRY(upload(slot.ribbonSegments, slot.segmentCapacity, kInitialPointCapacity, std::as_bytes(std::span(segments)),
                   sizeof(std::uint32_t), "Ribbon segments"));
    return static_cast<std::uint32_t>(segments.size());
}

bool ParticlePass::HasMeshes() const {
    return std::ranges::any_of(m_meshSlots, [](const MeshSlot& slot) { return !slot.effect.IsNull(); });
}

std::vector<rhi::BufferUse> ParticlePass::MeshBuffers() const {
    return {{m_particles, rhi::Access::VertexShaderRead},
            {m_meshLists, rhi::Access::VertexShaderRead},
            {m_arguments, rhi::Access::IndirectRead}};
}

void ParticlePass::RecordMeshes(rhi::CommandList& commands, std::uint32_t frameBuffer, const MeshPass& meshes) const {
    DAI_PROFILE_SCOPE("renderer::ParticlePass::RecordMeshes");
    MeshPushConstants push{.frameBuffer = frameBuffer,
                           .instanceBuffer = 0,
                           .materialBuffer = meshes.MaterialDataIndex(),
                           .instanceIndex = 0,
                           .materialIndex = 0,
                           .particles = m_device->StorageIndex(m_particles),
                           .list = m_device->StorageIndex(m_meshLists),
                           .listStart = 0,
                           .effects = m_device->StorageIndex(m_effectTable),
                           .curves = m_device->StorageIndex(m_curveTable),
                           .timeStep = m_timeStep,
                           .padding = 0};
    for (std::uint32_t index = 0; index < kMaxMeshEffects; ++index) {
        const MeshSlot& slot = m_meshSlots[index];
        if (slot.effect.IsNull()) {
            continue;
        }
        const std::optional<MeshPass::Geometry> geometry = meshes.GeometryOf(slot.mesh);
        if (!geometry) {
            continue;
        }
        const MeshPass::MaterialSlot material = meshes.MaterialOf(slot.material);
        commands.BindPipeline(material.doubleSided ? m_meshDrawDoubleSided : m_meshDraw);
        commands.BindVertexBuffer(geometry->vertices);
        commands.BindIndexBuffer(geometry->indices, rhi::IndexType::Uint32);
        push.materialIndex = material.index;
        push.listStart = index * kMeshCapacity;
        commands.Push(push);
        commands.DrawIndexedIndirect(m_arguments, (kMeshArguments + std::uint64_t{index} * 5) * sizeof(std::uint32_t));
    }
}

core::Result<void> ParticlePass::AddSimulation(RenderGraph& graph, const SceneView& view, std::uint32_t frameBuffer,
                                               const Shadows& shadows, const MeshPass& meshes) {
    DAI_PROFILE_SCOPE("renderer::ParticlePass::AddSimulation");
    FrameSlot& slot = m_frameSlots[m_nextFrameSlot];
    m_nextFrameSlot = (m_nextFrameSlot + 1) % rhi::kFramesInFlight;
    // The GPU finished the last frame recorded with this slot: what it hit is read before it is overwritten.
    ReadHits(slot);
    slot.used = true;

    // The requests of the frame, each with the number of particles before it. Beyond the pool's size,
    // the GPU drops them anyway.
    std::vector<GpuSpawn> requests;
    std::uint32_t emitted = 0;
    for (const ParticleSpawn& spawn : view.particleSpawns) {
        if (spawn.count == 0 || m_effects.TryGet(spawn.effect) == nullptr || emitted >= kMaxParticles) {
            continue;
        }
        const std::uint32_t count = std::min(spawn.count, kMaxParticles - emitted);
        requests.push_back(
            {.position = spawn.position,
             .effect = spawn.effect.Index(),
             .rotation = glm::vec4(spawn.rotation.x, spawn.rotation.y, spawn.rotation.z, spawn.rotation.w),
             .previous = spawn.previousPosition.value_or(spawn.position),
             .first = emitted,
             .velocity = spawn.velocity,
             .count = count,
             .scale = spawn.scale,
             .padding = 0});
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

    // Wind volumes and attractors (ADR-0053).
    std::vector<GpuForce> forces;
    for (const WindVolume& wind : view.windVolumes) {
        const glm::mat4 toBox = glm::transpose(glm::inverse(wind.world));
        forces.push_back({.toBox = {toBox[0], toBox[1], toBox[2]},
                          .vector = wind.velocity,
                          .kind = kForceWind,
                          .axis = glm::vec3(0.0f),
                          .effect = kNoEffect,
                          .strength = 0.0f,
                          .vortex = 0.0f,
                          .radius = 0.0f,
                          .gust = wind.gust,
                          .gustFrequency = wind.gustFrequency,
                          .edge = wind.edge,
                          .padding = {}});
    }
    for (const ParticleAttractor& attractor : view.particleAttractors) {
        const float length = glm::length(attractor.axis);
        forces.push_back({.toBox = {},
                          .vector = attractor.position,
                          .kind = kForceAttractor,
                          .axis = length > 0.0f ? attractor.axis / length : glm::vec3(0.0f, 1.0f, 0.0f),
                          .effect = attractor.effect.IsNull() ? kNoEffect : attractor.effect.Index(),
                          .strength = attractor.strength,
                          .vortex = attractor.vortex,
                          .radius = attractor.radius,
                          .gust = 0.0f,
                          .gustFrequency = 0.0f,
                          .edge = 0.0f,
                          .padding = {}});
    }
    if (slot.forceCapacity < std::max<std::size_t>(forces.size(), 1)) {
        const std::uint64_t capacity =
            std::max<std::uint64_t>({kInitialForceCapacity, forces.size(), slot.forceCapacity * 2});
        DAI_TRY_ASSIGN(rhi::BufferHandle created, m_device->CreateBuffer({.size = capacity * sizeof(GpuForce),
                                                                          .usage = rhi::BufferUsage::Storage,
                                                                          .memory = rhi::MemoryLocation::CpuToGpu,
                                                                          .name = "Particle forces"}));
        m_device->DestroyBuffer(slot.forces);
        slot.forces = created;
        slot.forceCapacity = capacity;
    }
    if (!forces.empty()) {
        std::memcpy(m_device->Mapped(slot.forces).data(), forces.data(), forces.size() * sizeof(GpuForce));
    }

    DAI_TRY_ASSIGN(const std::uint32_t segments, WriteRibbons(slot, view.ribbons));
    // Each mesh effect draws its mesh's indices, or nothing if the mesh is gone.
    std::array<std::uint32_t, kMaxMeshEffects> indexCounts{};
    for (std::uint32_t index = 0; index < kMaxMeshEffects; ++index) {
        if (!m_meshSlots[index].effect.IsNull()) {
            const std::optional<MeshPass::Geometry> geometry = meshes.GeometryOf(m_meshSlots[index].mesh);
            indexCounts[index] = geometry ? geometry->indexCount : 0;
        }
    }

    // The sub-spawns of the last frame are spawned now, from one list, while this frame's go to the other.
    const std::uint32_t next = 1 - m_current;
    const GpuFrame parameters{.wind = view.atmosphere.wind,
                              .forceCount = static_cast<std::uint32_t>(forces.size()),
                              .windGust = view.atmosphere.windGust,
                              .windGustFrequency = view.atmosphere.windGustFrequency,
                              .forces = m_device->StorageIndex(slot.forces),
                              .hits = m_device->StorageIndex(slot.hits),
                              .subSpawnsIn = m_device->StorageIndex(m_subSpawns[m_current]),
                              .subSpawnsOut = m_device->StorageIndex(m_subSpawns[next]),
                              .subCounterIn = kSubCounter + m_current,
                              .subCounterOut = kSubCounter + next,
                              .hitCapacity = kMaxHits,
                              .ribbons = m_device->StorageIndex(slot.ribbons),
                              .ribbonPoints = m_device->StorageIndex(slot.ribbonPoints),
                              .ribbonSegments = m_device->StorageIndex(slot.ribbonSegments),
                              .ribbonSegmentCount = segments,
                              .meshLists = m_device->StorageIndex(m_meshLists),
                              .padding = {},
                              .meshIndexCounts = indexCounts};
    std::memcpy(m_device->Mapped(slot.parameters).data(), &parameters, sizeof(parameters));
    m_parameters = m_device->StorageIndex(slot.parameters);
    m_timeStep = std::max(view.timeStep, 0.0f);

    ParticlePushConstants push{};
    push.particles = m_device->StorageIndex(m_particles);
    push.dead = m_device->StorageIndex(m_dead);
    push.aliveIn = m_device->StorageIndex(m_alive[m_current]);
    push.aliveOut = m_device->StorageIndex(m_alive[next]);
    push.keys = m_device->StorageIndex(m_keys);
    push.counters = m_device->StorageIndex(m_counters);
    push.arguments = m_device->StorageIndex(m_arguments);
    push.effects = m_device->StorageIndex(m_effectTable);
    push.curves = m_device->StorageIndex(m_curveTable);
    push.spawns = m_device->StorageIndex(slot.spawns);
    push.spawnCount = static_cast<std::uint32_t>(requests.size());
    push.emitCount = emitted;
    push.frameBuffer = frameBuffer;
    push.timeStep = std::max(view.timeStep, 0.0f);
    push.seed = ++m_frame;
    push.parameters = m_device->StorageIndex(slot.parameters);
    push.capacity = kMaxParticles;
    push.instanceBuffer = shadows.instanceBuffer;
    push.materialBuffer = shadows.materialBuffer;
    push.lightMap = shadows.lightMap;
    push.previousLightCount = shadows.previousLightCount;

    // Every pass but the sort reads and writes most of the pool's buffers.
    const std::vector<rhi::BufferUse> pool{{m_particles, rhi::Access::ComputeShaderWrite},
                                           {m_dead, rhi::Access::ComputeShaderWrite},
                                           {m_alive[m_current], rhi::Access::ComputeShaderWrite},
                                           {m_alive[next], rhi::Access::ComputeShaderWrite},
                                           {m_subSpawns[0], rhi::Access::ComputeShaderWrite},
                                           {m_subSpawns[1], rhi::Access::ComputeShaderWrite},
                                           {m_keys, rhi::Access::ComputeShaderWrite},
                                           {m_counters, rhi::Access::ComputeShaderWrite},
                                           {m_arguments, rhi::Access::ComputeShaderWrite},
                                           {m_meshLists, rhi::Access::ComputeShaderWrite},
                                           {slot.hits, rhi::Access::ComputeShaderWrite}};
    const auto dispatch = [push](rhi::PipelineHandle pipeline, std::uint32_t groups) {
        return [push, pipeline, groups](rhi::CommandList& commands) {
            commands.BindPipeline(pipeline);
            commands.Push(push);
            commands.Dispatch(groups);
        };
    };
    // The CPU does not know how many sub-spawns there are: the dispatch covers as many as can be. It also
    // empties this frame's hits.
    graph.AddPass({.name = "Particle sub-emission", .group = "Simulation", .buffers = pool},
                  dispatch(m_subEmit, kMaxSubSpawns / kGroupSize));
    if (emitted > 0) {
        graph.AddPass({.name = "Particle emission", .group = "Simulation", .buffers = pool},
                      dispatch(m_emit, (emitted + kGroupSize - 1) / kGroupSize));
    }
    // Particles meet the frame's surfaces, and lit ones cast shadow rays.
    std::vector<rhi::BufferUse> simulated = pool;
    simulated.push_back({shadows.skinnedVertices, rhi::Access::ComputeShaderRead});
    simulated.push_back({shadows.emissiveTriangles, rhi::Access::ComputeShaderRead});
    graph.AddPass({.name = "Particle simulation",
                   .group = "Simulation",
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
                           rhi::TextureHandle target, rhi::TextureHandle reactive, rhi::TextureHandle distortion,
                           const glm::vec3& cameraRight, const glm::vec3& cameraUp, std::optional<rhi::Extent> area,
                           rhi::TextureHandle fog) {
    ParticlePushConstants push{};
    push.particles = m_device->StorageIndex(m_particles);
    push.keys = m_device->StorageIndex(m_keys);
    push.effects = m_device->StorageIndex(m_effectTable);
    push.curves = m_device->StorageIndex(m_curveTable);
    push.frameBuffer = frameBuffer;
    push.parameters = m_parameters;
    push.depthTexture = m_device->BindlessIndex(depth);
    push.capacity = kMaxParticles;
    push.cameraRight = cameraRight;
    push.cameraUp = cameraUp;
    // Over the meshes, back to front; they read the depth to fade into surfaces.
    graph.AddPass({.name = "Particles",
                   .rendering = rhi::RenderingDesc{.colorTarget = target,
                                                   .moreColorTargets = {{reactive, glm::vec4(0.0f)},
                                                                        {distortion, glm::vec4(0.0f)}},
                                                   .area = area},
                   .textures = {{depth, rhi::Access::FragmentShaderRead}, {fog, rhi::Access::FragmentShaderRead}},
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

void ParticlePass::AddDistortion(RenderGraph& graph, std::uint32_t frameBuffer, rhi::TextureHandle target,
                                 rhi::TextureHandle background, rhi::TextureHandle distortion,
                                 std::optional<rhi::Extent> area) {
    graph.AddPass({.name = "Particle distortion copy",
                   .group = "Particles",
                   .textures = {{target, rhi::Access::TransferRead}, {background, rhi::Access::TransferWrite, true}}},
                  [target, background](rhi::CommandList& commands) { commands.CopyTexture(target, background); });
    const DistortionPushConstants push{.frameBuffer = frameBuffer,
                                       .background = m_device->BindlessIndex(background),
                                       .distortion = m_device->BindlessIndex(distortion),
                                       .padding = 0};
    graph.AddPass(
        {.name = "Particle distortion",
         .group = "Particles",
         .rendering = rhi::RenderingDesc{.colorTarget = target, .area = area},
         .textures = {{background, rhi::Access::FragmentShaderRead}, {distortion, rhi::Access::FragmentShaderRead}}},
        [this, push](rhi::CommandList& commands) {
            commands.BindPipeline(m_distort);
            commands.Push(push);
            commands.Draw(3);
        });
}

} // namespace daidalos::renderer
