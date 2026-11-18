// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/core/SlotPool.hpp>
#include <daidalos/renderer/Material.hpp>
#include <daidalos/renderer/SceneView.hpp>
#include <daidalos/resources/ModelData.hpp>
#include <daidalos/rhi/Device.hpp>

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "LightSampling.hpp"

namespace daidalos::renderer {

class RenderGraph;

/// The targets of a frame that MeshPass needs to know about.
struct FrameTargets {
    /// The pixels the frame draws, in the top-left corner of its targets (ADR-0049).
    rhi::Extent extent;
    /// The whole size of the targets, the output's: the projection's aspect follows it. extent's when zero.
    rhi::Extent targetExtent;
    /// Added to the level of the textures the G-buffer samples: log2 of the render scale, so that textures
    /// keep the output's detail (ADR-0049).
    float mipBias = 0.0f;
    /// The indirect diffuse light of the frame, per unit of diffuse color (ADR-0040), which the lighting pass
    /// adds to its lights' light. Null for none: direct light only.
    rhi::TextureHandle indirect;
    /// Index of the top-level acceleration structure the frame traces, for FrameData.
    std::uint32_t accelerationStructure = 0;
    /// Where the frame samples each pixel, relative to its center, in pixels, y down: the jitter of
    /// temporal antialiasing (ADR-0037). Motion vectors leave it out.
    glm::vec2 jitter{0.0f};
    /// Counts the frames: the noise of soft shadows changes with it (ADR-0039).
    std::uint32_t frameIndex = 0;
    /// The light that each pixel's specular lobe reflects (GlobalIllumination::Reflections), which the
    /// lighting pass multiplies by the specular albedo (ADR-0042). Null: the indirect light stands in for it.
    rhi::TextureHandle reflections;
    /// What the glass pass reads: a copy of the frame before glass, and the G-buffer's depth (ADR-0042).
    rhi::TextureHandle background;
    rhi::TextureHandle depth;
    /// What luminance in nits is multiplied by before tonemapping (ADR-0043): lights, emission and the sky
    /// reach the GPU multiplied by it, so that its targets hold values near those of the screen.
    float exposure = 1.0f;
    /// The fog in front of each point of the view (VolumetricFog::Output), which the composite, glass and
    /// particles read (ADR-0051). Null for no fog.
    rhi::TextureHandle fog;
    /// Seconds, by which the noise of fog volumes moves.
    float time = 0.0f;
    /// Smoother surfaces trace sharp reflections, which show emissive lights (ADR-0044): with reflections,
    /// the smaller of GlobalIllumination::kSharpRoughness and the roughness above which no reflection is
    /// traced; 0 without.
    float sharpReflections = 0.0f;
    /// Tints of the lit scene: what decals cover (1, ADR-0060), how wet surfaces are (2, ADR-0061).
    std::uint32_t debugFlags = 0;
    /// The storage indices of the particles rays meet (ADR-0062, ParticlePass::TracedParticles); unused when
    /// the frame's structure holds none.
    std::uint32_t particleData = 0;
    std::uint32_t particleEffects = 0;
    std::uint32_t particleCurves = 0;
};

/// What the G-buffer holds of the material, and what the lighting pass writes (ADR-0039, ADR-0044), which
/// the composite pass puts back together. The G-buffer's normals and motion vectors are the renderer's.
struct ShadingTargets {
    /// The base color, and the metalness in alpha (MeshPass::kBaseColorFormat).
    rhi::TextureHandle baseColor;
    /// The emission in nits times the exposure, and the occlusion in alpha (MeshPass::kEmissionFormat).
    rhi::TextureHandle emission;
    /// The clear coat and the sheen, packed (MeshPass::kLayersFormat, ADR-0056).
    rhi::TextureHandle layers;
    /// Storage images: the diffuse light, direct and indirect, per unit of diffuse color, and the specular
    /// light per unit of specular albedo, with the albedo in alpha (MeshPass::kLightFormat).
    rhi::TextureHandle diffuse;
    rhi::TextureHandle specular;
};

/// What the composite pass reads.
struct CompositeInputs {
    const ShadingTargets* shading = nullptr;
    /// The diffuse and specular light to add: the denoiser's, or the shading targets' own.
    rhi::TextureHandle diffuse;
    rhi::TextureHandle specular;
    rhi::TextureHandle depth;
    /// FrameTargets::fog, which the composite reads, or null.
    rhi::TextureHandle fog;
    /// The fog alone, over black: its debug view.
    bool fogOnly = false;
};

/// The perspective a frame is drawn with, reverse-Z, as the upscalers that reconstruct depth need it.
struct Perspective {
    /// Radians, from the bottom of the view to its top.
    float verticalFov = 0.0f;
    /// Width over height, the output's.
    float aspect = 1.0f;
    float nearPlane = 0.0f;
    float farPlane = 0.0f;
};

/// A draw as rays see it (ADR-0033). Rigid draws use their mesh's bottom-level structure; skinned draws
/// have a structure refitted each frame to their skinned vertices.
struct TracedDraw {
    /// Null for a skinned draw.
    rhi::AccelerationStructureHandle structure;
    /// For a skinned draw: its skinned vertices and its mesh's indices.
    rhi::TriangleGeometry skinned;
    glm::mat4 world{1.0f};
    /// Its material has no alpha test.
    bool opaque = true;
    bool doubleSided = false;
    bool castsShadows = true;
    /// Its material is glass (ADR-0042): rays other than the glass pass's go through it.
    bool glass = false;
    /// Which rays see it (RayTracingScene.hpp's masks, ADR-0059).
    std::uint8_t mask = 0;
};

/// Owns the meshes, the materials, the frame's lights and the pipelines that draw meshes: the G-buffer, glass
/// and the composite. It works on any target, so that tests can run it without a window. The Device must
/// outlive it.
class MeshPass {
public:
    static core::Result<MeshPass> Create(rhi::Device& device, rhi::Format colorFormat, rhi::Format depthFormat);

