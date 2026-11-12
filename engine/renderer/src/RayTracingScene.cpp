// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include "RayTracingScene.hpp"

#include <daidalos/core/Assert.hpp>
#include <daidalos/core/Profile.hpp>
#include <daidalos/rhi/Spirv.hpp>

#include <shaders/RayDebug.ps.spv.h>
#include <shaders/RayDebug.vs.spv.h>

#include <algorithm>
#include <format>
#include <utility>

#include "RenderGraph.hpp"

namespace daidalos::renderer {

namespace {

// Matches RayDebugPushConstants in shaders/RayDebug.slang.
struct RayDebugPushConstants {
    std::uint32_t frameBuffer;
    std::uint32_t instanceBuffer;
};

constexpr std::uint32_t kInitialInstanceCapacity = 256;

} // namespace

RayTracingScene::RayTracingScene(rhi::Device& device) : m_device(&device) {}

RayTracingScene::RayTracingScene(RayTracingScene&& other) noexcept
    : m_device(other.m_device), m_debugPipeline(std::exchange(other.m_debugPipeline, {})),
      m_slots(std::exchange(other.m_slots, {})), m_slot(other.m_slot), m_skinned(std::move(other.m_skinned)),
      m_instanceCount(other.m_instanceCount), m_skinnedCount(other.m_skinnedCount),
      m_bottomLevel(std::move(other.m_bottomLevel)), m_instances(std::move(other.m_instances)), m_stats(other.m_stats) {
    other.m_skinned.clear();
}

RayTracingScene::~RayTracingScene() {
    m_device->DestroyPipeline(m_debugPipeline);
    for (const Slot& slot : m_slots) {
        m_device->DestroyAccelerationStructure(slot.structure);
        m_device->DestroyBuffer(slot.instances);
    }
    for (const SkinnedStructure& skinned : m_skinned) {
        m_device->DestroyAccelerationStructure(skinned.structure);
    }
}

core::Result<RayTracingScene> RayTracingScene::Create(rhi::Device& device, rhi::Format colorFormat) {
    RayTracingScene scene(device);
    DAI_TRY_ASSIGN(scene.m_debugPipeline, device.CreatePipeline({.vertexShader = rhi::ToSpirv(kRayDebugVsSpirv),
                                                                 .fragmentShader = rhi::ToSpirv(kRayDebugPsSpirv),
                                                                 .colorFormat = colorFormat,
                                                                 .cull = rhi::CullMode::None,
                                                                 .name = "Ray debug"}));
    return scene;
}

core::Result<std::uint32_t> RayTracingScene::Reserve(std::uint32_t slot, std::size_t draws) {
    m_slot = slot;
    Slot& target = m_slots[slot];
    if (!target.structure || target.capacity < draws) {
        const auto capacity =
            static_cast<std::uint32_t>(std::max<std::size_t>({kInitialInstanceCapacity, draws, target.capacity * 2U}));
        DAI_TRY_ASSIGN(rhi::AccelerationStructureHandle structure,
                       m_device->CreateAccelerationStructure({.level = rhi::AccelerationStructureLevel::Top,
                                                              .maxInstances = capacity,
                                                              .rebuiltEveryFrame = true,
                                                              .name = std::format("Scene {}", slot)}));
        core::Result<rhi::BufferHandle> instances =
            m_device->CreateBuffer({.size = capacity * rhi::kAccelerationStructureInstanceSize,
                                    .usage = rhi::BufferUsage::AccelerationStructureInput,
                                    .memory = rhi::MemoryLocation::CpuToGpu,
                                    .name = std::format("Scene instances {}", slot)});
        if (!instances) {
            m_device->DestroyAccelerationStructure(structure);
            return tl::unexpected(std::move(instances.error()));
        }
        // Frames in flight keep using the old ones, which go away after them.
        m_device->DestroyAccelerationStructure(target.structure);
        m_device->DestroyBuffer(target.instances);
        target = {structure, *instances, capacity};
    }
    return m_device->AccelerationStructureIndex(target.structure);
}

core::Result<void> RayTracingScene::Prepare(std::span<const TracedDraw> draws) {
    DAI_PROFILE_SCOPE("renderer::RayTracingScene::Prepare");
    const Slot& slot = m_slots[m_slot];
    DAI_ASSERT(slot.capacity >= draws.size(), "Reserve was not called for this many draws");
    m_instances.clear();
    m_bottomLevel.clear();
    m_skinnedCount = 0;
    for (std::size_t index = 0; index < draws.size(); ++index) {
        const TracedDraw& draw = draws[index];
        rhi::AccelerationStructureHandle structure = draw.structure;
        if (!structure) {
            // A skinned draw keeps the structure of the draw in its place last frame if the geometry is the
            // same; otherwise it gets one for its geometry.
            if (m_skinnedCount == m_skinned.size()) {
                m_skinned.emplace_back();
            }
            SkinnedStructure& skinned = m_skinned[m_skinnedCount];
            if (!skinned.structure || skinned.geometry != draw.skinned) {
                DAI_TRY_ASSIGN(rhi::AccelerationStructureHandle created,
                               m_device->CreateAccelerationStructure({.level = rhi::AccelerationStructureLevel::Bottom,
                                                                      .geometry = draw.skinned,
                                                                      .refit = true,
                                                                      .name = "Skinned draw"}));
                m_device->DestroyAccelerationStructure(skinned.structure);
                skinned = {draw.skinned, created};
            }
            structure = skinned.structure;
            ++m_skinnedCount;
        }
        m_bottomLevel.push_back({structure, rhi::Access::AccelerationStructureRead});
        m_instances.push_back({.transform = draw.world,
                               .customIndex = static_cast<std::uint32_t>(index),
                               .mask = draw.glass          ? kMaskGlass
                                       : draw.castsShadows ? kMaskShadowCasters
                                                           : kMaskOthers,
                               .opaque = draw.opaque,
                               .doubleSided = draw.doubleSided,
                               .structure = structure});
    }
    // Structures of skinned draws that are gone.
    for (std::size_t index = m_skinnedCount; index < m_skinned.size(); ++index) {
        m_device->DestroyAccelerationStructure(m_skinned[index].structure);
    }
    m_skinned.resize(m_skinnedCount);
    // A mesh drawn many times is one structure to wait for.
    std::ranges::sort(m_bottomLevel, {}, [](const rhi::AccelerationStructureUse& use) {
        return std::pair(use.structure.Index(), use.structure.Generation());
    });
    const auto [first, last] = std::ranges::unique(m_bottomLevel, {}, &rhi::AccelerationStructureUse::structure);
    m_bottomLevel.erase(first, last);

    m_device->WriteInstances(slot.instances, 0, m_instances);
    m_instanceCount = static_cast<std::uint32_t>(m_instances.size());
    m_stats = {.instances = m_instanceCount, .skinnedStructures = m_skinnedCount};
    return {};
}

void RayTracingScene::AddBuild(RenderGraph& graph) {
    const Slot& slot = m_slots[m_slot];
    if (m_skinnedCount > 0) {
        PassDesc desc{.name = "Skinned structures", .group = "Acceleration structures"};
        for (std::uint32_t index = 0; index < m_skinnedCount; ++index) {
            const SkinnedStructure& skinned = m_skinned[index];
            desc.buffers.push_back({skinned.geometry.vertices, rhi::Access::AccelerationStructureRead});
            desc.buffers.push_back({skinned.geometry.indices, rhi::Access::AccelerationStructureRead});
            desc.structures.push_back({skinned.structure, rhi::Access::AccelerationStructureWrite});
        }
        // Each structure has its own scratch memory: one command refits them side by side.
        graph.AddPass(std::move(desc), [this](rhi::CommandList& commands) {
            m_skinnedHandles.clear();
            for (std::uint32_t index = 0; index < m_skinnedCount; ++index) {
                m_skinnedHandles.push_back(m_skinned[index].structure);
            }
            commands.BuildAccelerationStructures(m_skinnedHandles);
        });
    }
    PassDesc desc{.name = "Scene structure",
                  .group = "Acceleration structures",
                  .buffers = {{slot.instances, rhi::Access::AccelerationStructureRead}},
                  .structures = m_bottomLevel};
    desc.structures.push_back({slot.structure, rhi::Access::AccelerationStructureWrite});
    graph.AddPass(std::move(desc),
                  [this, structure = slot.structure, instances = slot.instances](rhi::CommandList& commands) {
                      commands.BuildAccelerationStructure(structure, instances, m_instanceCount);
                  });
}

void RayTracingScene::AddDebugView(RenderGraph& graph, rhi::TextureHandle target, std::uint32_t frameBuffer,
                                   std::uint32_t instanceBuffer) {
    PassDesc desc{.name = "Ray debug",
                  .rendering = rhi::RenderingDesc{.colorTarget = target},
                  .textures = {{target, rhi::Access::ColorTarget, true}},
                  .structures = {{m_slots[m_slot].structure, rhi::Access::RayQueryRead}}};
    if (m_skinnedCount > 0) {
        // The hits on skinned draws read their skinned vertices.
        desc.buffers.push_back({m_skinned.front().geometry.vertices, rhi::Access::FragmentShaderRead});
    }
    graph.AddPass(std::move(desc),
                  [this, push = RayDebugPushConstants{frameBuffer, instanceBuffer}](rhi::CommandList& commands) {
                      commands.BindPipeline(m_debugPipeline);
                      commands.Push(push);
                      commands.Draw(3);
                  });
}

} // namespace daidalos::renderer
