// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "MeshPass.hpp"

#include <daidalos/core/Assert.hpp>
#include <daidalos/core/Profile.hpp>
#include <daidalos/math/Projection.hpp>
#include <daidalos/rhi/Spirv.hpp>

#include <glm/common.hpp>
#include <glm/exponential.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/vec2.hpp>
#include <shaders/Composite.ps.spv.h>
#include <shaders/Composite.vs.spv.h>
#include <shaders/EmissiveAverage.cs.spv.h>
#include <shaders/EmissiveTriangles.cs.spv.h>
#include <shaders/GBuffer.ps.spv.h>
#include <shaders/GBuffer.vs.spv.h>
#include <shaders/Glass.ps.spv.h>
#include <shaders/Skinning.cs.spv.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <format>
#include <numbers>
#include <utility>

#include "RayTracingScene.hpp"
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
};

// Matches CompositePushConstants in shaders/Composite.slang.
struct CompositePushConstants {
    std::uint32_t frameBuffer;
    std::uint32_t baseColor;
    std::uint32_t emission;
    std::uint32_t diffuse;
    std::uint32_t specular;
    std::uint32_t shading;
    std::uint32_t depth;
    std::uint32_t flags;
};
// Matches the flags in shaders/Composite.slang.
constexpr std::uint32_t kFogOnly = 1;

struct SkinningPushConstants {
    std::uint32_t source;
    std::uint32_t skin;
    std::uint32_t joints;
    std::uint32_t firstJoint;
    std::uint32_t target;
    std::uint32_t firstVertex;
    std::uint32_t vertexCount;
    std::uint32_t previousJoints;
    std::uint32_t previousTarget;
};
constexpr std::uint32_t kSkinningGroupSize = 64;

// Match EmissiveAveragePushConstants and EmissiveTrianglesPushConstants in shaders/EmissiveAverage.slang and
// shaders/EmissiveTriangles.slang.
struct EmissiveAveragePushConstants {
    std::uint32_t vertexBuffer;
    std::uint32_t indexBuffer;
    std::uint32_t materialBuffer;
    std::uint32_t material;
    std::uint32_t output;
    std::uint32_t triangleCount;
};
struct EmissiveTrianglesPushConstants {
    std::uint32_t instanceBuffer;
    std::uint32_t instance;
    std::uint32_t firstTriangle;
    std::uint32_t triangleCount;
    std::uint32_t emission;
    std::uint32_t output;
    std::uint32_t twoSided;
    std::uint32_t padding;
};
constexpr std::uint32_t kEmissiveGroupSize = 64;

// Match EmissiveDraw and EmissiveTriangle in shaders/Lights.slangh.
struct EmissiveDrawData {
    std::uint32_t firstTriangle;
    std::uint32_t triangleCount;
    std::uint32_t aliasTable;
    std::uint32_t padding;
};
constexpr std::uint64_t kEmissiveTriangleSize = 64;

struct FrameData {
    glm::mat4 viewProjection;
    glm::mat4 inverseViewProjection;
    glm::vec3 cameraPosition;
    float fogDensity;
    glm::vec3 fogAlbedo;
    std::uint32_t lightCount;
    std::uint32_t lightBuffer;
    std::uint32_t skyTexture;
    float skyIntensity;
    std::uint32_t frameIndex;
    float normalStrength;
    std::uint32_t indirectTexture;
    glm::vec2 screenTexel;
    float nearPlane;
    std::uint32_t sourceCount;
    std::uint32_t aliasBuffer;
    float farPlane;
    glm::vec3 cameraForward;
    std::uint32_t emissiveDrawBuffer;
    std::uint32_t accelerationStructure;
    float skyYaw;
    glm::vec2 jitter;
    glm::mat4 unjitteredViewProjection;
    glm::mat4 previousViewProjection;
    std::uint32_t reflectionTexture;
    std::uint32_t backgroundTexture;
    std::uint32_t depthTexture;
    float exposure;
    std::uint32_t emissiveTriangleBuffer;
    std::uint32_t emissiveTriangleCount;
    float sharpReflections;
    float mipBias;
    glm::uvec2 renderSize;
    glm::uvec2 previousRenderSize;
    glm::uvec2 targetSize;
    float fogAnisotropy;
    float time;
    std::uint32_t fogVolumeBuffer;
    std::uint32_t fogVolumeCount;
    std::uint32_t fogTexture;
    std::uint32_t fogAliasBuffer;
    std::uint32_t decalBuffer; // The frame's decals (ADR-0060), and how many.
    std::uint32_t decalCount;
    std::uint32_t debugFlags; // kDebugDecals shows what decals cover.
    std::uint32_t framePadding;
};
static_assert(sizeof(FrameData) == 464);

/// A decal as shaders read it (ADR-0060). Matches its twin in shaders/Decals.slangh.
struct DecalData {
    glm::mat4 worldToBox;
    glm::vec3 forward;
    float opacity;
    glm::vec3 color;
    float roughness;
    std::uint32_t albedo;
    std::uint32_t normal;
    float metallic;
    float angleFade;
    glm::vec3 right;
    float texelSize;
    glm::vec3 up;
    std::uint32_t padding;
};
static_assert(sizeof(DecalData) == 144);

/// A box of fog (ADR-0051). Matches its twin in shaders/Fog.slangh.
struct FogVolumeData {
    glm::mat4 worldToBox;
    glm::vec3 albedo;
    float density;
    glm::vec3 noiseVelocity;
    float noiseFrequency;
    float noise;
    float edge;
    /// Halvings of the density per unit of the box's height, from its bottom; 0 for none.
    float heightFalloff;
    float padding;
    /// Radiance given off per unit of extinction (FogVolume::glow).
    glm::vec3 glow;
    float padding2;
};
static_assert(sizeof(FogVolumeData) == 128);

/// Matches kNoTexture in MeshTypes.slangh: no texture, or no buffer.
constexpr std::uint32_t kNoTexture = 0xFFFFFFFF;

struct LightData {
    glm::vec3 position;
    float range;
    glm::vec3 direction;
    std::uint32_t profile; // Bindless index of its profile (ADR-0057), or kNoTexture.
    glm::vec3 color;
    float cosOuter; // kPointLight for a point light.
    float cosInner;
    float radius;         // Directional lights: the tangent of their angular radius.
    std::uint32_t kind;   // kLightNear or kLightDirectional.
    std::uint32_t cookie; // Bindless index of its cookie, or kNoTexture.
    glm::vec3 up;         // The cookie's up.
    float cookieScale;    // One over the tangent of the cone's outer angle: the cookie's edge.
};
static_assert(sizeof(LightData) == 80);

/// The cosine of the outer angle of a point light: every direction is inside.
constexpr float kPointLight = -2.0f;
/// Match the kinds of light in MeshTypes.slangh: lights with a position, and directional lights.
constexpr std::uint32_t kLightNear = 0;
constexpr std::uint32_t kLightDirectional = 1;

/// A light as shaders read it, its color times the frame's exposure (ADR-0043), with the bindless indices of
/// its profile and cookie (ADR-0057).
LightData ToGpu(const Light& light, float exposure, std::uint32_t profile, std::uint32_t cookie) {
    if (light.type == LightType::Directional) {
        return {glm::vec3(0.0f),
                0.0f,
                glm::normalize(light.direction),
                kNoTexture,
                light.color * exposure,
                kPointLight,
                kPointLight + 1.0f,
                std::tan(std::clamp(light.angularRadius, 0.0f, 1.0f)),
                kLightDirectional,
                kNoTexture,
                glm::vec3(0.0f, 1.0f, 0.0f),
                1.0f};
    }
    const bool spot = light.type == LightType::Spot;
    const float length = glm::length(light.direction);
    return {light.position,
            light.range,
            length > 0.0f ? light.direction / length : glm::vec3(0.0f, 0.0f, -1.0f),
            profile,
            light.color * exposure,
            spot ? std::cos(light.outerAngle) : kPointLight,
            spot ? std::cos(light.innerAngle) : kPointLight + 1.0f,
            std::max(light.radius, 0.0f),
            kLightNear,
            spot ? cookie : kNoTexture,
            light.up,
            spot ? 1.0f / std::max(std::tan(std::clamp(light.outerAngle, 0.01f, 1.5f)), 1e-3f) : 1.0f};
}

