// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/core/SlotPool.hpp>
#include <daidalos/renderer/Primitives.hpp>
#include <daidalos/renderer/Scene.hpp>
#include <daidalos/rhi/Device.hpp>

#include <cstdint>
#include <span>
#include <string>

namespace daidalos::renderer {

/// Owns the meshes and the pipeline that draws them. It works on any target, so that tests can run
/// it without a window. The Device must outlive it.
class MeshPass {
public:
    static core::Result<MeshPass> Create(rhi::Device& device, rhi::Format colorFormat, rhi::Format depthFormat);

    MeshPass(MeshPass&& other) noexcept;
    MeshPass& operator=(MeshPass&& other) = delete;
    MeshPass(const MeshPass&) = delete;
    MeshPass& operator=(const MeshPass&) = delete;
    ~MeshPass();

    [[nodiscard]] core::Result<MeshHandle> CreateMesh(const MeshData& data, const std::string& name);
    void DestroyMesh(MeshHandle mesh);

    /// Replaces the pipeline. On failure the current one stays in use.
    [[nodiscard]] core::Result<void> ReplaceShaders(std::span<const std::uint32_t> vertexShader,
                                                    std::span<const std::uint32_t> fragmentShader);

    /// Draws the view inside the current rendering scope. aspect = width / height. Draws whose mesh
    /// has been destroyed are skipped.
    void Record(rhi::CommandList& commands, const SceneView& view, float aspect) const;

private:
    struct Mesh {
        rhi::BufferHandle vertices;
        rhi::BufferHandle indices;
        std::uint32_t indexCount = 0;
    };

    MeshPass(rhi::Device& device, rhi::Format colorFormat, rhi::Format depthFormat);

    [[nodiscard]] core::Result<rhi::PipelineHandle> BuildPipeline(std::span<const std::uint32_t> vertexShader,
                                                                  std::span<const std::uint32_t> fragmentShader);

    rhi::Device* m_device;
    rhi::Format m_colorFormat;
    rhi::Format m_depthFormat;
    rhi::PipelineHandle m_pipeline;
    core::SlotPool<Mesh, MeshTag> m_meshes;
};

} // namespace daidalos::renderer
