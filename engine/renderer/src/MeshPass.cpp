// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include "MeshPass.hpp"

#include <daidalos/core/Assert.hpp>
#include <daidalos/core/Profile.hpp>
#include <daidalos/math/Projection.hpp>
#include <daidalos/rhi/Spirv.hpp>

#include <glm/ext/matrix_transform.hpp>
#include <shaders/Mesh.ps.spv.h>
#include <shaders/Mesh.vs.spv.h>
#include <shaders/Prepass.ps.spv.h>
#include <shaders/ShadowMap.ps.spv.h>
#include <shaders/ShadowMap.vs.spv.h>
#include <shaders/Skinning.cs.spv.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <format>
#include <utility>

#include "RenderGraph.hpp"

namespace daidalos::renderer {

namespace {

// These structures match their twins in shaders/MeshTypes.slangh.
struct MeshPushConstants {
    std::uint32_t frameBuffer;
    std::uint32_t instanceBuffer;
    std::uint32_t materialBuffer;
    std::uint32_t instanceIndex;
    std::uint32_t materialIndex;
    std::uint32_t shadowIndex;
};

struct SkinningPushConstants {
    std::uint32_t source;
    std::uint32_t skin;
    std::uint32_t joints;
    std::uint32_t firstJoint;
    std::uint32_t target;
    std::uint32_t firstVertex;
    std::uint32_t vertexCount;
};
constexpr std::uint32_t kSkinningGroupSize = 64;

struct FrameData {
    glm::mat4 viewProjection;
    glm::mat4 inverseViewProjection;
    glm::vec3 cameraPosition;
    float fogDensity;
    glm::vec3 fogColor;
    std::uint32_t lightCount;
    glm::vec3 ambient;
    std::uint32_t lightBuffer;
    std::uint32_t shadowBuffer;
    std::uint32_t shadowAtlas;
    float shadowTexel;
    float normalStrength;
    glm::vec3 ambientBelow;
    std::uint32_t occlusionTexture;
    glm::vec2 screenTexel;
    float nearPlane;
    std::uint32_t clusterBuffer;
    glm::vec3 cameraForward;
    float clusterSliceScale;
    std::uint32_t clusterLightBuffer;
    float farPlane;
    std::uint32_t accelerationStructure;
    std::uint32_t padding;
};
static_assert(sizeof(FrameData) == 256);

struct LightData {
    glm::vec3 position;
    float range;
    glm::vec3 direction;
    float fullRange;
    glm::vec3 color;
    float cosOuter; // kPointLight for a point light.
    float cosInner;
    std::uint32_t shadow;
    std::array<std::uint32_t, 2> padding;
};
static_assert(sizeof(LightData) == 64);

struct ShadowData {
    glm::mat4 viewProjection;
    glm::vec2 atlasOffset;
    float atlasScale;
    float normalOffset;
};
static_assert(sizeof(ShadowData) == 80);

constexpr std::uint32_t kNoShadow = 0xFFFFFFFF;
// Against shadow acne: depth bias in the shadow map (reverse-Z, so negative), and receivers pushed along
// their normal by this many texels.
constexpr float kShadowDepthBias = -2.0f;
constexpr float kShadowSlopeBias = -2.5f;
constexpr float kShadowNormalOffsetTexels = 1.5f;
/// A spot light's shadow map covers its cone; wider cones are capped here.
constexpr float kMaxShadowHalfAngle = glm::radians(80.0f);
constexpr float kShadowNearPlane = 0.1f; // Meters.

/// The shadow map of a light, in the tile of a square atlas of grid by grid tiles of tileSize texels.
ShadowData ShadowFor(const Light& light, std::uint32_t tile, std::uint32_t grid, std::uint32_t tileSize) {
    const glm::vec3 direction = glm::normalize(light.direction);
    const glm::vec3 up = std::abs(direction.y) > 0.99f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::mat4 view = glm::lookAtRH(light.position, light.position + direction, up);
    const float halfAngle = std::clamp(light.outerAngle, glm::radians(1.0f), kMaxShadowHalfAngle);
    // Nearer than this, nothing casts a shadow: a lamp's own shade, around the light, would darken it.
    const float nearPlane = std::min(kShadowNearPlane, light.range * 0.5f);
    const glm::mat4 projection = math::PerspectiveReverseZ(halfAngle * 2.0f, 1.0f, nearPlane, light.range);
    // One texel of the map covers this much, per meter from the light.
    const float texelAngle = 2.0f * std::tan(halfAngle) / static_cast<float>(tileSize);
    const float scale = 1.0f / static_cast<float>(grid);
    return {projection * view, glm::vec2(static_cast<float>(tile % grid), static_cast<float>(tile / grid)) * scale,
            scale, kShadowNormalOffsetTexels * texelAngle};
}

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
            kNoShadow,
            {}};
}