struct InstanceData {
    glm::mat4 world;
    std::uint32_t vertexBuffer;
    std::uint32_t firstVertex;
    std::uint32_t indexBuffer;
    std::uint32_t material;
    glm::mat4 previousWorld;
    std::uint32_t previousPositions;
    float emissiveScale; // Multiplies its material's emission (ADR-0057).
    /// The frame's decals that touch it, a bit each (ADR-0060).
    std::array<std::uint32_t, 2> decalMask;
};
static_assert(sizeof(resources::Vertex) == 12 * sizeof(float), "Matches kVertexFloats in MeshTypes.slangh");
static_assert(sizeof(InstanceData) == 160);
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
    float transmission;
    float ior;
    std::uint32_t thin;
    /// Thick glass: the fraction of each color lost per meter, as an exponent: exp(-absorption * meters).
    glm::vec3 absorption;
    std::uint32_t emissiveLight;
    // The layers over the base (ADR-0056).
    float clearcoat;
    float clearcoatRoughness;
    float sheenRoughness;
    std::uint32_t layerPadding;
    glm::vec3 sheenColor;
    float layerPadding2;
};
static_assert(sizeof(GpuMaterial) == 128);

/// The absorption coefficient of glass that gives white light this color after this distance.
glm::vec3 Absorption(const glm::vec3& color, float distance) {
    if (!(distance > 0.0f) || std::isinf(distance)) {
        return glm::vec3(0.0f);
    }
    return -glm::log(glm::clamp(color, glm::vec3(1e-4f), glm::vec3(1.0f))) / distance;
}

constexpr std::uint32_t kInitialMaterialCapacity = 64;
constexpr std::uint64_t kInitialInstanceCapacity = 256;
constexpr std::uint64_t kInitialJointCapacity = 256;
constexpr std::uint64_t kInitialLightCapacity = 64;
constexpr std::uint64_t kInitialFogVolumeCapacity = 8;
/// Decals drawn in a frame, the nearest to the camera: a draw's mask has a bit for each (ADR-0060).
constexpr std::size_t kMaxDecals = 64;

/// A decal's box in world space, to test draws against.
struct DecalBox {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
};

/// The bits of the decals whose boxes a draw's bounding sphere touches.
std::array<std::uint32_t, 2> DecalMaskOf(const glm::vec4& bounds, const glm::mat4& world,
                                         std::span<const DecalBox> boxes) {
    std::array<std::uint32_t, 2> mask{0, 0};
    if (boxes.empty()) {
        return mask;
    }
    const float scale = std::max(
        {glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1])), glm::length(glm::vec3(world[2]))});
    const glm::vec3 center(world * glm::vec4(glm::vec3(bounds), 1.0f));
    const float radius = bounds.w * scale;
    for (std::size_t index = 0; index < boxes.size(); ++index) {
        const glm::vec3 nearest = glm::clamp(center, boxes[index].min, boxes[index].max);
        if (glm::dot(nearest - center, nearest - center) <= radius * radius) {
            mask[index / 32] |= 1U << (index % 32);
        }
    }
    return mask;
}
/// A light that moved farther than this, in meters, or turned by more than this cosine, since the last frame
/// moved: the histories that its light fills must follow it at once.
constexpr float kMovedDistance = 0.001f;
constexpr float kMovedCosine = 0.99999f;
/// Or whose brightness changed by more than this fraction.
constexpr float kMovedBrightness = 0.01f;
/// Grows an array of a frame slot to hold count elements, doubling, and never to zero. The CPU writes it,
/// unless it is in GPU memory.
core::Result<void> Reserve(rhi::Device& device, rhi::BufferHandle& buffer, std::uint64_t& capacity, std::uint64_t count,
                           std::uint64_t initial, std::uint64_t elementSize, const char* name,
                           rhi::MemoryLocation memory = rhi::MemoryLocation::CpuToGpu) {
    if (buffer && capacity >= count) {
        return {};
    }
    const std::uint64_t grown = std::max({initial, count, capacity * 2});
    DAI_TRY_ASSIGN(
        rhi::BufferHandle created,
        device.CreateBuffer(
            {.size = grown * elementSize, .usage = rhi::BufferUsage::Storage, .memory = memory, .name = name}));
    device.DestroyBuffer(buffer);
    buffer = created;
    capacity = grown;
    return {};
}

std::uint64_t Packed(std::uint32_t index, std::uint32_t generation) {
    return (std::uint64_t{generation} << 32U) | index;
}

/// What names an emissive draw from frame to frame: its id, its mesh and its material, apart from the names
/// of analytic lights.
std::uint64_t EmissiveDrawKey(SceneId id, std::uint32_t position, MeshHandle mesh, MaterialHandle material) {
    std::uint64_t key = IdentityKey(id, position) ^ 0x5EEDE1155105ULL;
    key = (key ^ Packed(mesh.Index(), mesh.Generation())) * 0x9E3779B97F4A7C15ULL;
    return (key ^ Packed(material.Index(), material.Generation())) * 0xBF58476D1CE4E5B9ULL;
}

/// How much a world matrix scales areas: the cube of its volume scale, squared.
float AreaScale(const glm::mat4& world) {
    return std::pow(std::abs(glm::determinant(glm::mat3(world))), 2.0f / 3.0f);
}

} // namespace

MeshPass::MeshPass(rhi::Device& device, rhi::Format colorFormat, rhi::Format depthFormat)
    : m_device(&device), m_colorFormat(colorFormat), m_depthFormat(depthFormat) {}

MeshPass::MeshPass(MeshPass&& other) noexcept
    : m_device(other.m_device), m_colorFormat(other.m_colorFormat), m_depthFormat(other.m_depthFormat),
      m_pipelines(std::exchange(other.m_pipelines, {})), m_vertexShader(std::move(other.m_vertexShader)),
      m_fragmentShader(std::move(other.m_fragmentShader)), m_meshes(std::exchange(other.m_meshes, {})),
      m_materials(std::exchange(other.m_materials, {})), m_materialTable(std::exchange(other.m_materialTable, {})),
      m_materialCapacity(std::exchange(other.m_materialCapacity, 0)),
      m_defaultMaterial(std::exchange(other.m_defaultMaterial, {})),
      m_frameSlots(std::exchange(other.m_frameSlots, {})), m_nextFrameSlot(other.m_nextFrameSlot),
      m_skinned(std::exchange(other.m_skinned, {})), m_previousSkinned(std::exchange(other.m_previousSkinned, {})),
      m_skinnedCapacity(std::exchange(other.m_skinnedCapacity, 0)),
      m_skinningPipeline(std::exchange(other.m_skinningPipeline, {})),
      m_emissiveSources(std::exchange(other.m_emissiveSources, {})),
      m_emissiveAverage(std::exchange(other.m_emissiveAverage, {})),
      m_emissiveTriangles(std::exchange(other.m_emissiveTriangles, {})) {}

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
    m_device->DestroyBuffer(m_previousSkinned);
    m_device->DestroyPipeline(m_skinningPipeline);
    m_device->DestroyPipeline(m_emissiveAverage);
    m_device->DestroyPipeline(m_emissiveTriangles);
    for (auto& [key, source] : m_emissiveSources) {
        DestroyEmissiveSource(source);
    }
    for (const FrameSlot& slot : m_frameSlots) {
        m_device->DestroyBuffer(slot.frame);
        m_device->DestroyBuffer(slot.instances);
        m_device->DestroyBuffer(slot.joints);
        m_device->DestroyBuffer(slot.lights);
        m_device->DestroyBuffer(slot.sources);
        m_device->DestroyBuffer(slot.fogSources);
        m_device->DestroyBuffer(slot.lightMap);
        m_device->DestroyBuffer(slot.emissiveDraws);
        m_device->DestroyBuffer(slot.emissiveTriangles);
        m_device->DestroyBuffer(slot.fogVolumes);
        m_device->DestroyBuffer(slot.decals);
    }
}

