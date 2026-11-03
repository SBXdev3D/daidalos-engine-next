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
#include <span>
#include <string>
#include <vector>

namespace daidalos::renderer {

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

    /// Replaces the pipelines. On failure the current ones stay in use.
    [[nodiscard]] core::Result<void> ReplaceShaders(std::span<const std::uint32_t> vertexShader,
                                                    std::span<const std::uint32_t> fragmentShader);

    /// A frame takes three steps. Prepare fills the frame's buffers from the view, outside any rendering
    /// scope; aspect = width / height. Draws whose mesh has been destroyed are skipped, and draws whose
    /// material has been use the default one. Without a camera, or after a failure to allocate, the
    /// frame draws nothing.
    [[nodiscard]] core::Result<void> Prepare(const SceneView& view, float aspect);
    /// Draws the shadow maps of the prepared frame, outside any rendering scope.
    void RecordShadows(rhi::CommandList& commands);
    /// Draws the prepared frame inside the current rendering scope.
    void Record(rhi::CommandList& commands);

private:
    struct Mesh {
        rhi::BufferHandle vertices;
        rhi::BufferHandle indices;
        /// Joints and weights of each vertex, read by the vertex shader. Null for a rigid mesh.
        rhi::BufferHandle skin;
        std::uint32_t indexCount = 0;
    };
    struct Material {
        bool doubleSided = false;
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
        rhi::BufferHandle shadows;
    };

    MeshPass(rhi::Device& device, rhi::Format colorFormat, rhi::Format depthFormat);

    [[nodiscard]] core::Result<void> CreateShadowResources();
    [[nodiscard]] core::Result<rhi::PipelineHandle> BuildPipeline(std::span<const std::uint32_t> vertexShader,
                                                                  std::span<const std::uint32_t> fragmentShader,
                                                                  rhi::CullMode cull);
    [[nodiscard]] core::Result<void> PrepareFrameSlot(FrameSlot& slot, const SceneView& view);
    /// Makes room for a material at this index of the table.
    [[nodiscard]] core::Result<void> ReserveMaterialSlot(std::uint32_t index);
    [[nodiscard]] std::uint32_t TextureIndex(rhi::TextureHandle texture) const;

    rhi::Device* m_device;
    rhi::Format m_colorFormat;
    rhi::Format m_depthFormat;
    rhi::PipelineHandle m_pipeline;
    rhi::PipelineHandle m_doubleSidedPipeline;
    rhi::PipelineHandle m_shadowPipeline;
    /// Every shadow map of a frame, side by side.
    rhi::TextureHandle m_shadowAtlas;
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
    };
    /// The frame that Prepare filled, if any. Its instances follow m_sorted.
    FrameSlot* m_prepared = nullptr;
    std::uint32_t m_shadowCount = 0;
    /// Reused from frame to frame.
    std::vector<SortedDraw> m_sorted;
};

/// The lights that get a shadow map: spot lights that ask for one, nearest to the camera first, at most
/// max of them. Returns their indices in the view.
[[nodiscard]] std::vector<std::uint32_t> ChooseShadowedLights(std::span<const Light> lights, const glm::vec3& camera,
                                                              std::size_t max);

} // namespace daidalos::renderer
