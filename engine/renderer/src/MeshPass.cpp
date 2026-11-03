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
#include <cmath>
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
    std::uint32_t jointBuffer;
};

struct FrameData {
    glm::mat4 viewProjection;
    glm::vec3 cameraPosition;
    float fogDensity;
    glm::vec3 fogColor;
    float padding;
    glm::vec3 spotPosition;
    float spotRange; // 0 without a light.
    glm::vec3 spotDirection;
    float spotCosInner;
    glm::vec3 spotColor;
    float spotCosOuter;
};

struct InstanceData {
    glm::mat4 world;
    std::uint32_t skinBuffer;
    std::uint32_t firstJoint; // kNoJoints for a rigid draw.
    std::array<std::uint32_t, 2> padding;
};
static_assert(sizeof(InstanceData) == 80);
static_assert(sizeof(resources::SkinWeights) == 24, "Matches SkinVertex in MeshTypes.hlsli");

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
constexpr std::uint64_t kInitialJointCapacity = 256;

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
        m_device->DestroyBuffer(mesh.skin);
    });
    m_device->DestroyBuffer(m_materialTable);
    for (const FrameSlot& slot : m_frameSlots) {
        m_device->DestroyBuffer(slot.frame);
        m_device->DestroyBuffer(slot.instances);
        m_device->DestroyBuffer(slot.joints);
    }
}

core::Result<MeshPass> MeshPass::Create(rhi::Device& device, rhi::Format colorFormat, rhi::Format depthFormat) {
    MeshPass pass(device, colorFormat, depthFormat);
    DAI_TRY(pass.ReplaceShaders(rhi::ToSpirv(kMeshVsSpirv), rhi::ToSpirv(kMeshPsSpirv)));
    DAI_TRY_ASSIGN(MaterialHandle defaultMaterial, pass.CreateMaterial({}));
    pass.m_defaultMaterial = defaultMaterial;
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
    DAI_TRY_ASSIGN(rhi::PipelineHandle pipeline, BuildPipeline(vertexShader, fragmentShader, rhi::CullMode::Back));
    core::Result<rhi::PipelineHandle> doubleSided = BuildPipeline(vertexShader, fragmentShader, rhi::CullMode::None);
    if (!doubleSided) {
        m_device->DestroyPipeline(pipeline);
        return tl::unexpected(std::move(doubleSided.error()));
    }
    m_device->DestroyPipeline(m_pipeline);
    m_device->DestroyPipeline(m_doubleSidedPipeline);
    m_pipeline = pipeline;
    m_doubleSidedPipeline = *doubleSided;
    return {};
}

core::Result<MeshHandle> MeshPass::CreateMesh(const resources::MeshData& data, const std::string& name) {
    DAI_ASSERT(!data.vertices.empty() && !data.indices.empty(), "A mesh cannot be empty");
    DAI_ASSERT(data.skin.empty() || data.skin.size() == data.vertices.size(), "A skin needs one entry per vertex");

    const auto vertexBytes = std::as_bytes(std::span(data.vertices));
    const auto indexBytes = std::as_bytes(std::span(data.indices));

    DAI_TRY_ASSIGN(rhi::BufferHandle vertices,
                   m_device->CreateBuffer({.size = vertexBytes.size(),
                                           .usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::TransferDst,
                                           .name = std::format("{} vertices", name)}));
    core::Result<rhi::BufferHandle> indices =
        m_device->CreateBuffer({.size = indexBytes.size(),
                                .usage = rhi::BufferUsage::Index | rhi::BufferUsage::TransferDst,
                                .name = std::format("{} indices", name)});
    if (!indices) {
        m_device->DestroyBuffer(vertices);
        return tl::unexpected(std::move(indices.error()));
    }

    core::Result<void> uploaded = m_device->UploadBuffer(vertices, vertexBytes).and_then([&] {
        return m_device->UploadBuffer(*indices, indexBytes);
    });
    rhi::BufferHandle skin;
    if (uploaded && !data.skin.empty()) {
        const auto skinBytes = std::as_bytes(std::span(data.skin));
        core::Result<rhi::BufferHandle> created =
            m_device->CreateBuffer({.size = skinBytes.size(),
                                    .usage = rhi::BufferUsage::Storage | rhi::BufferUsage::TransferDst,
                                    .name = std::format("{} skin", name)});
        uploaded = created.and_then([&](rhi::BufferHandle buffer) {
            skin = buffer;
            return m_device->UploadBuffer(buffer, skinBytes);
        });
    }
    if (!uploaded) {
        m_device->DestroyBuffer(vertices);
        m_device->DestroyBuffer(*indices);
        m_device->DestroyBuffer(skin);
        return tl::unexpected(std::move(uploaded.error()));
    }
    return m_meshes.Insert({vertices, *indices, skin, static_cast<std::uint32_t>(data.indices.size())});
}

