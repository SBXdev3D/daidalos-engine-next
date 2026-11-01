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
using renderer::MaterialDesc;
using renderer::MaterialHandle;
using renderer::MeshHandle;
using renderer::MeshPass;
using renderer::SceneView;
using resources::MeshData;
using resources::Vertex;

namespace {

constexpr rhi::Extent kExtent{64, 64};
constexpr rhi::Format kColorFormat = rhi::Format::Rgba8Unorm;
constexpr rhi::Format kDepthFormat = rhi::Format::D32Float;
constexpr glm::vec4 kBackground{0.0f, 0.0f, 0.0f, 1.0f};

// Faces the camera with counter-clockwise winding and a normal towards +Z.
MeshData MakeQuad(float left, float right, float bottom, float top, float z) {
    const glm::vec3 normal(0.0f, 0.0f, 1.0f);
    const glm::vec4 tangent(1.0f, 0.0f, 0.0f, 1.0f);
    return {{{{left, bottom, z}, normal, tangent, {0.0f, 1.0f}},
             {{right, bottom, z}, normal, tangent, {1.0f, 1.0f}},
             {{right, top, z}, normal, tangent, {1.0f, 0.0f}},
             {{left, top, z}, normal, tangent, {0.0f, 0.0f}}},
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

    MaterialHandle Material(const glm::vec3& color) {
        MaterialDesc desc;
        desc.baseColorFactor = glm::vec4(color, 1.0f);
        const core::Result<MaterialHandle> material = m_pass->CreateMaterial(desc);
        EXPECT_TRUE(material.has_value());
        return material.value_or(MaterialHandle{});
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
                            EXPECT_TRUE(m_pass->Record(commands, view, 1.0f).has_value());
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
    const MeshHandle mesh = Mesh(MakeQuad(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f));
    Render({TestCamera(), {{mesh, glm::mat4(1.0f), Material({1.0f, 0.0f, 0.0f})}}});

    const glm::ivec3 center = Pixel(32, 32);
    EXPECT_GT(center.r, 0);
    EXPECT_EQ(center.g, 0);
    EXPECT_EQ(center.b, 0);
    EXPECT_TRUE(IsBackground(0, 0));
}

TEST_F(MeshPassTest, NothingIsDrawnWithoutACamera) {
    const MeshHandle mesh = Mesh(MakeQuad(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f));
    Render({std::nullopt, {{mesh, glm::mat4(1.0f)}}});
    EXPECT_TRUE(IsBackground(32, 32));
}

TEST_F(MeshPassTest, PositiveYIsAtTheTopOfTheImage) {
    const MeshHandle mesh = Mesh(MakeQuad(-1.0f, 1.0f, 0.5f, 1.5f, 0.0f));
    Render({TestCamera(), {{mesh, glm::mat4(1.0f)}}});
    EXPECT_FALSE(IsBackground(32, 20));
    EXPECT_TRUE(IsBackground(32, 44));
}

TEST_F(MeshPassTest, PositiveXIsOnTheRightOfTheImage) {
    const MeshHandle mesh = Mesh(MakeQuad(0.5f, 1.5f, -1.0f, 1.0f, 0.0f));
    Render({TestCamera(), {{mesh, glm::mat4(1.0f)}}});
    EXPECT_FALSE(IsBackground(44, 32));
    EXPECT_TRUE(IsBackground(20, 32));
}

TEST_F(MeshPassTest, ClockwiseTrianglesAreCulled) {
    const MeshHandle mesh = Mesh(Reversed(MakeQuad(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f)));
    Render({TestCamera(), {{mesh, glm::mat4(1.0f)}}});
    EXPECT_TRUE(IsBackground(32, 32));
}

TEST_F(MeshPassTest, NearerSurfaceWinsWhicheverIsDrawnFirst) {
    const MeshHandle nearQuad = Mesh(MakeQuad(-1.0f, 1.0f, -1.0f, 1.0f, 1.0f));
    const MeshHandle farQuad = Mesh(MakeQuad(-1.0f, 1.0f, -1.0f, 1.0f, -1.0f));

    const MaterialHandle red = Material({1.0f, 0.0f, 0.0f});
    const MaterialHandle green = Material({0.0f, 1.0f, 0.0f});

    Render({TestCamera(), {{nearQuad, glm::mat4(1.0f), red}, {farQuad, glm::mat4(1.0f), green}}});
    EXPECT_GT(Pixel(32, 32).r, 0);
    EXPECT_EQ(Pixel(32, 32).g, 0);

    Render({TestCamera(), {{farQuad, glm::mat4(1.0f), green}, {nearQuad, glm::mat4(1.0f), red}}});
    EXPECT_GT(Pixel(32, 32).r, 0);
    EXPECT_EQ(Pixel(32, 32).g, 0);
}

TEST_F(MeshPassTest, WorldMatrixMovesTheMesh) {
    const MeshHandle mesh = Mesh(MakeQuad(-0.5f, 0.5f, -0.5f, 0.5f, 0.0f));
    Render({TestCamera(), {{mesh, glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f))}}});
    EXPECT_TRUE(IsBackground(32, 32));
    EXPECT_FALSE(IsBackground(44, 32));
}

TEST_F(MeshPassTest, NormalsFollowTheWorldRotation) {
    // Lit from above: the same quad turned to face up receives more light.
    const MeshHandle mesh = Mesh(MakeQuad(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f));
    Render({TestCamera(), {{mesh, glm::mat4(1.0f)}}});
    const int facingCamera = Pixel(32, 32).r;

    const glm::mat4 tilted = glm::rotate(glm::mat4(1.0f), glm::radians(-60.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    Render({TestCamera(), {{mesh, tilted}}});
    EXPECT_GT(Pixel(32, 32).r, facingCamera);
}

TEST_F(MeshPassTest, SpotLightOnlyLightsItsCone) {
    const MeshHandle mesh = Mesh(MakeQuad(-4.0f, 4.0f, -4.0f, 4.0f, 0.0f));
    const MaterialHandle white = Material({1.0f, 1.0f, 1.0f});
    SceneView view{TestCamera(), {{mesh, glm::mat4(1.0f), white}}};
    view.atmosphere = {glm::vec3(0.0f), 10.0f}; // Fog hides everything.
    Render(view);
    EXPECT_EQ(Pixel(32, 32), glm::ivec3(0));

    view.spotLight = renderer::SpotLight{.position = {0.0f, 0.0f, 5.0f},
                                         .direction = {0.0f, 0.0f, -1.0f},
                                         .color = glm::vec3(1.0f),
                                         .range = 20.0f,
                                         .innerAngle = glm::radians(5.0f),
                                         .outerAngle = glm::radians(10.0f)};
    Render(view);
    EXPECT_GT(Pixel(32, 32).r, 200);
    EXPECT_EQ(Pixel(4, 32), glm::ivec3(0));
    EXPECT_EQ(Pixel(32, 4), glm::ivec3(0));
}

TEST_F(MeshPassTest, DestroyedMeshIsSkipped) {
    const MeshHandle mesh = Mesh(MakeQuad(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f));
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
float4 VSMain(float3 position : POSITION) : SV_Position {
    const float4x4 viewProjection = g_frames[g_push.frameBuffer][0].viewProjection;
    const float4x4 world = g_instances[g_push.instanceBuffer][g_push.instanceIndex].world;
    return mul(viewProjection, mul(world, float4(position, 1.0)));
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

    const MeshHandle mesh = Mesh(MakeQuad(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f));
    Render({TestCamera(), {{mesh, glm::mat4(1.0f)}}});
    EXPECT_EQ(Pixel(32, 32), glm::ivec3(0, 255, 0));
    std::filesystem::remove_all(directory);
}

TEST_F(MeshPassTest, BaseColorTextureIsSampledWithTheMeshUvs) {
    // Left half green, right half blue.
    const std::array<std::uint32_t, 4> pixels{0xFF00FF00, 0xFF00FF00, 0xFFFF0000, 0xFFFF0000};
    resources::ImageData image;
    image.width = 4;
    image.height = 1;
    image.format = resources::PixelFormat::Rgba8Unorm;
    image.pixels.resize(sizeof(pixels));
    std::memcpy(image.pixels.data(), pixels.data(), sizeof(pixels));
    const core::Result<rhi::TextureHandle> texture = renderer::CreateTexture(Device(), image, "Test");
    ASSERT_TRUE(texture.has_value()) << core::Format(texture.error());

    MaterialDesc desc;
    desc.baseColorTexture = *texture;
    const core::Result<MaterialHandle> material = m_pass->CreateMaterial(desc);
    ASSERT_TRUE(material.has_value());
    const MeshHandle mesh = Mesh(MakeQuad(-2.0f, 2.0f, -2.0f, 2.0f, 0.0f));

    Render({TestCamera(), {{mesh, glm::mat4(1.0f), *material}}});
    EXPECT_GT(Pixel(21, 32).g, 0);
    EXPECT_EQ(Pixel(21, 32).b, 0);
    EXPECT_GT(Pixel(43, 32).b, 0);
    EXPECT_EQ(Pixel(43, 32).g, 0);
    Device().DestroyTexture(*texture);
}

TEST_F(MeshPassTest, AlphaBelowTheCutoffIsDiscarded) {
    MaterialDesc desc;
    desc.baseColorFactor = glm::vec4(1.0f, 1.0f, 1.0f, 0.25f);
    desc.alphaCutoff = 0.5f;
    const core::Result<MaterialHandle> material = m_pass->CreateMaterial(desc);
    ASSERT_TRUE(material.has_value());
    const MeshHandle mesh = Mesh(MakeQuad(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f));
    Render({TestCamera(), {{mesh, glm::mat4(1.0f), *material}}});
    EXPECT_TRUE(IsBackground(32, 32));
}

TEST_F(MeshPassTest, DoubleSidedMaterialDrawsTheBackOfTriangles) {
    MaterialDesc desc;
    desc.doubleSided = true;
    const core::Result<MaterialHandle> material = m_pass->CreateMaterial(desc);
    ASSERT_TRUE(material.has_value());
    const MeshHandle mesh = Mesh(Reversed(MakeQuad(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f)));
    Render({TestCamera(), {{mesh, glm::mat4(1.0f), *material}}});
    EXPECT_FALSE(IsBackground(32, 32));
}

TEST_F(MeshPassTest, DestroyedMaterialFallsBackToTheDefaultOne) {
    const MaterialHandle material = Material({1.0f, 0.0f, 0.0f});
    m_pass->DestroyMaterial(material);
    const MeshHandle mesh = Mesh(MakeQuad(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f));
    Render({TestCamera(), {{mesh, glm::mat4(1.0f), material}}});
    EXPECT_GT(Pixel(32, 32).g, 0); // White, not red.
}

TEST_F(MeshPassTest, MaterialTableGrowsAndKeepsItsContent) {
    const MaterialHandle first = Material({1.0f, 0.0f, 0.0f});
    for (int index = 0; index < 1000; ++index) {
        Material({0.0f, 1.0f, 0.0f});
    }
    const MeshHandle mesh = Mesh(MakeQuad(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f));
    Render({TestCamera(), {{mesh, glm::mat4(1.0f), first}}});
    EXPECT_GT(Pixel(32, 32).r, 0);
    EXPECT_EQ(Pixel(32, 32).g, 0);
}

TEST_F(MeshPassTest, InstanceBufferGrowsWithTheNumberOfDraws) {
    const MeshHandle mesh = Mesh(MakeQuad(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f));
    SceneView view{TestCamera(), {}};
    view.draws.assign(5000, {mesh, glm::mat4(1.0f)});
    Render(view);
    EXPECT_FALSE(IsBackground(32, 32));
}