core::Result<MeshPass> MeshPass::Create(rhi::Device& device, rhi::Format colorFormat, rhi::Format depthFormat) {
    MeshPass pass(device, colorFormat, depthFormat);
    DAI_TRY(pass.ReplaceShaders(rhi::ToSpirv(kGBufferVsSpirv), rhi::ToSpirv(kGBufferPsSpirv)));
    DAI_TRY_ASSIGN(pass.m_skinningPipeline,
                   device.CreateComputePipeline({.shader = rhi::ToSpirv(kSkinningCsSpirv), .name = "Skinning"}));
    DAI_TRY_ASSIGN(pass.m_emissiveAverage,
                   device.CreateComputePipeline(
                       {.shader = rhi::ToSpirv(kEmissiveAverageCsSpirv), .name = "Emission of triangles"}));
    DAI_TRY_ASSIGN(pass.m_emissiveTriangles,
                   device.CreateComputePipeline(
                       {.shader = rhi::ToSpirv(kEmissiveTrianglesCsSpirv), .name = "Emissive triangles"}));
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
        .colorFormat = kNormalFormat,
        .moreColorFormats = {kMotionFormat, kBaseColorFormat, kEmissionFormat, kLayersFormat},
        .depthFormat = m_depthFormat,
        .cull = cull,
        .name = "G-buffer",
    });
}

void MeshPass::DestroyPipelines(Pipelines& pipelines) {
    m_device->DestroyPipeline(pipelines.composite);
    m_device->DestroyPipeline(pipelines.gbuffer);
    m_device->DestroyPipeline(pipelines.gbufferDoubleSided);
    m_device->DestroyPipeline(pipelines.glass);
    m_device->DestroyPipeline(pipelines.glassDoubleSided);
    pipelines = {};
}

core::Result<void> MeshPass::ReplaceShaders(std::span<const std::uint32_t> vertexShader,
                                            std::span<const std::uint32_t> fragmentShader) {
    Pipelines built;
    const auto build = [&](rhi::PipelineHandle& pipeline, rhi::CullMode cull) -> core::Result<void> {
        DAI_TRY_ASSIGN(pipeline, BuildPipeline(vertexShader, fragmentShader, cull));
        return {};
    };
    core::Result<void> result =
        build(built.gbuffer, rhi::CullMode::Back)
            .and_then([&] { return build(built.gbufferDoubleSided, rhi::CullMode::None); })
            .and_then([&]() -> core::Result<void> {
                // Glass tests the G-buffer's depth itself, which it also reads: no depth target (ADR-0042).
                const std::vector<std::uint32_t> glassShader = rhi::ToSpirv(kGlassPsSpirv);
                using resources::Vertex;
                for (const auto& [pipeline, cull] : {std::pair{&built.glass, rhi::CullMode::Back},
                                                     std::pair{&built.glassDoubleSided, rhi::CullMode::None}}) {
                    DAI_TRY_ASSIGN(
                        *pipeline,
                        m_device->CreatePipeline(
                            {.vertexShader = vertexShader,
                             .fragmentShader = glassShader,
                             .vertexStride = sizeof(Vertex),
                             .vertexAttributes =
                                 {{0, rhi::Format::Rgb32Float, static_cast<std::uint32_t>(offsetof(Vertex, position))},
                                  {1, rhi::Format::Rgb32Float, static_cast<std::uint32_t>(offsetof(Vertex, normal))},
                                  {2, rhi::Format::Rg32Float, static_cast<std::uint32_t>(offsetof(Vertex, uv))},
                                  {3, rhi::Format::Rgba32Float, static_cast<std::uint32_t>(offsetof(Vertex, tangent))}},
                             .colorFormat = m_colorFormat,
                             .cull = cull,
                             .name = "Glass"}));
                }
                return {};
            })
            .and_then([&]() -> core::Result<void> {
                DAI_TRY_ASSIGN(built.composite,
                               m_device->CreatePipeline({.vertexShader = rhi::ToSpirv(kCompositeVsSpirv),
                                                         .fragmentShader = rhi::ToSpirv(kCompositePsSpirv),
                                                         .colorFormat = m_colorFormat,
                                                         .cull = rhi::CullMode::None,
                                                         .name = "Composite"}));
                return {};
            });
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
    return m_meshes.Insert(
        {vertices, *indices, skin, structure, vertexCount, indexCount, glm::vec4(center, radius), low.y});
}

std::optional<MeshPass::Geometry> MeshPass::GeometryOf(MeshHandle mesh) const {
    const Mesh* found = m_meshes.TryGet(mesh);
    if (found == nullptr) {
        return std::nullopt;
    }
    return Geometry{.vertices = found->vertices,
                    .indices = found->indices,
                    .indexCount = found->indexCount,
                    .lowest = found->lowest};
}

MeshPass::MaterialSlot MeshPass::MaterialOf(MaterialHandle material) const {
    const Material* found = m_materials.TryGet(material);
    if (found == nullptr) {
        return {.index = m_defaultMaterial.Index(), .doubleSided = false};
    }
    return {.index = material.Index(), .doubleSided = found->doubleSided};
}

void MeshPass::DestroyMesh(MeshHandle mesh) {
    if (const std::optional<Mesh> removed = m_meshes.Remove(mesh)) {
        m_device->DestroyBuffer(removed->vertices);
        m_device->DestroyBuffer(removed->indices);
        m_device->DestroyBuffer(removed->skin);
        m_device->DestroyAccelerationStructure(removed->structure);
    }
    const std::uint64_t packed = Packed(mesh.Index(), mesh.Generation());
    std::erase_if(m_emissiveSources, [&](auto& entry) {
        if (entry.first.first != packed) {
            return false;
        }
        DestroyEmissiveSource(entry.second);
        return true;
    });
}

void MeshPass::DestroyEmissiveSource(EmissiveSource& source) {
    m_device->DestroyBuffer(source.readback);
    m_device->DestroyBuffer(source.emission);
    m_device->DestroyBuffer(source.aliasTable);
    source = {};
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
        .transmission = std::clamp(desc.transmission, 0.0f, 1.0f),
        .ior = std::max(desc.ior, 1.0f),
        .thin = desc.thin ? 1U : 0U,
        .absorption = Absorption(desc.attenuationColor, desc.attenuationDistance),
        .emissiveLight = desc.emissiveLight ? 1U : 0U,
        .clearcoat = std::clamp(desc.clearcoat, 0.0f, 1.0f),
        .clearcoatRoughness = std::clamp(desc.clearcoatRoughness, 0.0f, 1.0f),
        .sheenRoughness = std::clamp(desc.sheenRoughness, 0.0f, 1.0f),
        .layerPadding = 0,
        .sheenColor = glm::clamp(desc.sheenColor, glm::vec3(0.0f), glm::vec3(1.0f)),
        .layerPadding2 = 0.0f,
    };

    const bool glass = desc.transmission > 0.0f;
    const MaterialHandle material = m_materials.Insert(
        {desc.doubleSided, desc.alphaCutoff > 0.0f, glass,
         desc.emissiveLight && !glass && glm::any(glm::greaterThan(desc.emissiveFactor, glm::vec3(0.0f)))});
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
    const std::uint64_t packed = Packed(material.Index(), material.Generation());
    std::erase_if(m_emissiveSources, [&](auto& entry) {
        if (entry.first.second != packed) {
            return false;
        }
        DestroyEmissiveSource(entry.second);
        return true;
    });
}