struct InstanceData {
    glm::mat4 world;
    std::uint32_t vertexBuffer;
    std::uint32_t firstVertex;
    std::uint32_t indexBuffer;
    std::uint32_t material;
};
static_assert(sizeof(resources::Vertex) == 12 * sizeof(float), "Matches kVertexFloats in MeshTypes.slangh");
static_assert(sizeof(InstanceData) == 80);
static_assert(sizeof(resources::SkinWeights) == 24, "Matches SkinVertex in MeshTypes.slangh");

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

MeshPass::MeshPass(rhi::Device& device, rhi::Format colorFormat, rhi::Format depthFormat,
                   const MeshPassSettings& settings)
    : m_device(&device), m_colorFormat(colorFormat), m_depthFormat(depthFormat),
      m_shadowLights(std::min<std::uint32_t>(settings.shadowLights, kMaxShadowedLights)),
      m_shadowTileSize(std::max(settings.shadowResolution, 1U)),
      // The smallest square grid with a tile per light, and one tile when there are none.
      m_shadowGrid(static_cast<std::uint32_t>(std::ceil(std::sqrt(static_cast<float>(std::max(m_shadowLights, 1U)))))),
      m_samples(std::max(settings.samples, 1U)) {}

MeshPass::MeshPass(MeshPass&& other) noexcept
    : m_device(other.m_device), m_colorFormat(other.m_colorFormat), m_depthFormat(other.m_depthFormat),
      m_pipelines(std::exchange(other.m_pipelines, {})), m_shadowPipeline(std::exchange(other.m_shadowPipeline, {})),
      m_shadowAtlas(std::exchange(other.m_shadowAtlas, {})), m_shadowLights(other.m_shadowLights),
      m_shadowTileSize(other.m_shadowTileSize), m_shadowGrid(other.m_shadowGrid), m_samples(other.m_samples),
      m_vertexShader(std::move(other.m_vertexShader)), m_fragmentShader(std::move(other.m_fragmentShader)),
      m_meshes(std::exchange(other.m_meshes, {})), m_materials(std::exchange(other.m_materials, {})),
      m_materialTable(std::exchange(other.m_materialTable, {})),
      m_materialCapacity(std::exchange(other.m_materialCapacity, 0)),
      m_defaultMaterial(std::exchange(other.m_defaultMaterial, {})),
      m_frameSlots(std::exchange(other.m_frameSlots, {})), m_nextFrameSlot(other.m_nextFrameSlot),
      m_skinned(std::exchange(other.m_skinned, {})), m_skinnedCapacity(std::exchange(other.m_skinnedCapacity, 0)),
      m_skinningPipeline(std::exchange(other.m_skinningPipeline, {})) {}

MeshPass::~MeshPass() {
    DestroyPipelines(m_pipelines);
    m_meshes.ForEach([this](const Mesh& mesh) {
        m_device->DestroyBuffer(mesh.vertices);
        m_device->DestroyBuffer(mesh.indices);
        m_device->DestroyBuffer(mesh.skin);
        m_device->DestroyAccelerationStructure(mesh.structure);
    });
    m_device->DestroyBuffer(m_materialTable);
    m_device->DestroyBuffer(m_skinned);
    m_device->DestroyPipeline(m_skinningPipeline);
    for (const FrameSlot& slot : m_frameSlots) {
        m_device->DestroyBuffer(slot.frame);
        m_device->DestroyBuffer(slot.instances);
        m_device->DestroyBuffer(slot.joints);
        m_device->DestroyBuffer(slot.lights);
        m_device->DestroyBuffer(slot.shadows);
        m_device->DestroyBuffer(slot.clusters);
        m_device->DestroyBuffer(slot.clusterLights);
    }
    m_device->DestroyPipeline(m_shadowPipeline);
    m_device->DestroyTexture(m_shadowAtlas);
}

core::Result<MeshPass> MeshPass::Create(rhi::Device& device, rhi::Format colorFormat, rhi::Format depthFormat,
                                        const MeshPassSettings& settings) {
    MeshPass pass(device, colorFormat, depthFormat, settings);
    DAI_TRY(pass.ReplaceShaders(rhi::ToSpirv(kMeshVsSpirv), rhi::ToSpirv(kMeshPsSpirv)));
    DAI_TRY(pass.CreateShadowResources());
    DAI_TRY_ASSIGN(pass.m_skinningPipeline,
                   device.CreateComputePipeline({.shader = rhi::ToSpirv(kSkinningCsSpirv), .name = "Skinning"}));
    // Plain white plastic. glTF's default material is a metal, which looks black without reflections.
    DAI_TRY_ASSIGN(MaterialHandle defaultMaterial, pass.CreateMaterial({.metallicFactor = 0.0f}));
    pass.m_defaultMaterial = defaultMaterial;
    return pass;
}