    MeshPass(MeshPass&& other) noexcept;
    MeshPass& operator=(MeshPass&& other) = delete;
    MeshPass(const MeshPass&) = delete;
    MeshPass& operator=(const MeshPass&) = delete;
    ~MeshPass();

    [[nodiscard]] core::Result<MeshHandle> CreateMesh(const resources::MeshData& data, const std::string& name);
    void DestroyMesh(MeshHandle mesh);
    /// What drawing a mesh by itself needs, as mesh particles do (ADR-0053), or nothing if it is gone.
    struct Geometry {
        rhi::BufferHandle vertices;
        rhi::BufferHandle indices;
        std::uint32_t indexCount = 0;
        /// The lowest of its vertices along its Y.
        float lowest = 0.0f;
    };
    [[nodiscard]] std::optional<Geometry> GeometryOf(MeshHandle mesh) const;
    /// A material's index in the material table and its sides, or the default material's if it is gone.
    struct MaterialSlot {
        std::uint32_t index = 0;
        bool doubleSided = false;
    };
    [[nodiscard]] MaterialSlot MaterialOf(MaterialHandle material) const;

    [[nodiscard]] core::Result<MaterialHandle> CreateMaterial(const MaterialDesc& desc);
    /// Changes a material in place (ADR-0079); false if it is gone or is the default one.
    bool UpdateMaterial(MaterialHandle material, const MaterialDesc& desc);
    void DestroyMaterial(MaterialHandle material);

    /// The G-buffer's first target: world normals, times 0.5 plus 0.5, and the roughness. Half floats: in 8
    /// bits, shading from the G-buffer's normals differs visibly from the path tracer's (ADR-0044).
    static constexpr rhi::Format kNormalFormat = rhi::Format::Rgba16Float;
    /// Its second: where each pixel's surface was in the previous frame minus where it is, in texture
    /// coordinates (ADR-0037).
    static constexpr rhi::Format kMotionFormat = rhi::Format::Rg16Float;
    /// Its third: sRGB applies to the color, for precision in the dark ones; the metalness stays linear.
    static constexpr rhi::Format kBaseColorFormat = rhi::Format::Rgba8Srgb;
    /// Its fourth.
    static constexpr rhi::Format kEmissionFormat = rhi::Format::Rgba16Float;
    /// The clear coat and the sheen of each pixel, packed (GBufferSurface.slangh, ADR-0056), and the horizons
    /// of its relief (ADR-0064).
    static constexpr rhi::Format kLayersFormat = rhi::Format::Rgba32Uint;
    /// The lighting pass's diffuse and specular light, for the denoiser (ADR-0039).
    static constexpr rhi::Format kLightFormat = rhi::Format::Rgba16Float;

