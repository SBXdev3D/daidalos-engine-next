// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/core/SlotPool.hpp>
#include <daidalos/renderer/Material.hpp>
#include <daidalos/renderer/Scene.hpp>
#include <daidalos/resources/ModelData.hpp>
#include <daidalos/rhi/Device.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string>

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

    /// Draws the view inside the current rendering scope. aspect = width / height. Draws whose mesh
    /// has been destroyed are skipped, and draws whose material has been use the default one. An
    /// allocation failure skips the frame's draws and reports the error.
    [[nodiscard]] core::Result<void> Record(rhi::CommandList& commands, const SceneView& view, float aspect);

private:
    struct Mesh {
        rhi::BufferHandle vertices;
        rhi::BufferHandle indices;
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
    };

    MeshPass(rhi::Device& device, rhi::Format colorFormat, rhi::Format depthFormat);

    [[nodiscard]] core::Result<rhi::PipelineHandle> BuildPipeline(std::span<const std::uint32_t> vertexShader,
                                                                  std::span<const std::uint32_t> fragmentShader,
                                                                  rhi::CullMode cull);
    [[nodiscard]] core::Result<void> PrepareFrameSlot(FrameSlot& slot, std::uint64_t instanceCount);
    /// Makes room for a material at this index of the table.
    [[nodiscard]] core::Result<void> ReserveMaterialSlot(std::uint32_t index);
    [[nodiscard]] std::uint32_t TextureIndex(rhi::TextureHandle texture) const;

    rhi::Device* m_device;
    rhi::Format m_colorFormat;
    rhi::Format m_depthFormat;
    rhi::PipelineHandle m_pipeline;
    rhi::PipelineHandle m_doubleSidedPipeline;
    core::SlotPool<Mesh, MeshTag> m_meshes;
    /// The index of a handle is also its index in the material table of the GPU.
    core::SlotPool<Material, MaterialTag> m_materials;
    rhi::BufferHandle m_materialTable;
    std::uint32_t m_materialCapacity = 0;
    MaterialHandle m_defaultMaterial;
    std::array<FrameSlot, rhi::kFramesInFlight> m_frameSlots;
    std::uint32_t m_nextFrameSlot = 0;
};

} // namespace daidalos::renderer