core::Result<void> MeshPass::CreateShadowResources() {
    using resources::Vertex;
    DAI_TRY_ASSIGN(
        m_shadowPipeline,
        m_device->CreatePipeline({
            .vertexShader = rhi::ToSpirv(kShadowMapVsSpirv),
            .fragmentShader = rhi::ToSpirv(kShadowMapPsSpirv),
            .vertexStride = sizeof(Vertex),
            .vertexAttributes = {{0, rhi::Format::Rgb32Float, static_cast<std::uint32_t>(offsetof(Vertex, position))},
                                 {2, rhi::Format::Rg32Float, static_cast<std::uint32_t>(offsetof(Vertex, uv))}},
            .depthFormat = rhi::Format::D32Float,
            .depthBias = kShadowDepthBias,
            .depthBiasSlope = kShadowSlopeBias,
            // Level walls have one side: culling would let light through them.
            .cull = rhi::CullMode::None,
            .name = "Shadow map",
        }));
    // Without shadows, a texel stands in for the atlas, which shaders still bind.
    const std::uint32_t atlasSize = m_shadowLights == 0 ? 1 : m_shadowGrid * m_shadowTileSize;
    DAI_TRY_ASSIGN(m_shadowAtlas,
                   m_device->CreateTexture({.extent = {atlasSize, atlasSize},
                                            .format = rhi::Format::D32Float,
                                            .usage = rhi::TextureUsage::DepthTarget | rhi::TextureUsage::Sampled,
                                            .name = "Shadow atlas"}));
    for (FrameSlot& slot : m_frameSlots) {
        DAI_TRY_ASSIGN(slot.clusters, m_device->CreateBuffer({.size = kClusterCount * sizeof(glm::uvec2),
                                                              .usage = rhi::BufferUsage::Storage,
                                                              .memory = rhi::MemoryLocation::CpuToGpu,
                                                              .name = "Clusters"}));
        DAI_TRY_ASSIGN(slot.shadows, m_device->CreateBuffer({.size = std::max(m_shadowLights, 1U) * sizeof(ShadowData),
                                                             .usage = rhi::BufferUsage::Storage,
                                                             .memory = rhi::MemoryLocation::CpuToGpu,
                                                             .name = "Shadows"}));
    }
    return {};
}

core::Result<rhi::PipelineHandle> MeshPass::BuildPipeline(std::span<const std::uint32_t> vertexShader,
                                                          std::span<const std::uint32_t> fragmentShader,
                                                          rhi::CullMode cull, bool prepass) {
    using resources::Vertex;
    return m_device->CreatePipeline({
        .vertexShader = vertexShader,
        .fragmentShader = fragmentShader,
        .vertexStride = sizeof(Vertex),
        .vertexAttributes = {{0, rhi::Format::Rgb32Float, static_cast<std::uint32_t>(offsetof(Vertex, position))},
                             {1, rhi::Format::Rgb32Float, static_cast<std::uint32_t>(offsetof(Vertex, normal))},
                             {2, rhi::Format::Rg32Float, static_cast<std::uint32_t>(offsetof(Vertex, uv))},
                             {3, rhi::Format::Rgba32Float, static_cast<std::uint32_t>(offsetof(Vertex, tangent))}},
        .colorFormat = prepass ? kNormalFormat : m_colorFormat,
        .depthFormat = m_depthFormat,
        // The mesh pass keeps the pre-pass's depth: equal depths pass the reverse-Z test.
        .depthWrite = prepass,
        .cull = cull,
        .samples = m_samples,
        .name = prepass ? "Depth and normal pre-pass" : "Mesh",
    });
}

void MeshPass::DestroyPipelines(Pipelines& pipelines) {
    m_device->DestroyPipeline(pipelines.prepass);
    m_device->DestroyPipeline(pipelines.prepassDoubleSided);
    m_device->DestroyPipeline(pipelines.mesh);
    m_device->DestroyPipeline(pipelines.meshDoubleSided);
    pipelines = {};
}

