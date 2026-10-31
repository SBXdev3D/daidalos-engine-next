// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <glm/ext/matrix_transform.hpp>

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <utility>
#include <vector>

#include "GpuFixture.hpp"
#include "MeshPass.hpp"
#include "ShaderCompiler.hpp"

using namespace daidalos;
using gpu_tests::GpuTest;
using renderer::CameraView;
using renderer::MeshData;
using renderer::MeshHandle;
using renderer::MeshPass;
using renderer::SceneView;
using renderer::Vertex;

namespace {

constexpr rhi::Extent kExtent{64, 64};
constexpr rhi::Format kColorFormat = rhi::Format::Rgba8Unorm;
constexpr rhi::Format kDepthFormat = rhi::Format::D32Float;
constexpr glm::vec4 kBackground{0.0f, 0.0f, 0.0f, 1.0f};

// Faces the camera with counter-clockwise winding and a normal towards +Z.
MeshData MakeQuad(float left, float right, float bottom, float top, float z, const glm::vec3& color) {
    const glm::vec3 normal(0.0f, 0.0f, 1.0f);
    return {{{{left, bottom, z}, normal, color},
             {{right, bottom, z}, normal, color},
             {{right, top, z}, normal, color},
             {{left, top, z}, normal, color}},
            {0, 1, 2, 0, 2, 3}};
}

MeshData Reversed(MeshData mesh) {
    for (std::size_t index = 0; index + 2 < mesh.indices.size(); index += 3) {
        std::swap(mesh.indices[index + 1], mesh.indices[index + 2]);
    }
    return mesh;
}

// Camera at +Z looking at the origin.
CameraView TestCamera() {
    return {glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -5.0f)), glm::radians(60.0f), 0.1f};
}

class MeshPassTest : public GpuTest {
protected:
    void SetUp() override {
        GpuTest::SetUp();
        if (IsSkipped() || HasFatalFailure()) {
            return;
        }
        core::Result<MeshPass> pass = MeshPass::Create(Device(), kColorFormat, kDepthFormat);
        ASSERT_TRUE(pass.has_value()) << core::Format(pass.error());
        m_pass.emplace(std::move(*pass));

        const core::Result<rhi::TextureHandle> color =
            Device().CreateTexture({.extent = kExtent,
                                    .format = kColorFormat,
                                    .usage = rhi::TextureUsage::ColorTarget | rhi::TextureUsage::TransferSrc,
                                    .name = "Color"});
        ASSERT_TRUE(color.has_value());
        m_color = *color;
        const core::Result<rhi::TextureHandle> depth = Device().CreateTexture(
            {.extent = kExtent, .format = kDepthFormat, .usage = rhi::TextureUsage::DepthTarget, .name = "Depth"});
        ASSERT_TRUE(depth.has_value());
        m_depth = *depth;
        const core::Result<rhi::BufferHandle> readback =
            Device().CreateBuffer({.size = std::uint64_t{kExtent.width} * kExtent.height * 4,
                                   .usage = rhi::BufferUsage::TransferDst,
                                   .memory = rhi::MemoryLocation::GpuToCpu,
                                   .name = "Readback"});
        ASSERT_TRUE(readback.has_value());
        m_readback = *readback;
    }

    void TearDown() override {
        if (m_pass.has_value()) {
            m_pass.reset();
            Device().DestroyTexture(m_color);
            Device().DestroyTexture(m_depth);
            Device().DestroyBuffer(m_readback);
        }
        GpuTest::TearDown();
    }

    MeshHandle Mesh(const MeshData& data) {
        const core::Result<MeshHandle> mesh = m_pass->CreateMesh(data, "Test");
        EXPECT_TRUE(mesh.has_value());
        return mesh.value_or(MeshHandle{});
    }