void MeshPass::DestroyMesh(MeshHandle mesh) {
    if (const std::optional<Mesh> removed = m_meshes.Remove(mesh)) {
        m_device->DestroyBuffer(removed->vertices);
        m_device->DestroyBuffer(removed->indices);
        m_device->DestroyBuffer(removed->skin);
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
    DAI_TRY_ASSIGN(rhi::BufferHandle table, m_device->CreateBuffer({.size = capacity * sizeof(GpuMaterial),
                                                                    .usage = rhi::BufferUsage::Storage,
                                                                    .memory = rhi::MemoryLocation::CpuToGpu,
                                                                    .name = "Materials"}));
    if (m_materialTable) {
        // Frames in flight keep reading the old table, which goes away after them.
        std::memcpy(m_device->Mapped(table).data(), m_device->Mapped(m_materialTable).data(),
                    m_materialCapacity * sizeof(GpuMaterial));
        m_device->DestroyBuffer(m_materialTable);
    }
    m_materialTable = table;
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

core::Result<void> MeshPass::PrepareFrameSlot(FrameSlot& slot, std::uint64_t instanceCount, std::uint64_t jointCount) {
    if (!slot.frame) {
        DAI_TRY_ASSIGN(rhi::BufferHandle frame, m_device->CreateBuffer({.size = sizeof(FrameData),
                                                                        .usage = rhi::BufferUsage::Storage,
                                                                        .memory = rhi::MemoryLocation::CpuToGpu,
                                                                        .name = "Frame data"}));
        slot.frame = frame;
    }
    if (slot.instanceCapacity < instanceCount) {
        const std::uint64_t capacity = std::max({kInitialInstanceCapacity, instanceCount, slot.instanceCapacity * 2});
        DAI_TRY_ASSIGN(rhi::BufferHandle instances, m_device->CreateBuffer({.size = capacity * sizeof(InstanceData),
                                                                            .usage = rhi::BufferUsage::Storage,
                                                                            .memory = rhi::MemoryLocation::CpuToGpu,
                                                                            .name = "Instances"}));
        m_device->DestroyBuffer(slot.instances);
        slot.instances = instances;
        slot.instanceCapacity = capacity;
    }
    if (slot.jointCapacity < jointCount) {
        const std::uint64_t capacity = std::max({kInitialJointCapacity, jointCount, slot.jointCapacity * 2});
        DAI_TRY_ASSIGN(rhi::BufferHandle joints, m_device->CreateBuffer({.size = capacity * sizeof(glm::mat4),
                                                                         .usage = rhi::BufferUsage::Storage,
                                                                         .memory = rhi::MemoryLocation::CpuToGpu,
                                                                         .name = "Joints"}));
        m_device->DestroyBuffer(slot.joints);
        slot.joints = joints;
        slot.jointCapacity = capacity;
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
    if (core::Result<void> prepared = PrepareFrameSlot(slot, std::max<std::uint64_t>(view.draws.size(), 1),
                                                       std::max<std::uint64_t>(view.jointMatrices.size(), 1));
        !prepared) {
        return prepared;
    }

    const CameraView& camera = *view.camera;
    const SpotLight spot = view.spotLight.value_or(SpotLight{.range = 0.0f});
    const FrameData frame{math::InfinitePerspectiveReverseZ(camera.verticalFov, aspect, camera.nearPlane) * camera.view,
                          glm::vec3(glm::inverse(camera.view)[3]),
                          view.atmosphere.fogDensity,
                          view.atmosphere.fogColor,
                          0.0f,
                          spot.position,
                          spot.range,
                          spot.direction,
                          std::cos(spot.innerAngle),
                          spot.color,
                          std::cos(spot.outerAngle)};
    std::memcpy(m_device->Mapped(slot.frame).data(), &frame, sizeof(frame));
    auto* instances = reinterpret_cast<InstanceData*>(m_device->Mapped(slot.instances).data());
    if (!view.jointMatrices.empty()) {
        std::memcpy(m_device->Mapped(slot.joints).data(), view.jointMatrices.data(),
                    view.jointMatrices.size() * sizeof(glm::mat4));
    }

    MeshPushConstants push{
        .frameBuffer = m_device->StorageIndex(slot.frame),
        .instanceBuffer = m_device->StorageIndex(slot.instances),
        .materialBuffer = m_device->StorageIndex(m_materialTable),
        .instanceIndex = 0,
        .materialIndex = 0,
        .jointBuffer = m_device->StorageIndex(slot.joints),
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
        // A skinned draw needs the skin of its mesh, and joints that the view holds. Otherwise it is rigid.
        const bool skinned = mesh->skin && draw.firstJoint != kNoJoints && draw.firstJoint < view.jointMatrices.size();
        instances[push.instanceIndex] = {
            draw.world, skinned ? m_device->StorageIndex(mesh->skin) : 0, skinned ? draw.firstJoint : kNoJoints, {}};
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