    /// The G-buffer's material targets and the lighting pass's, at this size.
    [[nodiscard]] core::Result<ShadingTargets> CreateShadingTargets(rhi::Extent extent) const;
    void DestroyShadingTargets(ShadingTargets& targets) const;
    /// The rendering of RecordGBuffer: normals (kNormalFormat), motion vectors (kMotionFormat), the material
    /// targets and the depth, all cleared, in the top-left corner of this size.
    [[nodiscard]] static rhi::RenderingDesc GBufferRendering(const ShadingTargets& targets, rhi::TextureHandle normals,
                                                             rhi::TextureHandle motion, rhi::TextureHandle depth,
                                                             std::optional<rhi::Extent> area = std::nullopt);

    /// Replaces the shaders of the G-buffer. The glass pass uses the same vertex shader, so that both
    /// compute the same depth. On failure the current pipelines stay in use.
    [[nodiscard]] core::Result<void> ReplaceShaders(std::span<const std::uint32_t> vertexShader,
                                                    std::span<const std::uint32_t> fragmentShader);

    /// A frame takes four steps (ADR-0024). Prepare fills the frame's buffers from the view, outside any
    /// rendering scope. Draws whose mesh has been destroyed are skipped, and draws whose material has been
    /// use the default one. Without a camera, or after a failure to allocate, the frame draws nothing.
    [[nodiscard]] core::Result<void> Prepare(const SceneView& view, const FrameTargets& targets);
    /// What the last Prepare chose to draw: FrameStats' draw, triangle and light counts. Lights counts the
    /// analytic lights and the emissive triangles that light.
    struct Counts {
        std::uint32_t draws = 0;
        std::uint32_t drawnDraws = 0;
        std::uint64_t triangles = 0;
        std::uint32_t lights = 0;
    };
    [[nodiscard]] const Counts& LastCounts() const { return m_counts; }
    [[nodiscard]] std::size_t MeshCount() const { return m_meshes.Size(); }
    [[nodiscard]] std::size_t MaterialCount() const { return m_materials.Size(); }
    /// The material at an index of the material table, as rays and particles report it, or null if it is gone.
    [[nodiscard]] MaterialHandle MaterialAt(std::uint32_t index) const { return m_materials.HandleAt(index); }
    /// Adds the pass that skins the prepared frame's skinned draws, if it has any, as "Skinning". It must
    /// come before every pass that draws or traces them.
    void AddSkinning(RenderGraph& graph);
    /// Adds the passes of the prepared frame's emissive lights, as "Emissive lights" (ADR-0044): the average
    /// emission of each triangle of an emissive mesh and material drawn together for the first time, read
    /// back a few frames later, and the frame's emissive triangles in world space. After AddSkinning, before
    /// any pass that samples the frame's lights.
    void AddEmissiveLights(RenderGraph& graph);
    /// The emission of each triangle of a mesh drawn with an emissive material, in nits, averaged over its
    /// texture: empty until it is read back, kFramesInFlight frames after the mesh and material were first
    /// prepared together.
    [[nodiscard]] std::vector<glm::vec3> TriangleEmission(MeshHandle mesh, MaterialHandle material) const;
    /// The prepared frame's emissive triangles, which AddEmissiveLights writes: every pass that samples the
    /// frame's lights reads it, and must say so. Null without a prepared frame.
    [[nodiscard]] rhi::BufferHandle EmissiveTriangles() const {
        return m_prepared != nullptr ? m_prepared->emissiveTriangles : rhi::BufferHandle{};
    }
    /// The skinned vertices of the prepared frame, which the passes that draw meshes read as vertices. Null
    /// when the frame has no skinned draw.
    [[nodiscard]] rhi::BufferHandle SkinnedVertices() const {
        return m_skinningJobs.empty() ? rhi::BufferHandle{} : m_skinned;
    }
    /// Where the skinned vertices were in the previous frame, which the G-buffer reads for motion vectors.
    /// Null when the frame has no skinned draw.
    [[nodiscard]] rhi::BufferHandle PreviousSkinnedPositions() const {
        return m_skinningJobs.empty() ? rhi::BufferHandle{} : m_previousSkinned;
    }
    /// The prepared frame's draws, in the order of its instances: a ray's custom index is an instance.
    [[nodiscard]] std::span<const TracedDraw> TracedDraws() const { return m_traced; }
    /// The storage index of the prepared frame's instances.
    [[nodiscard]] std::uint32_t InstanceDataIndex() const;
    /// The pixels the prepared frame draws, in the top-left corner of its targets.
    [[nodiscard]] rhi::Extent RenderExtent() const { return m_renderExtent; }
    /// The view and projection of the prepared frame, without jitter.
    [[nodiscard]] const glm::mat4& ViewProjection() const { return m_viewProjection; }
    /// The perspective of the prepared frame.
    [[nodiscard]] const Perspective& FramePerspective() const { return m_perspective; }
    /// The storage index of the material table.
    [[nodiscard]] std::uint32_t MaterialDataIndex() const { return m_device->StorageIndex(m_materialTable); }
    /// Writes the G-buffer of the opaque draws, inside a GBufferRendering scope. Skinned draws read
    /// PreviousSkinnedPositions.
    void RecordGBuffer(rhi::CommandList& commands);
    /// Draws the prepared frame's glass over the frame (ADR-0042), inside a rendering scope with a target of the
    /// pass's color format and no depth: it reads the G-buffer's depth and the background of FrameTargets.
    void RecordGlass(rhi::CommandList& commands);
    /// The prepared frame has glass to draw.
    [[nodiscard]] bool HasGlass() const { return m_prepared != nullptr && m_hasGlass; }
    /// Adds the pass that puts the shading targets back together into target, with the fog and the sky, as
    /// "Composite" (ADR-0039). target has the pass's color format; the prepared frame's corner of it is
    /// written whole.
    void AddComposite(RenderGraph& graph, rhi::TextureHandle target, const CompositeInputs& inputs);