core::Result<void> MeshPass::ReplaceShaders(std::span<const std::uint32_t> vertexShader,
                                            std::span<const std::uint32_t> fragmentShader) {
    const std::vector<std::uint32_t> prepassShader = rhi::ToSpirv(kPrepassPsSpirv);
    Pipelines built;
    const auto build = [&](rhi::PipelineHandle& pipeline, std::span<const std::uint32_t> fragment, rhi::CullMode cull,
                           bool prepass) -> core::Result<void> {
        DAI_TRY_ASSIGN(pipeline, BuildPipeline(vertexShader, fragment, cull, prepass));
        return {};
    };
    core::Result<void> result =
        build(built.prepass, prepassShader, rhi::CullMode::Back, true)
            .and_then([&] { return build(built.prepassDoubleSided, prepassShader, rhi::CullMode::None, true); })
            .and_then([&] { return build(built.mesh, fragmentShader, rhi::CullMode::Back, false); })
            .and_then([&] { return build(built.meshDoubleSided, fragmentShader, rhi::CullMode::None, false); });
    if (!result) {
        DestroyPipelines(built);
        return result;
    }
    DestroyPipelines(m_pipelines);
    m_pipelines = built;
    m_vertexShader.assign(vertexShader.begin(), vertexShader.end());
    m_fragmentShader.assign(fragmentShader.begin(), fragmentShader.end());
    return {};
}

core::Result<void> MeshPass::SetSamples(std::uint32_t samples) {
    if (samples == m_samples) {
        return {};
    }
    const std::uint32_t previous = std::exchange(m_samples, samples);
    // Copies: ReplaceShaders keeps what it is given.
    const std::vector<std::uint32_t> vertexShader = m_vertexShader;
    const std::vector<std::uint32_t> fragmentShader = m_fragmentShader;
    core::Result<void> rebuilt = ReplaceShaders(vertexShader, fragmentShader);
    if (!rebuilt) {
        m_samples = previous;
    }
    return rebuilt;
}