core::Result<void> MeshPass::PrepareFrameSlot(FrameSlot& slot, const SceneView& view) {
    const auto reserve = [this](rhi::BufferHandle& buffer, std::uint64_t& capacity, std::uint64_t count,
                                std::uint64_t initial, std::uint64_t elementSize, const char* name) {
        return Reserve(*m_device, buffer, capacity, count, initial, elementSize, name);
    };
    std::uint64_t frameCapacity = slot.frame ? 1 : 0;
    DAI_TRY(reserve(slot.frame, frameCapacity, 1, 1, sizeof(FrameData), "Frame data"));
    DAI_TRY(reserve(slot.instances, slot.instanceCapacity, view.draws.size(), kInitialInstanceCapacity,
                    sizeof(InstanceData), "Instances"));
    // The joints, then the same joints in the previous frame.
    DAI_TRY(reserve(slot.joints, slot.jointCapacity, view.jointMatrices.size() * 2, kInitialJointCapacity,
                    sizeof(glm::mat4), "Joints"));
    DAI_TRY(reserve(slot.lights, slot.lightCapacity, view.lights.size(), kInitialLightCapacity, sizeof(LightData),
                    "Lights"));
    DAI_TRY(reserve(slot.fogVolumes, slot.fogVolumeCapacity, view.fogVolumes.size(), kInitialFogVolumeCapacity,
                    sizeof(FogVolumeData), "Fog volumes"));
    DAI_TRY(reserve(slot.decals, slot.decalCapacity, kMaxDecals, kMaxDecals, sizeof(DecalData), "Decals"));
    return {};
}

MeshPass::EmissiveSource* MeshPass::FindEmissiveSource(MeshHandle meshHandle, MaterialHandle material) {
    const EmissiveKey key{Packed(meshHandle.Index(), meshHandle.Generation()),
                          Packed(material.Index(), material.Generation())};
    auto found = m_emissiveSources.find(key);
    if (found == m_emissiveSources.end()) {
        const Mesh& mesh = m_meshes.Get(meshHandle);
        EmissiveSource source{.triangleCount = mesh.indexCount / 3};
        core::Result<rhi::BufferHandle> readback =
            m_device->CreateBuffer({.size = std::uint64_t{source.triangleCount} * sizeof(glm::vec4),
                                    .usage = rhi::BufferUsage::Storage,
                                    .memory = rhi::MemoryLocation::GpuToCpu,
                                    .name = "Emission of triangles"});
        if (!readback) {
            return nullptr;
        }
        source.readback = *readback;
        found = m_emissiveSources.emplace(key, source).first;
        m_averageJobs.push_back({key, meshHandle, material});
    }
    EmissiveSource& source = found->second;
    if (source.ready || !source.averagedAt.has_value() || m_prepareCount < *source.averagedAt + rhi::kFramesInFlight) {
        return &source;
    }
    // The GPU has written the emission: each triangle's, and its area, from which the triangles are drawn.
    const std::span<const std::byte> bytes = m_device->Mapped(source.readback);
    std::vector<glm::vec4> triangles(source.triangleCount);
    std::memcpy(triangles.data(), bytes.data(), triangles.size() * sizeof(glm::vec4));
    std::vector<float> powers(triangles.size());
    source.power = 0.0f;
    for (std::size_t index = 0; index < triangles.size(); ++index) {
        powers[index] = std::max(Luminance(glm::vec3(triangles[index])), 0.0f) * triangles[index].w;
        source.power += powers[index];
    }
    const std::vector<AliasEntry> table = BuildAliasTable(powers);
    core::Result<void> made = m_device
                                  ->CreateBuffer({.size = triangles.size() * sizeof(glm::vec4),
                                                  .usage = rhi::BufferUsage::Storage,
                                                  .memory = rhi::MemoryLocation::CpuToGpu,
                                                  .name = "Emission of triangles"})
                                  .and_then([&](rhi::BufferHandle emission) {
                                      source.emission = emission;
                                      return m_device->CreateBuffer({.size = table.size() * sizeof(AliasEntry),
                                                                     .usage = rhi::BufferUsage::Storage,
                                                                     .memory = rhi::MemoryLocation::CpuToGpu,
                                                                     .name = "Emissive triangles by power"});
                                  })
                                  .map([&](rhi::BufferHandle aliasTable) { source.aliasTable = aliasTable; });
    if (!made) {
        DestroyEmissiveSource(source);
        m_emissiveSources.erase(found);
        return nullptr;
    }
    std::memcpy(m_device->Mapped(source.emission).data(), triangles.data(), triangles.size() * sizeof(glm::vec4));
    std::memcpy(m_device->Mapped(source.aliasTable).data(), table.data(), table.size() * sizeof(AliasEntry));
    m_device->DestroyBuffer(source.readback);
    source.readback = {};
    source.ready = true;
    return &source;
}

std::vector<glm::vec3> MeshPass::TriangleEmission(MeshHandle mesh, MaterialHandle material) const {
    const auto found = m_emissiveSources.find(
        {Packed(mesh.Index(), mesh.Generation()), Packed(material.Index(), material.Generation())});
    if (found == m_emissiveSources.end() || !found->second.ready) {
        return {};
    }
    const EmissiveSource& source = found->second;
    std::vector<glm::vec4> triangles(source.triangleCount);
    std::memcpy(triangles.data(), m_device->Mapped(source.emission).data(), triangles.size() * sizeof(glm::vec4));
    std::vector<glm::vec3> emission;
    emission.reserve(triangles.size());
    for (const glm::vec4& triangle : triangles) {
        emission.emplace_back(triangle);
    }
    return emission;
}