    /// The storage index of the prepared frame's FrameData, for the passes that read it.
    [[nodiscard]] std::uint32_t FrameDataIndex() const;
    /// Where each light of the frame prepared before this one is in this one's list (ADR-0044): the storage
    /// index of a uint per light, kGone for a light that is gone, and the number of lights it maps. Then a
    /// uint per light of the prepared frame, of flags: kLightNew for a light the frame before did not have,
    /// kLightMoved for one that moved since, whose light the histories must follow at once.
    [[nodiscard]] std::uint32_t LightMapIndex() const;
    [[nodiscard]] std::uint32_t PreviousLightCount() const { return m_mappedLightCount; }

    /// Multiplies the tilt of every normal map, from the next Prepare.
    void SetNormalStrength(float strength) { m_normalStrength = strength; }
    /// Radians across the view, for cameras without a field of view, from the next Prepare.
    void SetFieldOfView(float radians) { m_fieldOfView = radians; }
    /// Meters to the far plane, from the next Prepare: farther meshes and lights are skipped.
    void SetViewDistance(float meters) { m_viewDistance = meters; }

private:
    struct Mesh {
        rhi::BufferHandle vertices;
        rhi::BufferHandle indices;
        /// Joints and weights of each vertex, read by the skinning pass. Null for a rigid mesh.
        rhi::BufferHandle skin;
        /// Its triangles for rays, built once.
        rhi::AccelerationStructureHandle structure;
        std::uint32_t vertexCount = 0;
        std::uint32_t indexCount = 0;
        /// A sphere around the vertices, in the mesh's space: center and radius.
        glm::vec4 bounds{0.0f};
        /// The lowest vertex's Y.
        float lowest = 0.0f;
    };
    struct Material {
        bool doubleSided = false;
        bool alphaTested = false;
        bool glass = false;
        /// Its emission lights the scene (MaterialDesc::emissiveLight), and it has some.
        bool emissive = false;
    };
    /// What the passes need to know of a material to choose its pipeline and pass.
    [[nodiscard]] static Material Flags(const MaterialDesc& desc);
    /// Writes a material's values into its slot of the table the shaders read.
    void WriteMaterial(std::uint32_t slot, const MaterialDesc& desc);
    /// A mesh drawn with an emissive material, whose triangles are lights (ADR-0044).
    struct EmissiveSource {
        std::uint32_t triangleCount = 0;
        /// Where the averaging pass writes each triangle's emission and area, which the CPU reads once.
        rhi::BufferHandle readback;
        /// The Prepare that recorded the averaging, which the GPU has finished kFramesInFlight Prepares later.
        std::optional<std::uint64_t> averagedAt;
        bool ready = false;
        /// Once ready: each triangle's emission, in nits, which the frame's triangle pass reads, and the
        /// alias table of the triangles by power.
        rhi::BufferHandle emission;
        rhi::BufferHandle aliasTable;
        /// The luminance of the triangles times their area, in the mesh's space.
        float power = 0.0f;
    };
    using EmissiveKey = std::pair<std::uint64_t, std::uint64_t>;
    /// An emissive draw of the prepared frame: its instance, and where its triangles go.
    struct EmissiveJob {
        std::uint32_t instance;
        std::uint32_t firstTriangle;
        std::uint32_t triangleCount;
        std::uint32_t emission;
        std::uint32_t twoSided;
    };
    /// One skinned draw of the frame, for the skinning pass.
    struct SkinningJob {
        std::uint32_t source;
        std::uint32_t skin;
        std::uint32_t firstJoint;
        std::uint32_t firstVertex;
        std::uint32_t vertexCount;
    };
    /// What one frame in flight reads. A frame slot is free again when its turn comes, because there
    /// are never more frames in flight than slots.
    struct FrameSlot {
        rhi::BufferHandle frame;
        rhi::BufferHandle instances;
        std::uint64_t instanceCapacity = 0;
        rhi::BufferHandle joints;
        std::uint64_t jointCapacity = 0;
        rhi::BufferHandle lights;
        std::uint64_t lightCapacity = 0;
        /// The alias table of the light sources, by power, and the volumetric fog's, by power near the camera
        /// (FogSourceWeight).
        rhi::BufferHandle sources;
        std::uint64_t sourceCapacity = 0;
        rhi::BufferHandle fogSources;
        std::uint64_t fogSourceCapacity = 0;
        /// LightMapIndex's buffer.
        rhi::BufferHandle lightMap;
        std::uint64_t lightMapCapacity = 0;
        /// The emissive draws, the sources after the analytic lights, and their triangles in world space,
        /// which the GPU writes.
        rhi::BufferHandle emissiveDraws;
        std::uint64_t emissiveDrawCapacity = 0;
        rhi::BufferHandle emissiveTriangles;
        std::uint64_t emissiveTriangleCapacity = 0;
        /// The boxes of fog (ADR-0051).
        rhi::BufferHandle fogVolumes;
        std::uint64_t fogVolumeCapacity = 0;
        /// The frame's decals (ADR-0060), kMaxDecals at most.
        rhi::BufferHandle decals;
        std::uint64_t decalCapacity = 0;
        /// The frame's wet volumes and ripples (ADR-0061).
        rhi::BufferHandle wetVolumes;
        std::uint64_t wetVolumeCapacity = 0;
        rhi::BufferHandle ripples;
        std::uint64_t rippleCapacity = 0;
    };

