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

#include "LightClusters.hpp"

namespace daidalos::renderer {

class RenderGraph;

/// The targets of a frame that the mesh pass needs to know about.
struct FrameTargets {
    rhi::Extent extent;
    /// The ambient occlusion of the frame, or null to leave ambient light unoccluded.
    rhi::TextureHandle occlusion;
    /// Index of the top-level acceleration structure the frame traces, for FrameData.
    std::uint32_t accelerationStructure = 0;
    /// Where the frame samples each pixel, relative to its center, in pixels, y down: the jitter of
    /// temporal antialiasing (ADR-0037). Motion vectors leave it out.
    glm::vec2 jitter{0.0f};
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
};

/// Owns the meshes, the materials and the pipelines that draw them. It works on any target, so that
/// tests can run it without a window. The Device must outlive it.
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

    /// Normals of the pre-pass: world normals, times 0.5 plus 0.5.
    static constexpr rhi::Format kNormalFormat = rhi::Format::Rgba8Unorm;
    /// Motion vectors of the pre-pass, its second target: where each pixel's surface was in the previous
    /// frame minus where it is, in texture coordinates (ADR-0037).
    static constexpr rhi::Format kMotionFormat = rhi::Format::Rg16Float;

    /// Replaces the shaders of the mesh pass. The pre-pass uses the same vertex shader, so that both
    /// compute the same depth. On failure the current pipelines stay in use.
    [[nodiscard]] core::Result<void> ReplaceShaders(std::span<const std::uint32_t> vertexShader,
                                                    std::span<const std::uint32_t> fragmentShader);

    /// A frame takes four steps (ADR-0024). Prepare fills the frame's buffers from the view, outside any
    /// rendering scope. Draws whose mesh has been destroyed are skipped, and draws whose material has been
    /// use the default one. Without a camera, or after a failure to allocate, the frame draws nothing.
    [[nodiscard]] core::Result<void> Prepare(const SceneView& view, const FrameTargets& targets);
    /// What the last Prepare chose to draw: FrameStats' draw, triangle and light counts.
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
    /// Writes depth, normals and motion vectors, inside a rendering scope with a kNormalFormat target, a
    /// kMotionFormat target and a cleared depth. Skinned draws read PreviousSkinnedPositions.
    void RecordPrepass(rhi::CommandList& commands);
    /// Shades the prepared frame inside a rendering scope whose depth holds the pre-pass's: each pixel is
    /// shaded once, where its depth equals the pre-pass's. Then draws the sky where nothing was drawn, if
    /// the frame has one.
    void Record(rhi::CommandList& commands);

    /// The storage index of the prepared frame's FrameData, for the passes that read it.
    [[nodiscard]] std::uint32_t FrameDataIndex() const;

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
        rhi::BufferHandle clusters;
        rhi::BufferHandle clusterLights;
        std::uint64_t clusterLightCapacity = 0;
    };

    MeshPass(rhi::Device& device, rhi::Format colorFormat, rhi::Format depthFormat);

    [[nodiscard]] core::Result<void> CreateClusterBuffers();
    /// The pipelines of the pre-pass and the mesh pass, one-sided and double-sided.
    struct Pipelines {
        rhi::PipelineHandle prepass;
        rhi::PipelineHandle prepassDoubleSided;
        rhi::PipelineHandle mesh;
        rhi::PipelineHandle meshDoubleSided;
        rhi::PipelineHandle sky;
    };
    [[nodiscard]] core::Result<rhi::PipelineHandle> BuildPipeline(std::span<const std::uint32_t> vertexShader,
                                                                  std::span<const std::uint32_t> fragmentShader,
                                                                  rhi::CullMode cull, bool prepass);
    void DestroyPipelines(Pipelines& pipelines);
    /// Draws the sorted draws with these pipelines, one-sided and double-sided.
    void RecordDraws(rhi::CommandList& commands, rhi::PipelineHandle oneSided, rhi::PipelineHandle doubleSided);
    [[nodiscard]] core::Result<void> PrepareFrameSlot(FrameSlot& slot, const SceneView& view);
    /// Makes room for a material at this index of the table.
    [[nodiscard]] core::Result<void> ReserveMaterialSlot(std::uint32_t index);
    [[nodiscard]] std::uint32_t TextureIndex(rhi::TextureHandle texture) const;

    rhi::Device* m_device;
    rhi::Format m_colorFormat;
    rhi::Format m_depthFormat;
    Pipelines m_pipelines;
    /// The shaders of the mesh pass, to rebuild its pipelines.
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
    };
    static constexpr std::uint32_t kNotSkinned = 0xFFFFFFFF;
    /// Binds the vertices and indices of a draw, unless they are bound already.
    void BindGeometry(rhi::CommandList& commands, const SortedDraw& draw, rhi::BufferHandle& boundVertices,
                      std::uint64_t& boundOffset, MeshHandle& boundIndices);
    /// The frame that Prepare filled, if any. Its instances follow m_sorted.
    FrameSlot* m_prepared = nullptr;
    Counts m_counts;
    float m_normalStrength = 1.0f;
    /// Reused from frame to frame.
    LightClusters m_clusters;
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
    bool m_hasSky = false;
};

} // namespace daidalos::renderer