core::Result<MeshPass::FrameLights> MeshPass::PrepareLights(FrameSlot& slot, const SceneView& view, float exposure,
                                                            const glm::vec3& cameraPosition) {
    // The analytic lights, each its own source. Each source is also weighed for the volumetric fog by its
    // distance to the camera; a directional light, everywhere, as if it were kFogReach away (ADR-0051).
    const auto lightCount = static_cast<std::uint32_t>(view.lights.size());
    auto* lights = reinterpret_cast<LightData*>(m_device->Mapped(slot.lights).data());
    std::vector<float> powers;
    std::vector<float> fogPowers;
    std::vector<LightRange> ranges;
    // Lights that moved since the last frame, whose light the histories must follow at once.
    std::vector<bool> moved;
    std::unordered_map<std::uint64_t, LightPlace> places;
    for (std::uint32_t index = 0; index < lightCount; ++index) {
        const Light& light = view.lights[index];
        const auto bindless = [this](rhi::TextureHandle texture) {
            return texture.IsNull() ? kNoTexture : m_device->BindlessIndex(texture);
        };
        lights[index] = ToGpu(light, exposure, bindless(light.profile), bindless(light.cookie));
        powers.push_back(LightPower(light));
        fogPowers.push_back(FogSourceWeight(powers.back(), light.type == LightType::Directional
                                                               ? kFogReach
                                                               : glm::distance(light.position, cameraPosition)));
        const std::uint64_t key = IdentityKey(light.id, index);
        ranges.push_back({key, index, 1});
        const auto previous = m_previousPlaces.find(key);
        // A light that changed its brightness, as a flickering flame, moved too (ADR-0053).
        const float brightness = glm::dot(light.color, glm::vec3(0.2126f, 0.7152f, 0.0722f));
        moved.push_back(previous != m_previousPlaces.end() &&
                        (glm::distance(previous->second.position, light.position) > kMovedDistance ||
                         glm::dot(previous->second.direction, light.direction) < kMovedCosine ||
                         std::abs(previous->second.brightness - brightness) >
                             kMovedBrightness * std::max(previous->second.brightness, brightness)));
        places.emplace(key, LightPlace{light.position, light.direction, brightness});
    }
    m_previousPlaces = std::move(places);

    // Then the emissive draws, each a source of its triangles, once their emission is known (ADR-0044). The
    // flux of a surface that sends the same light everywhere is its radiance times its area times pi.
    std::vector<EmissiveDrawData> draws;
    m_emissiveJobs.clear();
    std::uint32_t triangleCount = 0;
    for (std::uint32_t index = 0; index < m_sorted.size(); ++index) {
        const SortedDraw& sorted = m_sorted[index];
        const Material& material = m_materials.Get(sorted.material);
        if (!material.emissive || sorted.glass) {
            continue;
        }
        const EmissiveSource* source = FindEmissiveSource(sorted.mesh, sorted.material);
        if (source == nullptr || !source->ready || source->power <= 0.0f) {
            continue;
        }
        const DrawItem& draw = view.draws[sorted.draw];
        // A lamp switched off gives no light; one that flickers gives what it glows (ADR-0057).
        if (draw.emissiveScale <= 0.0f) {
            continue;
        }
        const bool skinned = sorted.firstSkinnedVertex != kNotSkinned;
        const float sides = material.doubleSided ? 2.0f : 1.0f;
        powers.push_back(source->power * (skinned ? 1.0f : AreaScale(draw.world)) * std::numbers::pi_v<float> * sides *
                         draw.emissiveScale);
        // Its distance to the camera from its bounding sphere.
        const glm::vec4& bounds = m_meshes.Get(sorted.mesh).bounds;
        const float scale = std::max({glm::length(glm::vec3(draw.world[0])), glm::length(glm::vec3(draw.world[1])),
                                      glm::length(glm::vec3(draw.world[2]))});
        const glm::vec3 center(draw.world * glm::vec4(glm::vec3(bounds), 1.0f));
        fogPowers.push_back(
            FogSourceWeight(powers.back(), std::max(glm::distance(center, cameraPosition) - bounds.w * scale, 0.0f)));
        // A lamp whose glow changes moves for the histories, as a flickering light does.
        moved.push_back(skinned || draw.emissiveScale != 1.0f ||
                        (draw.previousWorld.has_value() && *draw.previousWorld != draw.world));
        ranges.push_back({EmissiveDrawKey(draw.id, sorted.draw, sorted.mesh, sorted.material),
                          lightCount + triangleCount, source->triangleCount});
        draws.push_back({triangleCount, source->triangleCount, m_device->StorageIndex(source->aliasTable), 0});
        m_emissiveJobs.push_back({index, triangleCount, source->triangleCount, m_device->StorageIndex(source->emission),
                                  material.doubleSided ? 1U : 0U});
        triangleCount += source->triangleCount;
    }

    DAI_TRY(Reserve(*m_device, slot.sources, slot.sourceCapacity, powers.size(), kInitialLightCapacity,
                    sizeof(AliasEntry), "Light sources"));
    DAI_TRY(Reserve(*m_device, slot.fogSources, slot.fogSourceCapacity, fogPowers.size(), kInitialLightCapacity,
                    sizeof(AliasEntry), "Fog light sources"));
    DAI_TRY(Reserve(*m_device, slot.emissiveDraws, slot.emissiveDrawCapacity, draws.size(), 16,
                    sizeof(EmissiveDrawData), "Emissive draws"));
    DAI_TRY(Reserve(*m_device, slot.emissiveTriangles, slot.emissiveTriangleCapacity, triangleCount, 256,
                    kEmissiveTriangleSize, "Emissive triangles", rhi::MemoryLocation::GpuOnly));
    // The last frame's lights, each mapped to this one's.
    // Then, for each of this frame's lights, 1 if it is new.
    DAI_TRY(Reserve(*m_device, slot.lightMap, slot.lightMapCapacity, m_lightCount + lightCount + triangleCount,
                    kInitialLightCapacity, sizeof(std::uint32_t), "Light map"));
    const std::vector<AliasEntry> table = BuildAliasTable(powers);
    if (!table.empty()) {
        std::memcpy(m_device->Mapped(slot.sources).data(), table.data(), table.size() * sizeof(AliasEntry));
    }
    const std::vector<AliasEntry> fogTable = BuildAliasTable(fogPowers);
    if (!fogTable.empty()) {
        std::memcpy(m_device->Mapped(slot.fogSources).data(), fogTable.data(), fogTable.size() * sizeof(AliasEntry));
    }
    if (!draws.empty()) {
        std::memcpy(m_device->Mapped(slot.emissiveDraws).data(), draws.data(), draws.size() * sizeof(EmissiveDrawData));
    }

    // Each light keeps its name from frame to frame: the reservoirs that chose it follow it.
    std::vector<std::uint32_t> map = MapLights(m_previousRanges, ranges, m_lightCount);
    std::vector<std::uint32_t> fresh(std::size_t{lightCount} + triangleCount, kLightNew);
    for (const std::uint32_t current : map) {
        if (current != kGone) {
            fresh[current] = 0;
        }
    }
    // A source's lights share its flag: an emissive draw's triangles move with it.
    for (std::size_t source = 0; source < ranges.size(); ++source) {
        if (moved[source]) {
            for (std::uint32_t light = 0; light < ranges[source].count; ++light) {
                fresh[ranges[source].first + light] |= kLightMoved;
            }
        }
    }
    map.insert(map.end(), fresh.begin(), fresh.end());
    if (!map.empty()) {
        std::memcpy(m_device->Mapped(slot.lightMap).data(), map.data(), map.size() * sizeof(std::uint32_t));
    }
    m_previousRanges = std::move(ranges);
    m_mappedLightCount = m_lightCount;
    m_lightCount = lightCount + triangleCount;
    return FrameLights{.sourceCount = static_cast<std::uint32_t>(powers.size()),
                       .emissiveTriangleCount = triangleCount};
}