    MeshPass(rhi::Device& device, rhi::Format colorFormat, rhi::Format depthFormat);

    /// The pipelines of the G-buffer and of glass, one-sided and double-sided.
    struct Pipelines {
        rhi::PipelineHandle gbuffer;
        rhi::PipelineHandle gbufferDoubleSided;
        rhi::PipelineHandle composite;
        rhi::PipelineHandle glass;
        rhi::PipelineHandle glassDoubleSided;
    };
    [[nodiscard]] core::Result<rhi::PipelineHandle> BuildPipeline(std::span<const std::uint32_t> vertexShader,
                                                                  std::span<const std::uint32_t> fragmentShader,
                                                                  rhi::CullMode cull);
    void DestroyPipelines(Pipelines& pipelines);
    /// Draws the sorted draws of glass, or the others, with these pipelines, one-sided and double-sided.
    void RecordDraws(rhi::CommandList& commands, rhi::PipelineHandle oneSided, rhi::PipelineHandle doubleSided,
                     bool glass);
    [[nodiscard]] core::Result<void> PrepareFrameSlot(FrameSlot& slot, const SceneView& view);
    /// Fills the frame slot's lights and emissive draws, their alias table and the map from the last frame's
    /// lights, and the frame's fields that name them. After the draws are sorted.
    struct FrameLights {
        std::uint32_t sourceCount = 0;
        std::uint32_t emissiveTriangleCount = 0;
    };
    [[nodiscard]] core::Result<FrameLights> PrepareLights(FrameSlot& slot, const SceneView& view, float exposure,
                                                          const glm::vec3& cameraPosition);
    /// The emissive source of a mesh and a material, made on first use, its emission read back if the GPU
    /// has written it. Null if it cannot be made.
    EmissiveSource* FindEmissiveSource(MeshHandle mesh, MaterialHandle material);
    void DestroyEmissiveSource(EmissiveSource& source);
    /// Makes room for a material at this index of the table.
    [[nodiscard]] core::Result<void> ReserveMaterialSlot(std::uint32_t index);
    [[nodiscard]] std::uint32_t TextureIndex(rhi::TextureHandle texture) const;