    void Render(const SceneView& view) {
        ASSERT_TRUE(Device()
                        .Submit([&](rhi::CommandList& commands) {
                            commands.Discard(m_color, rhi::TextureState::ColorTarget);
                            commands.Discard(m_depth, rhi::TextureState::DepthTarget);
                            commands.BeginRendering({.colorTarget = m_color,
                                                     .clearColor = kBackground,
                                                     .depthTarget = m_depth,
                                                     .clearDepth = 0.0f});
                            m_pass->Record(commands, view, 1.0f);
                            commands.EndRendering();
                            commands.Transition(m_color, rhi::TextureState::TransferSrc);
                            commands.CopyTextureToBuffer(m_color, m_readback);
                        })
                        .has_value());
    }

    // Red, green and blue of a pixel, counted from the top-left corner.
    [[nodiscard]] glm::ivec3 Pixel(std::uint32_t x, std::uint32_t y) {
        std::array<std::uint8_t, 4> pixel{};
        const std::size_t offset = (std::size_t{y} * kExtent.width + x) * pixel.size();
        std::memcpy(pixel.data(), Device().Mapped(m_readback).data() + offset, pixel.size());
        return {pixel[0], pixel[1], pixel[2]};
    }

    [[nodiscard]] bool IsBackground(std::uint32_t x, std::uint32_t y) { return Pixel(x, y) == glm::ivec3(0); }

    std::optional<MeshPass> m_pass;
    rhi::TextureHandle m_color;
    rhi::TextureHandle m_depth;
    rhi::BufferHandle m_readback;
};

} // namespace

TEST_F(MeshPassTest, DrawsAMeshInFrontOfTheCamera) {
    const MeshHandle mesh = Mesh(MakeQuad(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f, glm::vec3(1.0f, 0.0f, 0.0f)));
    Render({TestCamera(), {{mesh, glm::mat4(1.0f)}}});

    const glm::ivec3 center = Pixel(32, 32);
    EXPECT_GT(center.r, 0);
    EXPECT_EQ(center.g, 0);
    EXPECT_EQ(center.b, 0);
    EXPECT_TRUE(IsBackground(0, 0));
}

TEST_F(MeshPassTest, NothingIsDrawnWithoutACamera) {
    const MeshHandle mesh = Mesh(MakeQuad(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f, glm::vec3(1.0f)));
    Render({std::nullopt, {{mesh, glm::mat4(1.0f)}}});
    EXPECT_TRUE(IsBackground(32, 32));
}

TEST_F(MeshPassTest, PositiveYIsAtTheTopOfTheImage) {
    const MeshHandle mesh = Mesh(MakeQuad(-1.0f, 1.0f, 0.5f, 1.5f, 0.0f, glm::vec3(1.0f)));
    Render({TestCamera(), {{mesh, glm::mat4(1.0f)}}});
    EXPECT_FALSE(IsBackground(32, 20));
    EXPECT_TRUE(IsBackground(32, 44));
}

TEST_F(MeshPassTest, PositiveXIsOnTheRightOfTheImage) {
    const MeshHandle mesh = Mesh(MakeQuad(0.5f, 1.5f, -1.0f, 1.0f, 0.0f, glm::vec3(1.0f)));
    Render({TestCamera(), {{mesh, glm::mat4(1.0f)}}});
    EXPECT_FALSE(IsBackground(44, 32));
    EXPECT_TRUE(IsBackground(20, 32));
}

TEST_F(MeshPassTest, ClockwiseTrianglesAreCulled) {
    const MeshHandle mesh = Mesh(Reversed(MakeQuad(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f, glm::vec3(1.0f))));
    Render({TestCamera(), {{mesh, glm::mat4(1.0f)}}});
    EXPECT_TRUE(IsBackground(32, 32));
}