core::Result<void> MeshPass::Prepare(const SceneView& view, const FrameTargets& targets) {
    DAI_PROFILE_SCOPE("renderer::MeshPass::Prepare");
    m_prepared = nullptr;
    m_sorted.clear();
    m_skinningJobs.clear();
    m_traced.clear();
    if (!view.camera.has_value()) {
        return {};
    }
    const CameraView& camera = *view.camera;
    // The aspect is the output's: a render size rounded to whole pixels does not change the view.
    const rhi::Extent targetExtent = targets.targetExtent.width > 0 ? targets.targetExtent : targets.extent;
    const float aspect = static_cast<float>(targetExtent.width) / static_cast<float>(std::max(targetExtent.height, 1U));
    // The renderer's field of view is across the view, so that wider windows see more to the sides.
    const float verticalFov =
        camera.verticalFov.value_or(2.0f * std::atan(std::tan(m_fieldOfView * 0.5f) / std::max(aspect, 0.01f)));
    const float farPlane = std::max(m_viewDistance, camera.nearPlane * 2.0f);

    FrameSlot& slot = m_frameSlots[m_nextFrameSlot];
    m_nextFrameSlot = (m_nextFrameSlot + 1) % rhi::kFramesInFlight;
    DAI_TRY(PrepareFrameSlot(slot, view));
    ++m_prepareCount;

    const glm::vec3 cameraPosition = glm::inverse(camera.view)[3];
    const glm::mat4 projection = math::PerspectiveReverseZ(verticalFov, aspect, camera.nearPlane, farPlane);
    m_perspective = {.verticalFov = verticalFov, .aspect = aspect, .nearPlane = camera.nearPlane, .farPlane = farPlane};
    const glm::mat4 viewProjection = projection * camera.view;
    // The jitter moves the image in clip space, where y goes up and the screen is 2 wide.
    const glm::vec2 extent(static_cast<float>(std::max(targets.extent.width, 1U)),
                           static_cast<float>(std::max(targets.extent.height, 1U)));
    const glm::vec2 jitter = targets.jitter / extent;
    const glm::mat4 jittered =
        glm::translate(glm::mat4(1.0f), glm::vec3(-2.0f * jitter.x, 2.0f * jitter.y, 0.0f)) * viewProjection;
    // The first frame, and the first after a cut, did not move.
    const glm::mat4 previousViewProjection =
        m_previousViewProjection.has_value() && !view.cut ? *m_previousViewProjection : viewProjection;
    m_previousViewProjection = viewProjection;
    m_viewProjection = viewProjection;
    // The first frame, and the first after a cut, have no history to read: their own size stands in.
    const rhi::Extent previousExtent =
        m_previousRenderExtent.width > 0 && !view.cut ? m_previousRenderExtent : targets.extent;
    m_previousRenderExtent = targets.extent;
    m_renderExtent = targets.extent;
    FrameData frame{jittered,
                    glm::inverse(jittered),
                    cameraPosition,
                    std::max(view.atmosphere.fog.density, 0.0f),
                    glm::clamp(view.atmosphere.fog.albedo, glm::vec3(0.0f), glm::vec3(1.0f)),
                    static_cast<std::uint32_t>(view.lights.size()),
                    m_device->StorageIndex(slot.lights),
                    view.atmosphere.sky ? m_device->BindlessIndex(view.atmosphere.sky) : kNoTexture,
                    view.atmosphere.skyIntensity * targets.exposure,
                    targets.frameIndex,
                    m_normalStrength,
                    targets.indirect ? m_device->BindlessIndex(targets.indirect) : kNoTexture,
                    glm::vec2(1.0f / static_cast<float>(std::max(targets.extent.width, 1U)),
                              1.0f / static_cast<float>(std::max(targets.extent.height, 1U))),
                    camera.nearPlane,
                    static_cast<std::uint32_t>(view.lights.size()),
                    0, // The alias table: below, once the lights are known.
                    farPlane,
                    glm::normalize(-glm::vec3(glm::inverse(camera.view)[2])),
                    0,
                    targets.accelerationStructure,
                    view.atmosphere.skyYaw,
                    jitter,
                    viewProjection,
                    previousViewProjection,
                    targets.reflections ? m_device->BindlessIndex(targets.reflections) : kNoTexture,
                    targets.background ? m_device->BindlessIndex(targets.background) : kNoTexture,
                    targets.depth ? m_device->BindlessIndex(targets.depth) : kNoTexture,
                    targets.exposure,
                    0,
                    0,
                    targets.sharpReflections,
                    targets.mipBias,
                    {targets.extent.width, targets.extent.height},
                    {previousExtent.width, previousExtent.height},
                    {targetExtent.width, targetExtent.height},
                    std::clamp(view.atmosphere.fog.anisotropy, -0.95f, 0.95f),
                    targets.time,
                    m_device->StorageIndex(slot.fogVolumes),
                    static_cast<std::uint32_t>(view.fogVolumes.size()),
                    targets.fog ? m_device->BindlessIndex(targets.fog) : kNoTexture,
                    0};
    if (!view.fogVolumes.empty()) {
        auto* volumes = reinterpret_cast<FogVolumeData*>(m_device->Mapped(slot.fogVolumes).data());
        for (std::size_t index = 0; index < view.fogVolumes.size(); ++index) {
            const FogVolume& volume = view.fogVolumes[index];
            // The box's height in meters: its Y axis, scaled.
            const float height = glm::length(glm::vec3(volume.world[1]));
            volumes[index] = {.worldToBox = glm::inverse(volume.world),
                              .albedo = glm::clamp(volume.albedo, glm::vec3(0.0f), glm::vec3(1.0f)),
                              .density = std::max(volume.density, 0.0f),
                              .noiseVelocity = volume.noiseVelocity,
                              .noiseFrequency = volume.noiseFrequency,
                              .noise = std::clamp(volume.noise, 0.0f, 1.0f),
                              .edge = std::clamp(volume.edge, 0.0f, 1.0f),
                              .heightFalloff = volume.falloffHeight > 0.0f ? height / volume.falloffHeight : 0.0f,
                              .padding = 0.0f,
                              .glow = glm::max(volume.glow, glm::vec3(0.0f)),
                              .padding2 = 0.0f};
        }
    }

    // The decals nearest to the camera, within the view distance (ADR-0060), and their boxes in world space.
    std::vector<std::size_t> nearDecals;
    for (std::size_t index = 0; index < view.decals.size(); ++index) {
        const Decal& decal = view.decals[index];
        if (!decal.albedo.IsNull() && decal.opacity > 0.0f &&
            glm::distance(glm::vec3(decal.world[3]), cameraPosition) <= farPlane) {
            nearDecals.push_back(index);
        }
    }
    if (nearDecals.size() > kMaxDecals) {
        std::ranges::partial_sort(nearDecals, nearDecals.begin() + kMaxDecals, {}, [&](std::size_t index) {
            return glm::distance(glm::vec3(view.decals[index].world[3]), cameraPosition);
        });
        nearDecals.resize(kMaxDecals);
    }
    std::vector<DecalBox> decalBoxes;
    auto* decals = reinterpret_cast<DecalData*>(m_device->Mapped(slot.decals).data());
    for (std::size_t slotIndex = 0; slotIndex < nearDecals.size(); ++slotIndex) {
        const Decal& decal = view.decals[nearDecals[slotIndex]];
        DecalBox box{glm::vec3(1e30f), glm::vec3(-1e30f)};
        for (int corner = 0; corner < 8; ++corner) {
            const glm::vec3 local((corner & 1) != 0 ? 0.5f : -0.5f, (corner & 2) != 0 ? 0.5f : -0.5f,
                                  (corner & 4) != 0 ? 0.5f : -0.5f);
            const glm::vec3 point(decal.world * glm::vec4(local, 1.0f));
            box.min = glm::min(box.min, point);
            box.max = glm::max(box.max, point);
        }
        decalBoxes.push_back(box);
        const rhi::Extent size = m_device->TextureExtent(decal.albedo);
        const float width = glm::length(glm::vec3(decal.world[0]));
        decals[slotIndex] = {.worldToBox = glm::inverse(decal.world),
                             .forward = glm::normalize(-glm::vec3(decal.world[2])),
                             .opacity = std::clamp(decal.opacity, 0.0f, 1.0f),
                             .color = decal.color,
                             .roughness = std::clamp(decal.roughness, 0.045f, 1.0f),
                             .albedo = m_device->BindlessIndex(decal.albedo),
                             .normal = decal.normal.IsNull() ? kNoTexture : m_device->BindlessIndex(decal.normal),
                             .metallic = std::clamp(decal.metallic, 0.0f, 1.0f),
                             .angleFade = std::clamp(decal.angleFade, -1.0f, 0.99f),
                             .right = glm::normalize(glm::vec3(decal.world[0])),
                             .texelSize = width / static_cast<float>(std::max(size.width, 1U)),
                             .up = glm::normalize(glm::vec3(decal.world[1])),
                             .padding = 0};
    }
    frame.decalBuffer = m_device->StorageIndex(slot.decals);
    frame.decalCount = static_cast<std::uint32_t>(nearDecals.size());
    frame.debugFlags = targets.showDecals ? 1U : 0U;

    if (!view.jointMatrices.empty()) {
        // Without previous joints, they stood still.
        const bool moved = view.previousJointMatrices.size() == view.jointMatrices.size();
        const std::size_t bytes = view.jointMatrices.size() * sizeof(glm::mat4);
        std::byte* joints = m_device->Mapped(slot.joints).data();
        std::memcpy(joints, view.jointMatrices.data(), bytes);
        std::memcpy(joints + bytes, moved ? view.previousJointMatrices.data() : view.jointMatrices.data(), bytes);
    }
    m_jointCount = static_cast<std::uint32_t>(view.jointMatrices.size());

    // Sorted, so that the pipeline and the buffers change as rarely as possible. Instance i is the i-th
    // sorted draw, for the G-buffer, glass and rays alike.
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
        // A draw the camera does not see (ADR-0059) is left out of the G-buffer and the glass pass.
        const bool visible =
            draw.seenByCamera &&
            (draw.firstJoint != kNoJoints || glm::distance(center, cameraPosition) - bounds.w * scale <= farPlane);
        const bool glass = m_materials.Get(material).glass;
        // Glass casts no shadow: light goes through it (ADR-0042).
        m_sorted.push_back({(doubleSided << 32U) | draw.mesh.Index(), draw.mesh, material, index,
                            draw.castsShadows && !glass, visible, kNotSkinned, glass});
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
        DAI_TRY_ASSIGN(rhi::BufferHandle previous, m_device->CreateBuffer({.size = capacity * sizeof(glm::vec3),
                                                                           .usage = rhi::BufferUsage::Storage,
                                                                           .name = "Previous skinned positions"}));
        m_device->DestroyBuffer(m_skinned);
        m_device->DestroyBuffer(m_previousSkinned);
        m_skinned = created;
        m_previousSkinned = previous;
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
        // Skinned vertices are in world space already, and so are their previous positions.
        const glm::mat4 world = skinned ? glm::mat4(1.0f) : draw.world;
        instances[index] = {.world = world,
                            .vertexBuffer = m_device->StorageIndex(skinned ? m_skinned : mesh.vertices),
                            .firstVertex = skinned ? sorted.firstSkinnedVertex : 0,
                            .indexBuffer = m_device->StorageIndex(mesh.indices),
                            .material = sorted.material.Index(),
                            .previousWorld = skinned ? world : draw.previousWorld.value_or(world),
                            .previousPositions = skinned ? m_device->StorageIndex(m_previousSkinned) : kNoTexture,
                            .emissiveScale = std::max(draw.emissiveScale, 0.0f),
                            .decalMask = DecalMaskOf(m_meshes.Get(draw.mesh).bounds, draw.world, decalBoxes)};
        TracedDraw& traced = m_traced.emplace_back();
        traced.world = world;
        traced.opaque = !material.alphaTested;
        traced.doubleSided = material.doubleSided;
        traced.castsShadows = sorted.castsShadows;
        traced.glass = sorted.glass;
        traced.mask = sorted.glass
                          ? kMaskGlass
                          : static_cast<std::uint8_t>((sorted.castsShadows ? kMaskShadow | kMaskCollision : 0) |
                                                      (draw.seenByCamera ? kMaskCamera : 0) |
                                                      (draw.seenInReflections ? kMaskReflection : 0) |
                                                      (draw.seenByIndirect ? kMaskIndirect : 0));
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
    // The lights, emissive draws included, once the draws are sorted: their instances are known.
    DAI_TRY_ASSIGN(const FrameLights frameLights, PrepareLights(slot, view, targets.exposure, cameraPosition));
    frame.sourceCount = frameLights.sourceCount;
    frame.aliasBuffer = m_device->StorageIndex(slot.sources);
    frame.fogAliasBuffer = m_device->StorageIndex(slot.fogSources);
    frame.emissiveDrawBuffer = m_device->StorageIndex(slot.emissiveDraws);
    frame.emissiveTriangleBuffer = m_device->StorageIndex(slot.emissiveTriangles);
    frame.emissiveTriangleCount = frameLights.emissiveTriangleCount;
    std::memcpy(m_device->Mapped(slot.frame).data(), &frame, sizeof(frame));
    m_counts = {.draws = static_cast<std::uint32_t>(view.draws.size()),
                .lights = static_cast<std::uint32_t>(view.lights.size()) + frameLights.emissiveTriangleCount};
    m_hasGlass = false;
    for (const SortedDraw& sorted : m_sorted) {
        m_hasGlass = m_hasGlass || (sorted.visible && sorted.glass);
        if (sorted.visible) {
            ++m_counts.drawnDraws;
            m_counts.triangles += m_meshes.Get(sorted.mesh).indexCount / 3;
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
    PassDesc desc{.name = "Skinning",
                  .buffers = {{m_skinned, rhi::Access::ComputeShaderWrite},
                              {m_previousSkinned, rhi::Access::ComputeShaderWrite}}};
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
        const std::uint32_t previousTarget = m_device->StorageIndex(m_previousSkinned);
        // The draws write separate vertices: no dispatch waits for another.
        for (const SkinningJob& job : m_skinningJobs) {
            commands.Push(SkinningPushConstants{job.source, job.skin, joints, job.firstJoint, target, job.firstVertex,
                                                job.vertexCount, m_jointCount, previousTarget});
            commands.Dispatch((job.vertexCount + kSkinningGroupSize - 1) / kSkinningGroupSize);
        }
    });
}