    rhi::Device* m_device;
    rhi::Format m_colorFormat;
    rhi::Format m_depthFormat;
    Pipelines m_pipelines;
    /// The shaders of the G-buffer, to rebuild its pipelines.
    std::vector<std::uint32_t> m_vertexShader;
    std::vector<std::uint32_t> m_fragmentShader;
    float m_fieldOfView = glm::radians(90.0f);
    float m_viewDistance = 150.0f;
    core::SlotPool<Mesh, MeshTag> m_meshes;
    /// The index of a handle is also its index in the material table of the GPU.
    core::SlotPool<Material, MaterialTag> m_materials;
    rhi::BufferHandle m_materialTable;
    std::uint32_t m_materialCapacity = 0;
    MaterialHandle m_defaultMaterial;
    std::array<FrameSlot, rhi::kFramesInFlight> m_frameSlots;
    std::uint32_t m_nextFrameSlot = 0;
    /// A draw of the view, in the order Record issues it.
    struct SortedDraw {
        /// The pipeline, then the mesh: what changes least often comes first.
        std::uint64_t key;
        MeshHandle mesh;
        MaterialHandle material;
        /// Index in the view.
        std::uint32_t draw;
        bool castsShadows;
        /// Within the view distance; otherwise the draw only casts shadows.
        bool visible;
        /// Where its skinned vertices start in m_skinned, or kNotSkinned.
        std::uint32_t firstSkinnedVertex;
        /// Drawn by the glass pass, not by the G-buffer.
        bool glass;
    };
    static constexpr std::uint32_t kNotSkinned = 0xFFFFFFFF;
    /// Binds the vertices and indices of a draw, unless they are bound already.
    void BindGeometry(rhi::CommandList& commands, const SortedDraw& draw, rhi::BufferHandle& boundVertices,
                      std::uint64_t& boundOffset, MeshHandle& boundIndices);
    /// The frame that Prepare filled, if any. Its instances follow m_sorted.
    FrameSlot* m_prepared = nullptr;
    Counts m_counts;
    float m_normalStrength = 1.0f;
    /// The light sources of the last frame prepared, which name its lights (ADR-0044), and its number of
    /// lights, which the next frame maps; the number of lights the prepared frame's map holds.
    std::vector<LightSourceRange> m_previousRanges;
    /// Where each analytic light of the last frame prepared was, by its identity key: position and direction,
    /// to tell the lights that moved.
    /// Where a light was last frame, and how bright, to tell whether it moved.
    struct LightPlace {
        glm::vec3 position{0.0f};
        glm::vec3 direction{0.0f};
        float brightness = 0.0f;
    };
    std::unordered_map<std::uint64_t, LightPlace> m_previousPlaces;
    std::uint32_t m_lightCount = 0;
    std::uint32_t m_mappedLightCount = 0;
    /// Counts the frames prepared with a camera, which the readback of emission waits on.
    std::uint64_t m_prepareCount = 0;
    std::map<EmissiveKey, EmissiveSource> m_emissiveSources;
    /// The sources whose emission the next AddEmissiveLights averages, and the frame's emissive draws.
    struct AverageJob {
        EmissiveKey key;
        MeshHandle mesh;
        MaterialHandle material;
    };
    std::vector<AverageJob> m_averageJobs;
    std::vector<EmissiveJob> m_emissiveJobs;
    rhi::PipelineHandle m_emissiveAverage;
    rhi::PipelineHandle m_emissiveTriangles;
    /// Reused from frame to frame.
    std::vector<SortedDraw> m_sorted;
    /// The skinned vertices of the frame's skinned draws, in world space, written by the skinning pass.
    rhi::BufferHandle m_skinned;
    /// Their positions in the previous frame, skinned with the previous joints: three floats each.
    rhi::BufferHandle m_previousSkinned;
    /// Joints of the prepared frame; the previous joints follow them in the frame slot's buffer.
    std::uint32_t m_jointCount = 0;
    std::uint64_t m_skinnedCapacity = 0;
    rhi::PipelineHandle m_skinningPipeline;
    std::vector<SkinningJob> m_skinningJobs;
    std::vector<TracedDraw> m_traced;
    glm::mat4 m_viewProjection{1.0f};
    Perspective m_perspective;
    /// Of the last frame prepared, for its successor's motion vectors.
    std::optional<glm::mat4> m_previousViewProjection;
    /// The pixels the prepared frame draws, and those of the frame before, whose histories the next reads.
    rhi::Extent m_renderExtent;
    rhi::Extent m_previousRenderExtent;
    bool m_hasGlass = false;
};

} // namespace daidalos::renderer
