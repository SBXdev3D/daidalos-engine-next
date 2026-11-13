// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/core/SlotPool.hpp>
#include <daidalos/renderer/Material.hpp>
#include <daidalos/renderer/SceneView.hpp>
#include <daidalos/resources/ModelData.hpp>
#include <daidalos/rhi/Device.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "LightSampling.hpp"

namespace daidalos::renderer {

class RenderGraph;

/// The targets of a frame that the mesh pass needs to know about.
struct FrameTargets {
    rhi::Extent extent;
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
    /// What the glass pass reads: a copy of the frame before glass, and the pre-pass's depth (ADR-0042).
    rhi::TextureHandle background;
    rhi::TextureHandle depth;
    /// What luminance in nits is multiplied by before tonemapping (ADR-0043): lights, emission and the sky
    /// reach the GPU multiplied by it, so that its targets hold values near those of the screen.
    float exposure = 1.0f;
};

/// What the G-buffer holds of the material, and what the lighting pass writes (ADR-0039, ADR-0044), which
/// the composite pass puts back together. The G-buffer's normals and motion vectors are the renderer's.
struct ShadingTargets {
    /// The base color, and the metalness in alpha (MeshPass::kBaseColorFormat).
    rhi::TextureHandle baseColor;
    /// The emission in nits times the exposure, and the occlusion in alpha (MeshPass::kEmissionFormat).
    rhi::TextureHandle emission;
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

    [[nodiscard]] core::Result<MaterialHandle> CreateMaterial(const MaterialDesc& desc);
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
    /// The lighting pass's diffuse and specular light, for the denoiser (ADR-0039).
    static constexpr rhi::Format kLightFormat = rhi::Format::Rgba16Float;

    /// The G-buffer's material targets and the lighting pass's, at this size.
    [[nodiscard]] core::Result<ShadingTargets> CreateShadingTargets(rhi::Extent extent) const;
    void DestroyShadingTargets(ShadingTargets& targets) const;
    /// The rendering of RecordGBuffer: normals (kNormalFormat), motion vectors (kMotionFormat), the material
    /// targets and the depth, all cleared.
    [[nodiscard]] static rhi::RenderingDesc GBufferRendering(const ShadingTargets& targets, rhi::TextureHandle normals,
                                                             rhi::TextureHandle motion, rhi::TextureHandle depth);

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
    /// Adds the pass that skins the prepared frame's skinned draws, if it has any, as "Skinning". It must
    /// come before every pass that draws or traces them.
    void AddSkinning(RenderGraph& graph);
    /// The skinned vertices of the prepared frame, which the passes that draw meshes read as vertices. Null
    /// when the frame has no skinned draw.
    [[nodiscard]] rhi::BufferHandle SkinnedVertices() const {
        return m_skinningJobs.empty() ? rhi::BufferHandle{} : m_skinned;
    }
    /// Where the skinned vertices were in the previous frame, which the pre-pass reads for motion vectors.
    /// Null when the frame has no skinned draw.
    [[nodiscard]] rhi::BufferHandle PreviousSkinnedPositions() const {
        return m_skinningJobs.empty() ? rhi::BufferHandle{} : m_previousSkinned;
    }
    /// The prepared frame's draws, in the order of its instances: a ray's custom index is an instance.
    [[nodiscard]] std::span<const TracedDraw> TracedDraws() const { return m_traced; }
    /// The storage index of the prepared frame's instances.
    [[nodiscard]] std::uint32_t InstanceDataIndex() const;
    /// The view and projection of the prepared frame, without jitter.
    [[nodiscard]] const glm::mat4& ViewProjection() const { return m_viewProjection; }
    /// The storage index of the material table.
    [[nodiscard]] std::uint32_t MaterialDataIndex() const { return m_device->StorageIndex(m_materialTable); }
    /// Writes the G-buffer of the opaque draws, inside a GBufferRendering scope. Skinned draws read
    /// PreviousSkinnedPositions.
    void RecordGBuffer(rhi::CommandList& commands);
    /// Draws the prepared frame's glass over the frame (ADR-0042), inside a rendering scope with a target of the
    /// mesh pass's color format and no depth: it reads the G-buffer's depth and the background of FrameTargets.
    void RecordGlass(rhi::CommandList& commands);
    /// The prepared frame has glass to draw.
    [[nodiscard]] bool HasGlass() const { return m_prepared != nullptr && m_hasGlass; }
    /// Adds the pass that puts the shading targets back together into target, with the fog and the sky, as
    /// "Composite" (ADR-0039). target has the mesh pass's color format and is written whole.
    void AddComposite(RenderGraph& graph, rhi::TextureHandle target, const CompositeInputs& inputs);

    /// The storage index of the prepared frame's FrameData, for the passes that read it.
    [[nodiscard]] std::uint32_t FrameDataIndex() const;
    /// Where each light of the frame prepared before this one is in this one's list (ADR-0044): the storage
    /// index of a uint per light, kGone for a light that is gone, and the number of lights it maps.
    [[nodiscard]] std::uint32_t LightMapIndex() const;
    [[nodiscard]] std::uint32_t PreviousLightCount() const { return m_previousLightCount; }

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
    };
    struct Material {
        bool doubleSided = false;
        bool alphaTested = false;
        bool glass = false;
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
        /// The alias table of the light sources, by power.
        rhi::BufferHandle sources;
        std::uint64_t sourceCapacity = 0;
        /// LightMapIndex's buffer.
        rhi::BufferHandle lightMap;
        std::uint64_t lightMapCapacity = 0;
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
    /// Fills the frame slot's lights, their alias table and the map from the last frame's lights.
    void PrepareLights(FrameSlot& slot, const SceneView& view, float exposure);
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
    /// What names each light of the last frame prepared, by index, and each of this one's (ADR-0044).
    std::vector<std::uint64_t> m_previousLightKeys;
    std::vector<std::uint64_t> m_lightKeys;
    std::uint32_t m_previousLightCount = 0;
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
    /// Of the last frame prepared, for its successor's motion vectors.
    std::optional<glm::mat4> m_previousViewProjection;
    bool m_hasGlass = false;
};

} // namespace daidalos::renderer