core::Result<MeshHandle> MeshPass::CreateMesh(const resources::MeshData& data, const std::string& name) {
    DAI_ASSERT(!data.vertices.empty() && !data.indices.empty(), "A mesh cannot be empty");
    DAI_ASSERT(data.skin.empty() || data.skin.size() == data.vertices.size(), "A skin needs one entry per vertex");

    const auto vertexBytes = std::as_bytes(std::span(data.vertices));
    const auto indexBytes = std::as_bytes(std::span(data.indices));

    DAI_TRY_ASSIGN(
        rhi::BufferHandle vertices,
        m_device->CreateBuffer({.size = vertexBytes.size(),
                                .usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::TransferDst |
                                         rhi::BufferUsage::Storage | rhi::BufferUsage::AccelerationStructureInput,
                                .name = std::format("{} vertices", name)}));
    core::Result<rhi::BufferHandle> indices =
        m_device->CreateBuffer({.size = indexBytes.size(),
                                .usage = rhi::BufferUsage::Index | rhi::BufferUsage::TransferDst |
                                         rhi::BufferUsage::Storage | rhi::BufferUsage::AccelerationStructureInput,
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
    // The mesh's triangles for rays, built with the uploads right after its vertices arrive.
    const auto vertexCount = static_cast<std::uint32_t>(data.vertices.size());
    const auto indexCount = static_cast<std::uint32_t>(data.indices.size());
    rhi::AccelerationStructureHandle structure;
    if (uploaded) {
        core::Result<rhi::AccelerationStructureHandle> created =
            m_device->CreateAccelerationStructure({.level = rhi::AccelerationStructureLevel::Bottom,
                                                   .geometry = {.vertices = vertices,
                                                                .vertexStride = sizeof(resources::Vertex),
                                                                .vertexCount = vertexCount,
                                                                .indices = *indices,
                                                                .indexCount = indexCount},
                                                   .name = name});
        uploaded = created.and_then([&](rhi::AccelerationStructureHandle built) {
            structure = built;
            return m_device->BuildWithUploads(built);
        });
    }
    if (!uploaded) {
        m_device->DestroyBuffer(vertices);
        m_device->DestroyBuffer(*indices);
        m_device->DestroyBuffer(skin);
        m_device->DestroyAccelerationStructure(structure);
        return tl::unexpected(std::move(uploaded.error()));
    }
    // The box's center, and the farthest vertex from it: not the smallest sphere, but a tight one.
    glm::vec3 low(std::numeric_limits<float>::max());
    glm::vec3 high(std::numeric_limits<float>::lowest());
    for (const resources::Vertex& vertex : data.vertices) {
        low = glm::min(low, vertex.position);
        high = glm::max(high, vertex.position);
    }
    const glm::vec3 center = (low + high) * 0.5f;
    float radius = 0.0f;
    for (const resources::Vertex& vertex : data.vertices) {
        radius = std::max(radius, glm::distance(center, vertex.position));
    }
    return m_meshes.Insert({vertices, *indices, skin, structure, vertexCount, indexCount, glm::vec4(center, radius)});
}

void MeshPass::DestroyMesh(MeshHandle mesh) {
    if (const std::optional<Mesh> removed = m_meshes.Remove(mesh)) {
        m_device->DestroyBuffer(removed->vertices);
        m_device->DestroyBuffer(removed->indices);
        m_device->DestroyBuffer(removed->skin);
        m_device->DestroyAccelerationStructure(removed->structure);
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

    const MaterialHandle material = m_materials.Insert({desc.doubleSided, desc.alphaCutoff > 0.0f});
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
    DAI_TRY(reserve(slot.clusterLights, slot.clusterLightCapacity, m_clusters.lightIndices.size(),
                    kInitialLightCapacity * 16, sizeof(std::uint32_t), "Cluster lights"));
    return {};
}

std::vector<std::uint32_t> ChooseShadowedLights(std::span<const Light> lights, const glm::vec3& camera, std::size_t max,
                                                float maxDistance) {
    std::vector<std::uint32_t> chosen;
    for (std::uint32_t index = 0; index < lights.size(); ++index) {
        const Light& light = lights[index];
        if (light.castsShadows && light.type == LightType::Spot &&
            glm::distance(light.position, camera) - light.range <= maxDistance) {
            chosen.push_back(index);
        }
    }
    const auto distance = [&](std::uint32_t index) { return glm::distance(lights[index].position, camera); };
    std::ranges::stable_sort(chosen, {}, distance);
    chosen.resize(std::min(chosen.size(), max));
    return chosen;
}

core::Result<void> MeshPass::Prepare(const SceneView& view, const FrameTargets& targets) {
    DAI_PROFILE_SCOPE("renderer::MeshPass::Prepare");
    m_prepared = nullptr;
    m_shadowCount = 0;
    m_sorted.clear();
    m_skinningJobs.clear();
    m_traced.clear();
    if (!view.camera.has_value()) {
        return {};
    }
    const CameraView& camera = *view.camera;
    const float aspect =
        static_cast<float>(targets.extent.width) / static_cast<float>(std::max(targets.extent.height, 1U));
    // The renderer's field of view is across the view, so that wider windows see more to the sides.
    const float verticalFov =
        camera.verticalFov.value_or(2.0f * std::atan(std::tan(m_fieldOfView * 0.5f) / std::max(aspect, 0.01f)));
    const float farPlane = std::max(m_viewDistance, camera.nearPlane * 2.0f);
    AssignLightsToClusters(view.lights, {camera.view, verticalFov, aspect, camera.nearPlane, farPlane}, m_clusters);

    FrameSlot& slot = m_frameSlots[m_nextFrameSlot];
    m_nextFrameSlot = (m_nextFrameSlot + 1) % rhi::kFramesInFlight;
    DAI_TRY(PrepareFrameSlot(slot, view));

    const glm::vec3 cameraPosition = glm::inverse(camera.view)[3];
    const glm::mat4 viewProjection =
        math::PerspectiveReverseZ(verticalFov, aspect, camera.nearPlane, farPlane) * camera.view;
    m_viewProjection = viewProjection;
    const FrameData frame{viewProjection,
                          glm::inverse(viewProjection),
                          cameraPosition,
                          view.atmosphere.fogDensity,
                          view.atmosphere.fogColor,
                          static_cast<std::uint32_t>(view.lights.size()),
                          view.atmosphere.ambient,
                          m_device->StorageIndex(slot.lights),
                          m_device->StorageIndex(slot.shadows),
                          m_device->BindlessIndex(m_shadowAtlas),
                          1.0f / static_cast<float>(m_shadowGrid * m_shadowTileSize),
                          m_normalStrength,
                          view.atmosphere.ambientBelow,
                          targets.occlusion ? m_device->BindlessIndex(targets.occlusion) : kNoTexture,
                          glm::vec2(1.0f / static_cast<float>(std::max(targets.extent.width, 1U)),
                                    1.0f / static_cast<float>(std::max(targets.extent.height, 1U))),
                          camera.nearPlane,
                          m_device->StorageIndex(slot.clusters),
                          glm::normalize(-glm::vec3(glm::inverse(camera.view)[2])),
                          ClusterSliceScale(camera.nearPlane, farPlane),
                          m_device->StorageIndex(slot.clusterLights),
                          farPlane,
                          targets.accelerationStructure,
                          0};
    std::memcpy(m_device->Mapped(slot.clusters).data(), m_clusters.ranges.data(),
                m_clusters.ranges.size() * sizeof(glm::uvec2));
    if (!m_clusters.lightIndices.empty()) {
        std::memcpy(m_device->Mapped(slot.clusterLights).data(), m_clusters.lightIndices.data(),
                    m_clusters.lightIndices.size() * sizeof(std::uint32_t));
    }
    std::memcpy(m_device->Mapped(slot.frame).data(), &frame, sizeof(frame));

    auto* lights = reinterpret_cast<LightData*>(m_device->Mapped(slot.lights).data());
    for (std::size_t index = 0; index < view.lights.size(); ++index) {
        lights[index] = ToGpu(view.lights[index]);
    }
    auto* shadows = reinterpret_cast<ShadowData*>(m_device->Mapped(slot.shadows).data());
    for (const std::uint32_t light : ChooseShadowedLights(view.lights, cameraPosition, m_shadowLights, farPlane)) {
        lights[light].shadow = m_shadowCount;
        shadows[m_shadowCount] = ShadowFor(view.lights[light], m_shadowCount, m_shadowGrid, m_shadowTileSize);
        ++m_shadowCount;
    }
    if (!view.jointMatrices.empty()) {
        std::memcpy(m_device->Mapped(slot.joints).data(), view.jointMatrices.data(),
                    view.jointMatrices.size() * sizeof(glm::mat4));
    }

    // Sorted, so that the pipeline and the buffers change as rarely as possible. Instance i is the i-th
    // sorted draw, for the shadow maps and the meshes alike.
    for (std::uint32_t index = 0; index < view.draws.size(); ++index) {
        const DrawItem& draw = view.draws[index];
        if (m_meshes.TryGet(draw.mesh) == nullptr) {
            continue;
        }
        const MaterialHandle material = m_materials.TryGet(draw.material) ? draw.material : m_defaultMaterial;
        const std::uint64_t doubleSided = m_materials.Get(material).doubleSided ? 1 : 0;
        // Beyond the far plane, a rigid draw is not drawn, but may still cast a shadow into the view. Skinned
        // draws move away from their bounds, and are always drawn.
        const glm::vec4& bounds = m_meshes.Get(draw.mesh).bounds;
        const float scale = std::max({glm::length(glm::vec3(draw.world[0])), glm::length(glm::vec3(draw.world[1])),
                                      glm::length(glm::vec3(draw.world[2]))});
        const glm::vec3 center(draw.world * glm::vec4(glm::vec3(bounds), 1.0f));
        const bool visible =
            draw.firstJoint != kNoJoints || glm::distance(center, cameraPosition) - bounds.w * scale <= farPlane;
        m_sorted.push_back({(doubleSided << 32U) | draw.mesh.Index(), draw.mesh, material, index, draw.castsShadows,
                            visible, kNotSkinned});
    }
    std::ranges::sort(m_sorted, {}, &SortedDraw::key);

    // Skinned draws get their vertices from the skinning pass, one after the other in m_skinned. A skinned
    // draw needs the skin of its mesh, and joints that the view holds. Otherwise it is rigid.
    m_skinningJobs.clear();
    std::uint32_t skinnedVertices = 0;
    for (SortedDraw& sorted : m_sorted) {
        const DrawItem& draw = view.draws[sorted.draw];
        const Mesh& mesh = m_meshes.Get(draw.mesh);
        if (mesh.skin && draw.firstJoint != kNoJoints && draw.firstJoint < view.jointMatrices.size()) {
            sorted.firstSkinnedVertex = skinnedVertices;
            m_skinningJobs.push_back({m_device->StorageIndex(mesh.vertices), m_device->StorageIndex(mesh.skin),
                                      draw.firstJoint, skinnedVertices, mesh.vertexCount});
            skinnedVertices += mesh.vertexCount;
        }
    }
    if (skinnedVertices > m_skinnedCapacity) {
        const std::uint64_t capacity = std::max<std::uint64_t>(skinnedVertices, m_skinnedCapacity * 2);
        DAI_TRY_ASSIGN(rhi::BufferHandle created,
                       m_device->CreateBuffer({.size = capacity * sizeof(resources::Vertex),
                                               .usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::Storage |
                                                        rhi::BufferUsage::AccelerationStructureInput,
                                               .name = "Skinned vertices"}));
        m_device->DestroyBuffer(m_skinned);
        m_skinned = created;
        m_skinnedCapacity = capacity;
    }

    auto* instances = reinterpret_cast<InstanceData*>(m_device->Mapped(slot.instances).data());
    m_traced.clear();
    for (std::size_t index = 0; index < m_sorted.size(); ++index) {
        const SortedDraw& sorted = m_sorted[index];
        const DrawItem& draw = view.draws[sorted.draw];
        const Mesh& mesh = m_meshes.Get(draw.mesh);
        const Material& material = m_materials.Get(sorted.material);
        const bool skinned = sorted.firstSkinnedVertex != kNotSkinned;
        // Skinned vertices are in world space already.
        const glm::mat4 world = skinned ? glm::mat4(1.0f) : draw.world;
        instances[index] = {world, m_device->StorageIndex(skinned ? m_skinned : mesh.vertices),
                            skinned ? sorted.firstSkinnedVertex : 0, m_device->StorageIndex(mesh.indices),
                            sorted.material.Index()};
        TracedDraw& traced = m_traced.emplace_back();
        traced.world = world;
        traced.opaque = !material.alphaTested;
        traced.doubleSided = material.doubleSided;
        traced.castsShadows = sorted.castsShadows;
        if (skinned) {
            traced.skinned = {.vertices = m_skinned,
                              .vertexOffset = std::uint64_t{sorted.firstSkinnedVertex} * sizeof(resources::Vertex),
                              .vertexStride = sizeof(resources::Vertex),
                              .vertexCount = mesh.vertexCount,
                              .indices = mesh.indices,
                              .indexCount = mesh.indexCount};
        } else {
            traced.structure = mesh.structure;
        }
    }
    m_counts = {.draws = static_cast<std::uint32_t>(view.draws.size()),
                .lights = static_cast<std::uint32_t>(view.lights.size()),
                .shadowedLights = m_shadowCount};
    for (const SortedDraw& sorted : m_sorted) {
        if (sorted.visible) {
            ++m_counts.drawnDraws;
            m_counts.triangles += m_meshes.Get(sorted.mesh).indexCount / 3;
        }
        if (sorted.castsShadows) {
            m_counts.shadowDraws += m_shadowCount;
        }
    }
    m_prepared = &slot;
    return {};
}

std::uint32_t MeshPass::InstanceDataIndex() const {
    DAI_ASSERT(m_prepared != nullptr, "No frame is prepared");
    return m_device->StorageIndex(m_prepared->instances);
}

void MeshPass::AddSkinning(RenderGraph& graph) {
    if (m_prepared == nullptr || m_skinningJobs.empty()) {
        return;
    }
    PassDesc desc{.name = "Skinning", .buffers = {{m_skinned, rhi::Access::ComputeShaderWrite}}};
    for (const SortedDraw& sorted : m_sorted) {
        if (sorted.firstSkinnedVertex != kNotSkinned) {
            const Mesh& mesh = m_meshes.Get(sorted.mesh);
            desc.buffers.push_back({mesh.vertices, rhi::Access::ComputeShaderRead});
            desc.buffers.push_back({mesh.skin, rhi::Access::ComputeShaderRead});
        }
    }
    graph.AddPass(std::move(desc), [this](rhi::CommandList& commands) {
        DAI_PROFILE_SCOPE("renderer::MeshPass::Skinning");
        commands.BindPipeline(m_skinningPipeline);
        const std::uint32_t joints = m_device->StorageIndex(m_prepared->joints);
        const std::uint32_t target = m_device->StorageIndex(m_skinned);
        // The draws write separate vertices: no dispatch waits for another.
        for (const SkinningJob& job : m_skinningJobs) {
            commands.Push(SkinningPushConstants{job.source, job.skin, joints, job.firstJoint, target, job.firstVertex,
                                                job.vertexCount});
            commands.Dispatch((job.vertexCount + kSkinningGroupSize - 1) / kSkinningGroupSize);
        }
    });
}

void MeshPass::AddShadows(RenderGraph& graph) {
    if (m_prepared == nullptr || m_shadowCount == 0) {
        return;
    }
    graph.AddPass({.name = "Shadows",
                   .rendering = rhi::RenderingDesc{.depthTarget = m_shadowAtlas, .clearDepth = 0.0f},
                   .buffers = {{SkinnedVertices(), rhi::Access::VertexInputRead}}},
                  [this](rhi::CommandList& commands) { RecordShadows(commands); });
}

void MeshPass::BindGeometry(rhi::CommandList& commands, const SortedDraw& draw, rhi::BufferHandle& boundVertices,
                            std::uint64_t& boundOffset, MeshHandle& boundIndices) {
    const Mesh& mesh = m_meshes.Get(draw.mesh);
    const bool skinned = draw.firstSkinnedVertex != kNotSkinned;
    const rhi::BufferHandle vertices = skinned ? m_skinned : mesh.vertices;
    const std::uint64_t offset = skinned ? std::uint64_t{draw.firstSkinnedVertex} * sizeof(resources::Vertex) : 0;
    if (vertices != boundVertices || offset != boundOffset) {
        commands.BindVertexBuffer(vertices, offset);
        boundVertices = vertices;
        boundOffset = offset;
    }
    if (draw.mesh != boundIndices) {
        commands.BindIndexBuffer(mesh.indices, rhi::IndexType::Uint32);
        boundIndices = draw.mesh;
    }
}

void MeshPass::RecordShadows(rhi::CommandList& commands) {
    DAI_PROFILE_SCOPE("renderer::MeshPass::RecordShadows");
    commands.BindPipeline(m_shadowPipeline);
    MeshPushConstants push{
        .frameBuffer = m_device->StorageIndex(m_prepared->frame),
        .instanceBuffer = m_device->StorageIndex(m_prepared->instances),
        .materialBuffer = m_device->StorageIndex(m_materialTable),
        .instanceIndex = 0,
        .materialIndex = 0,
        .shadowIndex = 0,
    };
    for (std::uint32_t shadow = 0; shadow < m_shadowCount; ++shadow) {
        commands.SetViewport(static_cast<std::int32_t>((shadow % m_shadowGrid) * m_shadowTileSize),
                             static_cast<std::int32_t>((shadow / m_shadowGrid) * m_shadowTileSize), m_shadowTileSize,
                             m_shadowTileSize);
        push.shadowIndex = shadow;
        rhi::BufferHandle boundVertices;
        std::uint64_t boundOffset = 0;
        MeshHandle boundIndices;
        for (std::uint32_t index = 0; index < m_sorted.size(); ++index) {
            const SortedDraw& draw = m_sorted[index];
            if (!draw.castsShadows) {
                continue;
            }
            const Mesh& mesh = m_meshes.Get(draw.mesh);
            BindGeometry(commands, draw, boundVertices, boundOffset, boundIndices);
            push.instanceIndex = index;
            push.materialIndex = draw.material.Index();
            commands.Push(push);
            commands.DrawIndexed(mesh.indexCount);
        }
    }
}

void MeshPass::RecordPrepass(rhi::CommandList& commands) {
    DAI_PROFILE_SCOPE("renderer::MeshPass::RecordPrepass");
    RecordDraws(commands, m_pipelines.prepass, m_pipelines.prepassDoubleSided);
}

void MeshPass::Record(rhi::CommandList& commands) {
    DAI_PROFILE_SCOPE("renderer::MeshPass::Record");
    RecordDraws(commands, m_pipelines.mesh, m_pipelines.meshDoubleSided);
}

std::uint32_t MeshPass::FrameDataIndex() const {
    DAI_ASSERT(m_prepared != nullptr, "No frame is prepared");
    return m_device->StorageIndex(m_prepared->frame);
}

void MeshPass::RecordDraws(rhi::CommandList& commands, rhi::PipelineHandle oneSided, rhi::PipelineHandle doubleSided) {
    if (m_prepared == nullptr) {
        return;
    }
    MeshPushConstants push{
        .frameBuffer = m_device->StorageIndex(m_prepared->frame),
        .instanceBuffer = m_device->StorageIndex(m_prepared->instances),
        .materialBuffer = m_device->StorageIndex(m_materialTable),
        .instanceIndex = 0,
        .materialIndex = 0,
        .shadowIndex = 0,
    };
    rhi::PipelineHandle boundPipeline;
    rhi::BufferHandle boundVertices;
    std::uint64_t boundOffset = 0;
    MeshHandle boundIndices;
    for (std::uint32_t index = 0; index < m_sorted.size(); ++index) {
        const SortedDraw& draw = m_sorted[index];
        if (!draw.visible) {
            continue;
        }
        const Mesh& mesh = m_meshes.Get(draw.mesh);
        const rhi::PipelineHandle pipeline = m_materials.Get(draw.material).doubleSided ? doubleSided : oneSided;
        if (pipeline != boundPipeline) {
            commands.BindPipeline(pipeline);
            boundPipeline = pipeline;
        }
        BindGeometry(commands, draw, boundVertices, boundOffset, boundIndices);
        push.instanceIndex = index;
        push.materialIndex = draw.material.Index();
        commands.Push(push);
        commands.DrawIndexed(mesh.indexCount);
    }
}

} // namespace daidalos::renderer