void MeshPass::AddEmissiveLights(RenderGraph& graph) {
    if (m_prepared == nullptr) {
        return;
    }
    // The emission of each triangle of the meshes and materials drawn together for the first time, written
    // where the CPU reads it a few frames later.
    if (!m_averageJobs.empty()) {
        PassDesc desc{.name = "Emission of triangles", .group = "Emissive lights"};
        std::vector<EmissiveAveragePushConstants> pushes;
        for (const AverageJob& job : m_averageJobs) {
            const auto found = m_emissiveSources.find(job.key);
            const Mesh* drawn = m_meshes.TryGet(job.mesh);
            if (found == m_emissiveSources.end() || drawn == nullptr) {
                continue;
            }
            EmissiveSource& source = found->second;
            desc.buffers.push_back({source.readback, rhi::Access::ComputeShaderWrite});
            desc.buffers.push_back({drawn->vertices, rhi::Access::ComputeShaderRead});
            desc.buffers.push_back({drawn->indices, rhi::Access::ComputeShaderRead});
            pushes.push_back({m_device->StorageIndex(drawn->vertices), m_device->StorageIndex(drawn->indices),
                              m_device->StorageIndex(m_materialTable), job.material.Index(),
                              m_device->StorageIndex(source.readback), source.triangleCount});
            source.averagedAt = m_prepareCount;
        }
        m_averageJobs.clear();
        if (!pushes.empty()) {
            graph.AddPass(std::move(desc), [this, pushes = std::move(pushes)](rhi::CommandList& commands) {
                DAI_PROFILE_SCOPE("renderer::MeshPass::EmissionOfTriangles");
                commands.BindPipeline(m_emissiveAverage);
                for (const EmissiveAveragePushConstants& push : pushes) {
                    commands.Push(push);
                    commands.Dispatch((push.triangleCount + kEmissiveGroupSize - 1) / kEmissiveGroupSize);
                }
            });
        }
    }
    // The frame's emissive triangles, in world space: emissive draws that move stay lights.
    if (m_emissiveJobs.empty()) {
        return;
    }
    PassDesc desc{.name = "Emissive triangles",
                  .group = "Emissive lights",
                  .buffers = {{m_prepared->emissiveTriangles, rhi::Access::ComputeShaderWrite},
                              {SkinnedVertices(), rhi::Access::ComputeShaderRead}}};
    std::vector<EmissiveTrianglesPushConstants> pushes;
    const std::uint32_t instances = m_device->StorageIndex(m_prepared->instances);
    const std::uint32_t output = m_device->StorageIndex(m_prepared->emissiveTriangles);
    for (const EmissiveJob& job : m_emissiveJobs) {
        pushes.push_back(
            {instances, job.instance, job.firstTriangle, job.triangleCount, job.emission, output, job.twoSided, 0});
        const Mesh& mesh = m_meshes.Get(m_sorted[job.instance].mesh);
        desc.buffers.push_back({mesh.vertices, rhi::Access::ComputeShaderRead});
        desc.buffers.push_back({mesh.indices, rhi::Access::ComputeShaderRead});
    }
    graph.AddPass(std::move(desc), [this, pushes = std::move(pushes)](rhi::CommandList& commands) {
        DAI_PROFILE_SCOPE("renderer::MeshPass::EmissiveTriangles");
        commands.BindPipeline(m_emissiveTriangles);
        // The draws write separate triangles: no dispatch waits for another.
        for (const EmissiveTrianglesPushConstants& push : pushes) {
            commands.Push(push);
            commands.Dispatch((push.triangleCount + kEmissiveGroupSize - 1) / kEmissiveGroupSize);
        }
    });
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

void MeshPass::RecordGBuffer(rhi::CommandList& commands) {
    DAI_PROFILE_SCOPE("renderer::MeshPass::RecordGBuffer");
    RecordDraws(commands, m_pipelines.gbuffer, m_pipelines.gbufferDoubleSided, false);
}

void MeshPass::RecordGlass(rhi::CommandList& commands) {
    DAI_PROFILE_SCOPE("renderer::MeshPass::RecordGlass");
    RecordDraws(commands, m_pipelines.glass, m_pipelines.glassDoubleSided, true);
}

core::Result<ShadingTargets> MeshPass::CreateShadingTargets(rhi::Extent extent) const {
    ShadingTargets targets;
    const auto create = [&](rhi::TextureHandle& texture, rhi::Format format, rhi::TextureUsage written,
                            const char* name) -> core::Result<void> {
        DAI_TRY_ASSIGN(texture, m_device->CreateTexture(
                                    {.extent = extent,
                                     .format = format,
                                     .usage = written | rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferSrc,
                                     .name = name}));
        return {};
    };
    using enum rhi::TextureUsage;
    core::Result<void> created =
        create(targets.baseColor, kBaseColorFormat, ColorTarget, "Base color")
            .and_then([&] { return create(targets.emission, kEmissionFormat, ColorTarget, "Emission"); })
            .and_then([&] { return create(targets.layers, kLayersFormat, ColorTarget, "Layers"); })
            .and_then([&] { return create(targets.diffuse, kLightFormat, Storage, "Diffuse light"); })
            .and_then([&] { return create(targets.specular, kLightFormat, Storage, "Specular light"); });
    if (!created) {
        DestroyShadingTargets(targets);
        return tl::unexpected(std::move(created.error()));
    }
    return targets;
}

void MeshPass::DestroyShadingTargets(ShadingTargets& targets) const {
    m_device->DestroyTexture(targets.baseColor);
    m_device->DestroyTexture(targets.emission);
    m_device->DestroyTexture(targets.layers);
    m_device->DestroyTexture(targets.diffuse);
    m_device->DestroyTexture(targets.specular);
    targets = {};
}

rhi::RenderingDesc MeshPass::GBufferRendering(const ShadingTargets& targets, rhi::TextureHandle normals,
                                              rhi::TextureHandle motion, rhi::TextureHandle depth,
                                              std::optional<rhi::Extent> area) {
    // Where nothing is drawn, nothing moved: the sky's motion comes from the camera alone.
    return {.colorTarget = normals,
            .clearColor = glm::vec4(0.5f, 1.0f, 0.5f, 1.0f),
            .moreColorTargets = {{motion, glm::vec4(0.0f)},
                                 {targets.baseColor, glm::vec4(0.0f)},
                                 {targets.emission, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)},
                                 {targets.layers, glm::vec4(0.0f)}},
            .depthTarget = depth,
            .clearDepth = 0.0f,
            .area = area};
}