TEST_F(MeshPassTest, NearerSurfaceWinsWhicheverIsDrawnFirst) {
    const MeshHandle nearQuad = Mesh(MakeQuad(-1.0f, 1.0f, -1.0f, 1.0f, 1.0f, glm::vec3(1.0f, 0.0f, 0.0f)));
    const MeshHandle farQuad = Mesh(MakeQuad(-1.0f, 1.0f, -1.0f, 1.0f, -1.0f, glm::vec3(0.0f, 1.0f, 0.0f)));

    Render({TestCamera(), {{nearQuad, glm::mat4(1.0f)}, {farQuad, glm::mat4(1.0f)}}});
    EXPECT_GT(Pixel(32, 32).r, 0);
    EXPECT_EQ(Pixel(32, 32).g, 0);

    Render({TestCamera(), {{farQuad, glm::mat4(1.0f)}, {nearQuad, glm::mat4(1.0f)}}});
    EXPECT_GT(Pixel(32, 32).r, 0);
    EXPECT_EQ(Pixel(32, 32).g, 0);
}

TEST_F(MeshPassTest, WorldMatrixMovesTheMesh) {
    const MeshHandle mesh = Mesh(MakeQuad(-0.5f, 0.5f, -0.5f, 0.5f, 0.0f, glm::vec3(1.0f)));
    Render({TestCamera(), {{mesh, glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f))}}});
    EXPECT_TRUE(IsBackground(32, 32));
    EXPECT_FALSE(IsBackground(44, 32));
}

TEST_F(MeshPassTest, NormalsFollowTheWorldRotation) {
    // Lit from above: the same quad turned to face up receives more light.
    const MeshHandle mesh = Mesh(MakeQuad(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f, glm::vec3(1.0f)));
    Render({TestCamera(), {{mesh, glm::mat4(1.0f)}}});
    const int facingCamera = Pixel(32, 32).r;

    const glm::mat4 tilted = glm::rotate(glm::mat4(1.0f), glm::radians(-60.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    Render({TestCamera(), {{mesh, tilted}}});
    EXPECT_GT(Pixel(32, 32).r, facingCamera);
}

TEST_F(MeshPassTest, DestroyedMeshIsSkipped) {
    const MeshHandle mesh = Mesh(MakeQuad(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f, glm::vec3(1.0f)));
    m_pass->DestroyMesh(mesh);
    Render({TestCamera(), {{mesh, glm::mat4(1.0f)}}});
    EXPECT_TRUE(IsBackground(32, 32));
}

TEST_F(MeshPassTest, ReloadedShadersAreUsedByTheNextFrame) {
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "daidalos-reload-test";
    std::filesystem::create_directories(directory);
    const std::filesystem::path source = directory / "Green.hlsl";
    std::ofstream(source) << R"(
#include "MeshTypes.hlsli"
[[vk::push_constant]] MeshPushConstants g_push;
float4 VSMain(float3 position : POSITION) : SV_Position {
    return mul(g_push.viewProjection, mul(g_push.world, float4(position, 1.0)));
}
float4 PSMain() : SV_Target { return float4(0.0, 1.0, 0.0, 1.0); }
)";
    std::filesystem::copy_file(std::filesystem::path(DAI_SHADER_SOURCE_DIR) / "MeshTypes.hlsli",
                               directory / "MeshTypes.hlsli", std::filesystem::copy_options::overwrite_existing);

    const auto vertex = renderer::CompileShader(DAI_DXC_PATH, source, renderer::ShaderStage::Vertex);
    ASSERT_TRUE(vertex.has_value()) << core::Format(vertex.error());
    const auto pixel = renderer::CompileShader(DAI_DXC_PATH, source, renderer::ShaderStage::Pixel);
    ASSERT_TRUE(pixel.has_value()) << core::Format(pixel.error());
    ASSERT_TRUE(m_pass->ReplaceShaders(*vertex, *pixel).has_value());

    const MeshHandle mesh = Mesh(MakeQuad(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f, glm::vec3(1.0f, 0.0f, 0.0f)));
    Render({TestCamera(), {{mesh, glm::mat4(1.0f)}}});
    EXPECT_EQ(Pixel(32, 32), glm::ivec3(0, 255, 0));
    std::filesystem::remove_all(directory);
}
