// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include "MeshPass.hpp"

#include <daidalos/core/Assert.hpp>
#include <daidalos/core/Profile.hpp>
#include <daidalos/math/Projection.hpp>
#include <daidalos/rhi/Spirv.hpp>

#include <shaders/Mesh.ps.spv.h>
#include <shaders/Mesh.vs.spv.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <format>
#include <utility>

namespace daidalos::renderer {

namespace {

// These structures match their twins in shaders/MeshTypes.hlsli.
struct MeshPushConstants {
    std::uint32_t frameBuffer;
    std::uint32_t instanceBuffer;
    std::uint32_t materialBuffer;
    std::uint32_t instanceIndex;
    std::uint32_t materialIndex;
};

struct FrameData {
    glm::mat4 viewProjection;
    glm::vec3 cameraPosition;
    float fogDensity;
    glm::vec3 fogColor;
    float padding;
};

struct InstanceData {
    glm::mat4 world;
};

struct GpuMaterial {
    glm::vec4 baseColorFactor;
    glm::vec3 emissiveFactor;
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float occlusionStrength;
    float alphaCutoff;
    std::uint32_t baseColorTexture;
    std::uint32_t metallicRoughnessTexture;
    std::uint32_t normalTexture;
    std::uint32_t occlusionTexture;
    std::uint32_t emissiveTexture;
    std::array<std::uint32_t, 3> padding;
};
static_assert(sizeof(GpuMaterial) == 80);

constexpr std::uint32_t kNoTexture = 0xFFFFFFFF;
constexpr std::uint32_t kInitialMaterialCapacity = 64;
constexpr std::uint64_t kInitialInstanceCapacity = 256;

} // namespace

MeshPass::MeshPass(rhi::Device& device, rhi::Format colorFormat, rhi::Format depthFormat)
    : m_device(&device), m_colorFormat(colorFormat), m_depthFormat(depthFormat) {}

MeshPass::MeshPass(MeshPass&& other) noexcept
    : m_device(other.m_device), m_colorFormat(other.m_colorFormat), m_depthFormat(other.m_depthFormat),
      m_pipeline(std::exchange(other.m_pipeline, {})),
      m_doubleSidedPipeline(std::exchange(other.m_doubleSidedPipeline, {})),
      m_meshes(std::exchange(other.m_meshes, {})), m_materials(std::exchange(other.m_materials, {})),
      m_materialTable(std::exchange(other.m_materialTable, {})),
      m_materialCapacity(std::exchange(other.m_materialCapacity, 0)),
      m_defaultMaterial(std::exchange(other.m_defaultMaterial, {})),
      m_frameSlots(std::exchange(other.m_frameSlots, {})), m_nextFrameSlot(other.m_nextFrameSlot) {}

MeshPass::~MeshPass() {
    m_device->DestroyPipeline(m_pipeline);
    m_device->DestroyPipeline(m_doubleSidedPipeline);
    m_meshes.ForEach([this](const Mesh& mesh) {
        m_device->DestroyBuffer(mesh.vertices);
        m_device->DestroyBuffer(mesh.indices);
    });
    m_device->DestroyBuffer(m_materialTable);
    for (const FrameSlot& slot : m_frameSlots) {
        m_device->DestroyBuffer(slot.frame);
        m_device->DestroyBuffer(slot.instances);
    }
}

core::Result<MeshPass> MeshPass::Create(rhi::Device& device, rhi::Format colorFormat, rhi::Format depthFormat) {
    MeshPass pass(device, colorFormat, depthFormat);
    core::Result<void> built = pass.ReplaceShaders(rhi::ToSpirv(kMeshVsSpirv), rhi::ToSpirv(kMeshPsSpirv));
    if (!built) {
        return tl::unexpected(std::move(built.error()));
    }
    core::Result<MaterialHandle> defaultMaterial = pass.CreateMaterial({});
    if (!defaultMaterial) {
        return tl::unexpected(std::move(defaultMaterial.error()));
    }
    pass.m_defaultMaterial = *defaultMaterial;
    return pass;
}

core::Result<rhi::PipelineHandle> MeshPass::BuildPipeline(std::span<const std::uint32_t> vertexShader,
                                                          std::span<const std::uint32_t> fragmentShader,
                                                          rhi::CullMode cull) {
    using resources::Vertex;
    return m_device->CreatePipeline({
        .vertexShader = vertexShader,
        .fragmentShader = fragmentShader,
        .vertexStride = sizeof(Vertex),
        .vertexAttributes = {{0, rhi::Format::Rgb32Float, static_cast<std::uint32_t>(offsetof(Vertex, position))},
                             {1, rhi::Format::Rgb32Float, static_cast<std::uint32_t>(offsetof(Vertex, normal))},
                             {2, rhi::Format::Rg32Float, static_cast<std::uint32_t>(offsetof(Vertex, uv))}},
        .colorFormat = m_colorFormat,
        .depthFormat = m_depthFormat,
        .cull = cull,
        .name = "Mesh",
    });
}

core::Result<void> MeshPass::ReplaceShaders(std::span<const std::uint32_t> vertexShader,
                                            std::span<const std::uint32_t> fragmentShader) {
    core::Result<rhi::PipelineHandle> pipeline = BuildPipeline(vertexShader, fragmentShader, rhi::CullMode::Back);
    if (!pipeline) {
        return tl::unexpected(std::move(pipeline.error()));
    }
    core::Result<rhi::PipelineHandle> doubleSided = BuildPipeline(vertexShader, fragmentShader, rhi::CullMode::None);
    if (!doubleSided) {
        m_device->DestroyPipeline(*pipeline);
        return tl::unexpected(std::move(doubleSided.error()));
    }
    m_device->DestroyPipeline(m_pipeline);
    m_device->DestroyPipeline(m_doubleSidedPipeline);
    m_pipeline = *pipeline;
    m_doubleSidedPipeline = *doubleSided;
    return {};
}

core::Result<MeshHandle> MeshPass::CreateMesh(const resources::MeshData& data, const std::string& name) {
    DAI_ASSERT(!data.vertices.empty() && !data.indices.empty(), "A mesh cannot be empty");

    const auto vertexBytes = std::as_bytes(std::span(data.vertices));
    const auto indexBytes = std::as_bytes(std::span(data.indices));

    core::Result<rhi::BufferHandle> vertices =
        m_device->CreateBuffer({.size = vertexBytes.size(),
                                .usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::TransferDst,
                                .name = std::format("{} vertices", name)});
    if (!vertices) {
        return tl::unexpected(std::move(vertices.error()));
    }
    core::Result<rhi::BufferHandle> indices =
        m_device->CreateBuffer({.size = indexBytes.size(),
                                .usage = rhi::BufferUsage::Index | rhi::BufferUsage::TransferDst,
                                .name = std::format("{} indices", name)});
    if (!indices) {
        m_device->DestroyBuffer(*vertices);
        return tl::unexpected(std::move(indices.error()));
    }

    core::Result<void> uploaded = m_device->UploadBuffer(*vertices, vertexBytes).and_then([&] {
        return m_device->UploadBuffer(*indices, indexBytes);
    });
    if (!uploaded) {
        m_device->DestroyBuffer(*vertices);
        m_device->DestroyBuffer(*indices);
        return tl::unexpected(std::move(uploaded.error()));
    }
    return m_meshes.Insert({*vertices, *indices, static_cast<std::uint32_t>(data.indices.size())});
}

void MeshPass::DestroyMesh(MeshHandle mesh) {
    if (const std::optional<Mesh> removed = m_meshes.Remove(mesh)) {
        m_device->DestroyBuffer(removed->vertices);
        m_device->DestroyBuffer(removed->indices);
    }
}

std::uint32_t MeshPass::TextureIndex(rhi::TextureHandle texture) const {
    return texture ? m_device->BindlessIndex(texture) : kNoTexture;
}

core::Result<void> MeshPass::ReserveMaterialSlot(std::uint32_t index) {
    if (index < m_materialCapacity) {
        return {};
    }
    const std::uint32_t capacity = std::max(kInitialMaterialCapacity, m_materialCapacity * 2);
    core::Result<rhi::BufferHandle> table = m_device->CreateBuffer({.size = capacity * sizeof(GpuMaterial),
                                                                    .usage = rhi::BufferUsage::Storage,
                                                                    .memory = rhi::MemoryLocation::CpuToGpu,
                                                                    .name = "Materials"});
    if (!table) {
        return tl::unexpected(std::move(table.error()));
    }
    if (m_materialTable) {
        // Frames in flight keep reading the old table, which goes away after them.
        std::memcpy(m_device->Mapped(*table).data(), m_device->Mapped(m_materialTable).data(),
                    m_materialCapacity * sizeof(GpuMaterial));
        m_device->DestroyBuffer(m_materialTable);
    }
    m_materialTable = *table;
    m_materialCapacity = capacity;
    return {};
}

core::Result<MaterialHandle> MeshPass::CreateMaterial(const MaterialDesc& desc) {
    const GpuMaterial data{
        .baseColorFactor = desc.baseColorFactor,
        .emissiveFactor = desc.emissiveFactor,
        .metallicFactor = desc.metallicFactor,
        .roughnessFactor = desc.roughnessFactor,
        .normalScale = desc.normalScale,
        .occlusionStrength = desc.occlusionStrength,
        .alphaCutoff = desc.alphaCutoff,
        .baseColorTexture = TextureIndex(desc.baseColorTexture),
        .metallicRoughnessTexture = TextureIndex(desc.metallicRoughnessTexture),
        .normalTexture = TextureIndex(desc.normalTexture),
        .occlusionTexture = TextureIndex(desc.occlusionTexture),
        .emissiveTexture = TextureIndex(desc.emissiveTexture),
        .padding = {},
    };

    const MaterialHandle material = m_materials.Insert({desc.doubleSided});
    if (core::Result<void> reserved = ReserveMaterialSlot(material.Index()); !reserved) {
        m_materials.Remove(material);
        return tl::unexpected(std::move(reserved.error()));
    }
    // A slot reused here may still be read by a frame in flight, which draws a destroyed material:
    // that frame shows the new values, and nothing worse.
    std::memcpy(m_device->Mapped(m_materialTable).data() + std::size_t{material.Index()} * sizeof(GpuMaterial), &data,
                sizeof(data));
    return material;
}

void MeshPass::DestroyMaterial(MaterialHandle material) {
    DAI_ASSERT(material != m_defaultMaterial, "The default material belongs to the pass");
    m_materials.Remove(material);
}

core::Result<void> MeshPass::PrepareFrameSlot(FrameSlot& slot, std::uint64_t instanceCount) {
    if (!slot.frame) {
        core::Result<rhi::BufferHandle> frame = m_device->CreateBuffer({.size = sizeof(FrameData),
                                                                        .usage = rhi::BufferUsage::Storage,
                                                                        .memory = rhi::MemoryLocation::CpuToGpu,
                                                                        .name = "Frame data"});
        if (!frame) {
            return tl::unexpected(std::move(frame.error()));
        }
        slot.frame = *frame;
    }
    if (slot.instanceCapacity < instanceCount) {
        const std::uint64_t capacity = std::max({kInitialInstanceCapacity, instanceCount, slot.instanceCapacity * 2});
        core::Result<rhi::BufferHandle> instances = m_device->CreateBuffer({.size = capacity * sizeof(InstanceData),
                                                                            .usage = rhi::BufferUsage::Storage,
                                                                            .memory = rhi::MemoryLocation::CpuToGpu,
                                                                            .name = "Instances"});
        if (!instances) {
            return tl::unexpected(std::move(instances.error()));
        }
        m_device->DestroyBuffer(slot.instances);
        slot.instances = *instances;
        slot.instanceCapacity = capacity;
    }
    return {};
}

core::Result<void> MeshPass::Record(rhi::CommandList& commands, const SceneView& view, float aspect) {
    DAI_PROFILE_SCOPE("renderer::MeshPass");
    if (!view.camera.has_value()) {
        return {};
    }
    FrameSlot& slot = m_frameSlots[m_nextFrameSlot];
    m_nextFrameSlot = (m_nextFrameSlot + 1) % rhi::kFramesInFlight;
    if (core::Result<void> prepared = PrepareFrameSlot(slot, std::max<std::uint64_t>(view.draws.size(), 1));
        !prepared) {
        return prepared;
    }

    const CameraView& camera = *view.camera;
    const FrameData frame{math::InfinitePerspectiveReverseZ(camera.verticalFov, aspect, camera.nearPlane) * camera.view,
                          glm::vec3(glm::inverse(camera.view)[3]), view.atmosphere.fogDensity, view.atmosphere.fogColor,
                          0.0f};
    std::memcpy(m_device->Mapped(slot.frame).data(), &frame, sizeof(frame));
    auto* instances = reinterpret_cast<InstanceData*>(m_device->Mapped(slot.instances).data());

    MeshPushConstants push{
        .frameBuffer = m_device->StorageIndex(slot.frame),
        .instanceBuffer = m_device->StorageIndex(slot.instances),
        .materialBuffer = m_device->StorageIndex(m_materialTable),
        .instanceIndex = 0,
        .materialIndex = 0,
    };
    rhi::PipelineHandle bound;
    for (const DrawItem& draw : view.draws) {
        const Mesh* mesh = m_meshes.TryGet(draw.mesh);
        if (mesh == nullptr) {
            continue;
        }
        const MaterialHandle material = m_materials.TryGet(draw.material) ? draw.material : m_defaultMaterial;
        const rhi::PipelineHandle pipeline =
            m_materials.TryGet(material)->doubleSided ? m_doubleSidedPipeline : m_pipeline;
        if (pipeline != bound) {
            commands.BindPipeline(pipeline);
            bound = pipeline;
        }
        instances[push.instanceIndex] = {draw.world};
        push.materialIndex = material.Index();
        commands.BindVertexBuffer(mesh->vertices);
        commands.BindIndexBuffer(mesh->indices, rhi::IndexType::Uint32);
        commands.Push(push);
        commands.DrawIndexed(mesh->indexCount);
        ++push.instanceIndex;
    }
    return {};
}

} // namespace daidalos::renderer