void MeshPass::AddComposite(RenderGraph& graph, rhi::TextureHandle target, const CompositeInputs& inputs) {
    DAI_ASSERT(inputs.shading != nullptr, "The composite reads the shading targets");
    if (m_prepared == nullptr) {
        return;
    }
    const ShadingTargets& shading = *inputs.shading;
    const auto index = [this](rhi::TextureHandle texture) { return m_device->BindlessIndex(texture); };
    const CompositePushConstants push{m_device->StorageIndex(m_prepared->frame),
                                      index(shading.baseColor),
                                      index(shading.emission),
                                      index(inputs.diffuse),
                                      index(inputs.specular),
                                      index(shading.specular),
                                      index(inputs.depth),
                                      inputs.fogOnly ? kFogOnly : 0U};
    graph.AddPass({.name = "Composite",
                   .rendering = rhi::RenderingDesc{.colorTarget = target, .area = m_renderExtent},
                   .textures = {{target, rhi::Access::ColorTarget, true},
                                {shading.baseColor, rhi::Access::FragmentShaderRead},
                                {shading.emission, rhi::Access::FragmentShaderRead},
                                {shading.specular, rhi::Access::FragmentShaderRead},
                                {inputs.diffuse, rhi::Access::FragmentShaderRead},
                                {inputs.specular, rhi::Access::FragmentShaderRead},
                                {inputs.depth, rhi::Access::FragmentShaderRead},
                                {inputs.fog, rhi::Access::FragmentShaderRead}}},
                  [this, push](rhi::CommandList& commands) {
                      commands.BindPipeline(m_pipelines.composite);
                      commands.Push(push);
                      commands.Draw(3);
                  });
}

std::uint32_t MeshPass::FrameDataIndex() const {
    DAI_ASSERT(m_prepared != nullptr, "No frame is prepared");
    return m_device->StorageIndex(m_prepared->frame);
}

std::uint32_t MeshPass::LightMapIndex() const {
    DAI_ASSERT(m_prepared != nullptr, "No frame is prepared");
    return m_device->StorageIndex(m_prepared->lightMap);
}

void MeshPass::RecordDraws(rhi::CommandList& commands, rhi::PipelineHandle oneSided, rhi::PipelineHandle doubleSided,
                           bool glass) {
    if (m_prepared == nullptr) {
        return;
    }
    MeshPushConstants push{
        .frameBuffer = m_device->StorageIndex(m_prepared->frame),
        .instanceBuffer = m_device->StorageIndex(m_prepared->instances),
        .materialBuffer = m_device->StorageIndex(m_materialTable),
        .instanceIndex = 0,
        .materialIndex = 0,
    };
    rhi::PipelineHandle boundPipeline;
    rhi::BufferHandle boundVertices;
    std::uint64_t boundOffset = 0;
    MeshHandle boundIndices;
    for (std::uint32_t index = 0; index < m_sorted.size(); ++index) {
        const SortedDraw& draw = m_sorted[index];
        if (!draw.visible || draw.glass != glass) {
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
