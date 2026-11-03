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
    std::uint32_t lightCount;
    glm::vec3 ambient;
    std::uint32_t lightBuffer;
};

struct LightData {
    glm::vec3 position;
    float range;
    glm::vec3 direction;
    float fullRange;
    glm::vec3 color;
    float cosOuter; // kPointLight for a point light.
    float cosInner;
    std::array<std::uint32_t, 3> padding;
};
static_assert(sizeof(LightData) == 64);

/// The cosine of the outer angle of a point light: every direction is inside.
constexpr float kPointLight = -2.0f;

LightData ToGpu(const Light& light) {
    const bool spot = light.type == LightType::Spot;
    return {light.position,
            light.range,
            spot ? glm::normalize(light.direction) : glm::vec3(0.0f, 0.0f, -1.0f),
            light.range * std::clamp(light.falloff, 0.0f, 1.0f),
            light.color,
            spot ? std::cos(light.outerAngle) : kPointLight,
            spot ? std::cos(light.innerAngle) : kPointLight + 1.0f,
            {}};
}

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
constexpr std::uint64_t kInitialLightCapacity = 64;

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
        m_device->DestroyBuffer(slot.lights);
    }
}

core::Result<MeshPass> MeshPass::Create(rhi::Device& device, rhi::Format colorFormat, rhi::Format depthFormat) {
    MeshPass pass(device, colorFormat, depthFormat);
    DAI_TRY(pass.ReplaceShaders(rhi::ToSpirv(kMeshVsSpirv), rhi::ToSpirv(kMeshPsSpirv)));
    // Plain white plastic. glTF's default material is a metal, which looks black without reflections.
    DAI_TRY_ASSIGN(MaterialHandle defaultMaterial, pass.CreateMaterial({.metallicFactor = 0.0f}));
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
                             {2, rhi::Format::Rg32Float, static_cast<std::uint32_t>(offsetof(Vertex, uv))},
                             {3, rhi::Format::Rgba32Float, static_cast<std::uint32_t>(offsetof(Vertex, tangent))}},
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

core::Result<void> MeshPass::PrepareFrameSlot(FrameSlot& slot, const SceneView& view) {
    // Grows an array of the frame slot to hold count elements, doubling, and never to zero.
    const auto reserve = [this](rhi::BufferHandle& buffer, std::uint64_t& capacity, std::uint64_t count,
                                std::uint64_t initial, std::uint64_t elementSize,
                                const char* name) -> core::Result<void> {
        if (buffer && capacity >= count) {
            return {};
        }
        const std::uint64_t grown = std::max({initial, count, capacity * 2});
        DAI_TRY_ASSIGN(rhi::BufferHandle created, m_device->CreateBuffer({.size = grown * elementSize,
                                                                          .usage = rhi::BufferUsage::Storage,
                                                                          .memory = rhi::MemoryLocation::CpuToGpu,
                                                                          .name = name}));
        m_device->DestroyBuffer(buffer);
        buffer = created;
        capacity = grown;
        return {};
    };
    std::uint64_t frameCapacity = slot.frame ? 1 : 0;
    DAI_TRY(reserve(slot.frame, frameCapacity, 1, 1, sizeof(FrameData), "Frame data"));
    DAI_TRY(reserve(slot.instances, slot.instanceCapacity, view.draws.size(), kInitialInstanceCapacity,
                    sizeof(InstanceData), "Instances"));
    DAI_TRY(reserve(slot.joints, slot.jointCapacity, view.jointMatrices.size(), kInitialJointCapacity,
                    sizeof(glm::mat4), "Joints"));
    DAI_TRY(reserve(slot.lights, slot.lightCapacity, view.lights.size(), kInitialLightCapacity, sizeof(LightData),
                    "Lights"));
    return {};
}

core::Result<void> MeshPass::Record(rhi::CommandList& commands, const SceneView& view, float aspect) {
    DAI_PROFILE_SCOPE("renderer::MeshPass");
    if (!view.camera.has_value()) {
        return {};
    }
    FrameSlot& slot = m_frameSlots[m_nextFrameSlot];
    m_nextFrameSlot = (m_nextFrameSlot + 1) % rhi::kFramesInFlight;
    DAI_TRY(PrepareFrameSlot(slot, view));

    const CameraView& camera = *view.camera;
    const FrameData frame{math::InfinitePerspectiveReverseZ(camera.verticalFov, aspect, camera.nearPlane) * camera.view,
                          glm::vec3(glm::inverse(camera.view)[3]),
                          view.atmosphere.fogDensity,
                          view.atmosphere.fogColor,
                          static_cast<std::uint32_t>(view.lights.size()),
                          view.atmosphere.ambient,
                          m_device->StorageIndex(slot.lights)};
    std::memcpy(m_device->Mapped(slot.frame).data(), &frame, sizeof(frame));
    auto* lights = reinterpret_cast<LightData*>(m_device->Mapped(slot.lights).data());
    for (std::size_t index = 0; index < view.lights.size(); ++index) {
        lights[index] = ToGpu(view.lights[index]);
    }
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
    // Sorted, so that the pipeline and the buffers change as rarely as possible.
    m_sorted.clear();
    for (std::uint32_t index = 0; index < view.draws.size(); ++index) {
        const DrawItem& draw = view.draws[index];
        if (m_meshes.TryGet(draw.mesh) == nullptr) {
            continue;
        }
        const MaterialHandle material = m_materials.TryGet(draw.material) ? draw.material : m_defaultMaterial;
        const std::uint64_t doubleSided = m_materials.Get(material).doubleSided ? 1 : 0;
        m_sorted.push_back({(doubleSided << 32U) | draw.mesh.Index(), index, material});
    }
    std::ranges::sort(m_sorted, {}, &SortedDraw::key);

    rhi::PipelineHandle boundPipeline;
    MeshHandle boundMesh;
    for (const SortedDraw& sorted : m_sorted) {
        const DrawItem& draw = view.draws[sorted.draw];
        const Mesh& mesh = m_meshes.Get(draw.mesh);
        const rhi::PipelineHandle pipeline =
            m_materials.Get(sorted.material).doubleSided ? m_doubleSidedPipeline : m_pipeline;
        if (pipeline != boundPipeline) {
            commands.BindPipeline(pipeline);
            boundPipeline = pipeline;
        }
        if (draw.mesh != boundMesh) {
            commands.BindVertexBuffer(mesh.vertices);
            commands.BindIndexBuffer(mesh.indices, rhi::IndexType::Uint32);
            boundMesh = draw.mesh;
        }
        // A skinned draw needs the skin of its mesh, and joints that the view holds. Otherwise it is rigid.
        const bool skinned = mesh.skin && draw.firstJoint != kNoJoints && draw.firstJoint < view.jointMatrices.size();
        instances[push.instanceIndex] = {
            draw.world, skinned ? m_device->StorageIndex(mesh.skin) : 0, skinned ? draw.firstJoint : kNoJoints, {}};
        push.materialIndex = sorted.material.Index();
        commands.Push(push);
        commands.DrawIndexed(mesh.indexCount);
        ++push.instanceIndex;
    }
    return {};
}

} // namespace daidalos::renderer
