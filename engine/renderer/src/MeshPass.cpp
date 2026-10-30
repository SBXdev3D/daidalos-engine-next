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

#include <cstddef>
#include <format>
#include <utility>

namespace daidalos::renderer {

namespace {

// Matches MeshPushConstants in shaders/MeshTypes.hlsli.
struct MeshPushConstants {
    glm::mat4 viewProjection;
    glm::mat4 world;
};

} // namespace

MeshPass::MeshPass(rhi::Device& device, rhi::Format colorFormat, rhi::Format depthFormat)
    : m_device(&device), m_colorFormat(colorFormat), m_depthFormat(depthFormat) {}

MeshPass::MeshPass(MeshPass&& other) noexcept
    : m_device(other.m_device), m_colorFormat(other.m_colorFormat), m_depthFormat(other.m_depthFormat),
      m_pipeline(std::exchange(other.m_pipeline, {})), m_meshes(std::exchange(other.m_meshes, {})) {}

MeshPass::~MeshPass() {
    m_device->DestroyPipeline(m_pipeline);
    m_meshes.ForEach([this](const Mesh& mesh) {
        m_device->DestroyBuffer(mesh.vertices);
        m_device->DestroyBuffer(mesh.indices);
    });
}

core::Result<MeshPass> MeshPass::Create(rhi::Device& device, rhi::Format colorFormat, rhi::Format depthFormat) {
    MeshPass pass(device, colorFormat, depthFormat);
    core::Result<void> built = pass.ReplaceShaders(rhi::ToSpirv(kMeshVsSpirv), rhi::ToSpirv(kMeshPsSpirv));
    if (!built) {
        return tl::unexpected(std::move(built.error()));
    }
    return pass;
}

core::Result<rhi::PipelineHandle> MeshPass::BuildPipeline(std::span<const std::uint32_t> vertexShader,
                                                          std::span<const std::uint32_t> fragmentShader) {
    return m_device->CreatePipeline({
        .vertexShader = vertexShader,
        .fragmentShader = fragmentShader,
        .vertexStride = sizeof(Vertex),
        .vertexAttributes = {{0, rhi::Format::Rgb32Float, static_cast<std::uint32_t>(offsetof(Vertex, position))},
                             {1, rhi::Format::Rgb32Float, static_cast<std::uint32_t>(offsetof(Vertex, normal))},
                             {2, rhi::Format::Rgb32Float, static_cast<std::uint32_t>(offsetof(Vertex, color))}},
        .colorFormat = m_colorFormat,
        .depthFormat = m_depthFormat,
        .name = "Mesh",
    });
}

core::Result<void> MeshPass::ReplaceShaders(std::span<const std::uint32_t> vertexShader,
                                            std::span<const std::uint32_t> fragmentShader) {
    core::Result<rhi::PipelineHandle> pipeline = BuildPipeline(vertexShader, fragmentShader);
    if (!pipeline) {
        return tl::unexpected(std::move(pipeline.error()));
    }
    m_device->DestroyPipeline(m_pipeline);
    m_pipeline = *pipeline;
    return {};
}

core::Result<MeshHandle> MeshPass::CreateMesh(const MeshData& data, const std::string& name) {
    DAI_ASSERT(!data.vertices.empty() && !data.indices.empty(), "A mesh cannot be empty");

    const auto vertexBytes = std::as_bytes(std::span(data.vertices));
    const auto indexBytes = std::as_bytes(std::span(data.indices));

    core::Result<rhi::BufferHandle> vertices =
        m_device->CreateBuffer({.size = vertexBytes.size(),
                                .usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::TransferDst,
                                .name = std::format("{} vertices", name)});
    if (!vertices) {
        return tl::unexpected(std::move(vertices.error()));
    }
    core::Result<rhi::BufferHandle> indices =
        m_device->CreateBuffer({.size = indexBytes.size(),
                                .usage = rhi::BufferUsage::Index | rhi::BufferUsage::TransferDst,
                                .name = std::format("{} indices", name)});
    if (!indices) {
        m_device->DestroyBuffer(*vertices);
        return tl::unexpected(std::move(indices.error()));
    }

    core::Result<void> uploaded = m_device->UploadBuffer(*vertices, vertexBytes).and_then([&] {
        return m_device->UploadBuffer(*indices, indexBytes);
    });
    if (!uploaded) {
        m_device->DestroyBuffer(*vertices);
        m_device->DestroyBuffer(*indices);
        return tl::unexpected(std::move(uploaded.error()));
    }
    return m_meshes.Insert({*vertices, *indices, static_cast<std::uint32_t>(data.indices.size())});
}

void MeshPass::DestroyMesh(MeshHandle mesh) {
    if (const std::optional<Mesh> removed = m_meshes.Remove(mesh)) {
        m_device->DestroyBuffer(removed->vertices);
        m_device->DestroyBuffer(removed->indices);
    }
}

void MeshPass::Record(rhi::CommandList& commands, const SceneView& view, float aspect) const {
    DAI_PROFILE_SCOPE("renderer::MeshPass");
    if (!view.camera.has_value()) {
        return;
    }
    const CameraView& camera = *view.camera;
    const glm::mat4 viewProjection =
        math::InfinitePerspectiveReverseZ(camera.verticalFov, aspect, camera.nearPlane) * camera.view;

    commands.BindPipeline(m_pipeline);
    for (const DrawItem& draw : view.draws) {
        const Mesh* mesh = m_meshes.TryGet(draw.mesh);
        if (mesh == nullptr) {
            continue;
        }
        commands.BindVertexBuffer(mesh->vertices);
        commands.BindIndexBuffer(mesh->indices, rhi::IndexType::Uint32);
        commands.Push(MeshPushConstants{viewProjection, draw.world});
        commands.DrawIndexed(mesh->indexCount);
    }
}

} // namespace daidalos::renderer
