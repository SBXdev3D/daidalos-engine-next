// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/renderer/Material.hpp>
#include <daidalos/resources/ImageData.hpp>

#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/packing.hpp>

#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <utility>
#include <vector>

#include "Denoiser.hpp"
#include "DirectLighting.hpp"
#include "GpuFixture.hpp"
#include "MeshPass.hpp"
#include "ParticlePass.hpp"
#include "RayTracingScene.hpp"
#include "RenderGraph.hpp"
#include "ShaderCompiler.hpp"
#include "TemporalResolvePass.hpp"

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
        core::Result<renderer::RayTracingScene> scene = renderer::RayTracingScene::Create(Device(), kColorFormat);
        ASSERT_TRUE(scene.has_value()) << core::Format(scene.error());
        m_scene.emplace(std::move(*scene));
        core::Result<renderer::DirectLighting> lighting = renderer::DirectLighting::Create(Device());
        ASSERT_TRUE(lighting.has_value()) << core::Format(lighting.error());
        ASSERT_TRUE(lighting->Resize(kExtent).has_value());
        m_lighting.emplace(std::move(*lighting));

        const core::Result<rhi::TextureHandle> color = Device().CreateTexture(
            {.extent = kExtent,
             .format = kColorFormat,
             .usage = rhi::TextureUsage::ColorTarget | rhi::TextureUsage::TransferSrc | rhi::TextureUsage::Sampled,
             .name = "Color"});
        ASSERT_TRUE(color.has_value());
        m_color = *color;
        const core::Result<rhi::TextureHandle> depth =
            Device().CreateTexture({.extent = kExtent,
                                    .format = kDepthFormat,
                                    .usage = rhi::TextureUsage::DepthTarget | rhi::TextureUsage::Sampled,
                                    .name = "Depth"});
        ASSERT_TRUE(depth.has_value());
        m_depth = *depth;
        const core::Result<rhi::TextureHandle> normals =
            Device().CreateTexture({.extent = kExtent,
                                    .format = MeshPass::kNormalFormat,
                                    .usage = rhi::TextureUsage::ColorTarget | rhi::TextureUsage::Sampled,
                                    .name = "Normals"});
        ASSERT_TRUE(normals.has_value());
        m_normals = *normals;
        const core::Result<rhi::TextureHandle> motion = Device().CreateTexture(
            {.extent = kExtent,
             .format = MeshPass::kMotionFormat,
             .usage = rhi::TextureUsage::ColorTarget | rhi::TextureUsage::TransferSrc | rhi::TextureUsage::Sampled,
             .name = "Motion"});
        ASSERT_TRUE(motion.has_value());
        m_motion = *motion;
        const core::Result<rhi::BufferHandle> motionReadback =
            Device().CreateBuffer({.size = std::uint64_t{kExtent.width} * kExtent.height * 4,
                                   .usage = rhi::BufferUsage::TransferDst,
                                   .memory = rhi::MemoryLocation::GpuToCpu,
                                   .name = "Motion readback"});
        ASSERT_TRUE(motionReadback.has_value());
        m_motionReadback = *motionReadback;
        const core::Result<rhi::BufferHandle> readback =
            Device().CreateBuffer({.size = std::uint64_t{kExtent.width} * kExtent.height * 4,
                                   .usage = rhi::BufferUsage::TransferDst,
                                   .memory = rhi::MemoryLocation::GpuToCpu,
                                   .name = "Readback"});
        ASSERT_TRUE(readback.has_value());
        m_readback = *readback;
        const core::Result<renderer::ShadingTargets> shading = m_pass->CreateShadingTargets(kExtent);
        ASSERT_TRUE(shading.has_value()) << core::Format(shading.error());
        m_shading = *shading;
    }

    void TearDown() override {
        if (m_pass.has_value()) {
            m_pass->DestroyShadingTargets(m_shading);
            m_lighting.reset();
            m_scene.reset();
            m_pass.reset();
            Device().DestroyTexture(m_color);
            Device().DestroyTexture(m_depth);
            Device().DestroyTexture(m_normals);
            Device().DestroyTexture(m_motion);
            Device().DestroyBuffer(m_readback);
            Device().DestroyBuffer(m_motionReadback);
        }
        GpuTest::TearDown();
    }

    MeshHandle Mesh(const MeshData& data) {
        const core::Result<MeshHandle> mesh = m_pass->CreateMesh(data, "Test");
        EXPECT_TRUE(mesh.has_value());
        return mesh.value_or(MeshHandle{});
    }

    // A plain dielectric: glTF's default is a metal, which reflects no diffuse light.
    MaterialHandle Material(const glm::vec3& color) {
        MaterialDesc desc;
        desc.baseColorFactor = glm::vec4(color, 1.0f);
        desc.metallicFactor = 0.0f;
        const core::Result<MaterialHandle> material = m_pass->CreateMaterial(desc);
        EXPECT_TRUE(material.has_value());
        return material.value_or(MaterialHandle{});
    }

    // Renders the view, each pixel sampled at jitter from its center (in pixels), then puts its light
    // together, through the denoiser if there is one (ADR-0039). Each frame moves the noise of soft shadows.
    // A view without lights is lit by a white light from behind the camera: what faces the camera shows its
    // color, as a white ambient light showed it before global illumination (ADR-0040).
    void Render(const SceneView& scene, glm::vec2 jitter = glm::vec2(0.0f), renderer::Denoiser* denoiser = nullptr) {
        SceneView view = scene;
        if (view.camera.has_value() && view.lights.empty()) {
            view.lights.push_back({.type = renderer::LightType::Directional,
                                   .direction = -glm::vec3(glm::inverse(view.camera->view)[2]),
                                   // Pi lux: a white matte surface facing it reflects 1 nit (ADR-0043).
                                   .color = glm::vec3(glm::pi<float>())});
        }
        ASSERT_TRUE(
            Device()
                .Submit([&](rhi::CommandList& commands) {
                    // Every light casts shadows by rays: the frame's structure comes first.
                    const core::Result<std::uint32_t> structure = m_scene->Reserve(0, view.draws.size());
                    ASSERT_TRUE(structure.has_value());
                    EXPECT_TRUE(m_pass
                                    ->Prepare(view, {.extent = m_renderExtent,
                                                     .targetExtent = kExtent,
                                                     .accelerationStructure = *structure,
                                                     .jitter = jitter,
                                                     .frameIndex = m_frameIndex++})
                                    .has_value());
                    renderer::RenderGraph graph;
                    m_pass->AddSkinning(graph);
                    rhi::AccelerationStructureHandle traced;
                    if (view.camera.has_value()) {
                        ASSERT_TRUE(m_scene->Prepare(m_pass->TracedDraws()).has_value());
                        m_scene->AddBuild(graph);
                        traced = m_scene->Structure();
                        m_pass->AddEmissiveLights(graph);
                    }
                    const rhi::BufferUse skinned{m_pass->SkinnedVertices(), rhi::Access::VertexInputRead};
                    graph.AddPass(
                        {.name = "G-buffer",
                         .rendering =
                             MeshPass::GBufferRendering(m_shading, m_normals, m_motion, m_depth, m_renderExtent),
                         .buffers = {skinned, {m_pass->PreviousSkinnedPositions(), rhi::Access::VertexShaderRead}}},
                        [&](rhi::CommandList& list) { m_pass->RecordGBuffer(list); });
                    if (view.camera.has_value()) {
                        m_lighting->AddTo(graph,
                                          {.extent = m_renderExtent,
                                           .frameBuffer = m_pass->FrameDataIndex(),
                                           .instanceBuffer = m_pass->InstanceDataIndex(),
                                           .materialBuffer = m_pass->MaterialDataIndex(),
                                           .structure = traced,
                                           .skinnedVertices = m_pass->SkinnedVertices(),
                                           .emissiveTriangles = m_pass->EmissiveTriangles(),
                                           .depth = m_depth,
                                           .normals = m_normals,
                                           .motion = m_motion,
                                           .baseColor = m_shading.baseColor,
                                           .emission = m_shading.emission,
                                           .diffuse = m_shading.diffuse,
                                           .specular = m_shading.specular,
                                           .lightMap = m_pass->LightMapIndex(),
                                           .previousLightCount = m_pass->PreviousLightCount(),
                                           .frameIndex = m_frameIndex},
                                          {});
                    }
                    if (denoiser != nullptr && view.camera.has_value()) {
                        denoiser->AddTo(graph, {.extent = m_renderExtent,
                                                .frameBuffer = m_pass->FrameDataIndex(),
                                                .diffuse = m_shading.diffuse,
                                                .specular = m_shading.specular,
                                                .depth = m_depth,
                                                .normals = m_normals,
                                                .motion = m_motion});
                    }
                    const bool denoised = denoiser != nullptr && view.camera.has_value();
                    if (view.camera.has_value()) {
                        m_pass->AddComposite(graph, m_color,
                                             {.shading = &m_shading,
                                              .diffuse = denoised ? denoiser->Diffuse() : m_shading.diffuse,
                                              .specular = denoised ? denoiser->Specular() : m_shading.specular,
                                              .depth = m_depth});
                    } else {
                        // Nothing to put together: the frame is the background.
                        graph.AddPass(
                            {.name = "Clear",
                             .rendering = rhi::RenderingDesc{.colorTarget = m_color, .clearColor = kBackground}},
                            [](rhi::CommandList& /*list*/) {});
                    }
                    graph.Execute(commands);
                    commands.Transition(m_color, rhi::TextureState::TransferSrc);
                    commands.CopyTextureToBuffer(m_color, m_readback);
                    commands.Transition(m_motion, rhi::TextureState::TransferSrc);
                    commands.CopyTextureToBuffer(m_motion, m_motionReadback);
                })
                .has_value());
    }

    // The motion vector of a pixel, counted from the top-left corner: where its surface was in the previous
    // frame minus where it is, in pixels.
    [[nodiscard]] glm::vec2 Motion(std::uint32_t x, std::uint32_t y) {
        std::array<std::uint16_t, 2> halves{};
        const std::size_t offset = (std::size_t{y} * kExtent.width + x) * sizeof(halves);
        std::memcpy(halves.data(), Device().Mapped(m_motionReadback).data() + offset, sizeof(halves));
        return glm::vec2(glm::unpackHalf1x16(halves[0]) * static_cast<float>(kExtent.width),
                         glm::unpackHalf1x16(halves[1]) * static_cast<float>(kExtent.height));
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
    std::optional<renderer::RayTracingScene> m_scene;
    std::optional<renderer::DirectLighting> m_lighting;
    renderer::ShadingTargets m_shading;
    /// The frame draws this top-left corner of its targets, which are kExtent (ADR-0049).
    rhi::Extent m_renderExtent = kExtent;
    std::uint32_t m_frameIndex = 0;
    rhi::TextureHandle m_color;
    rhi::TextureHandle m_depth;
    rhi::TextureHandle m_normals;
    rhi::TextureHandle m_motion;
    rhi::BufferHandle m_readback;
    rhi::BufferHandle m_motionReadback;
};

} // namespace

TEST_F(MeshPassTest, DrawsAMeshInFrontOfTheCamera) {
    const MeshHandle mesh = Mesh(MakeQuad(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f));
    Render({TestCamera(), {{mesh, glm::mat4(1.0f), Material({1.0f, 0.0f, 0.0f})}}});

    const glm::ivec3 center = Pixel(32, 32);
    EXPECT_GT(center.r, 0);
    EXPECT_LT(center.g, 16); // Only the white reflection of a dielectric.
    EXPECT_LT(center.b, 16); // Only the white reflection of a dielectric.
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
    EXPECT_LT(Pixel(32, 32).g, 16); // Only the white reflection of a dielectric.

    Render({TestCamera(), {{farQuad, glm::mat4(1.0f), green}, {nearQuad, glm::mat4(1.0f), red}}});
    EXPECT_GT(Pixel(32, 32).r, 0);
    EXPECT_LT(Pixel(32, 32).g, 16); // Only the white reflection of a dielectric.
}

TEST_F(MeshPassTest, WorldMatrixMovesTheMesh) {
    const MeshHandle mesh = Mesh(MakeQuad(-0.5f, 0.5f, -0.5f, 0.5f, 0.0f));
    Render({TestCamera(), {{mesh, glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f))}}});
    EXPECT_TRUE(IsBackground(32, 32));
    EXPECT_FALSE(IsBackground(44, 32));
}

// In the dark, with one light.
SceneView LitView(const CameraView& camera, renderer::DrawItem draw, const renderer::Light& light) {
    SceneView view{camera, {draw}};
    view.lights.push_back(light);
    return view;
}

TEST_F(MeshPassTest, WhereNoLightReachesOnlyEmissionShows) {
    // No light comes from nowhere (ADR-0040): a light that reaches nothing leaves the surface black, unless
    // it glows.
    const MeshHandle mesh = Mesh(MakeQuad(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f));
    const renderer::Light faraway{.position = {0.0f, 0.0f, 100.0f}, .range = 1.0f};
    Render(LitView(TestCamera(), {mesh, glm::mat4(1.0f), Material({0.5f, 0.25f, 0.0f})}, faraway));
    EXPECT_EQ(Pixel(32, 32), glm::ivec3(0));

    MaterialDesc glowing;
    glowing.emissiveFactor = glm::vec3(0.5f, 0.25f, 0.0f);
    const core::Result<MaterialHandle> material = m_pass->CreateMaterial(glowing);
    ASSERT_TRUE(material.has_value());
    Render(LitView(TestCamera(), {mesh, glm::mat4(1.0f), *material}, faraway));
    EXPECT_NEAR(Pixel(32, 32).r, 128, 1);
    EXPECT_NEAR(Pixel(32, 32).g, 64, 1);
}

TEST_F(MeshPassTest, NormalsFollowTheWorldRotation) {
    // Lit from above: the same quad turned to face up receives more light.
    const MeshHandle mesh = Mesh(MakeQuad(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f));
    const MaterialHandle white = Material({1.0f, 1.0f, 1.0f});
    const renderer::Light above{.position = {0.0f, 5.0f, 2.0f}, .color = glm::vec3(90.0f), .range = 50.0f};
    Render(LitView(TestCamera(), {mesh, glm::mat4(1.0f), white}, above));
    const int facingCamera = Pixel(32, 32).r;

    const glm::mat4 tilted = glm::rotate(glm::mat4(1.0f), glm::radians(-60.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    Render(LitView(TestCamera(), {mesh, tilted, white}, above));
    EXPECT_GT(Pixel(32, 32).r, facingCamera);
}

TEST_F(MeshPassTest, SpotLightOnlyLightsItsCone) {
    const MeshHandle mesh = Mesh(MakeQuad(-4.0f, 4.0f, -4.0f, 4.0f, 0.0f));
    const MaterialHandle white = Material({1.0f, 1.0f, 1.0f});
    // 25 pi candela, 5 m away: pi lux, which a white matte surface facing the light reflects as 1 nit
    // (ADR-0043). The fixture's exposure is 1.
    Render(LitView(TestCamera(), {mesh, glm::mat4(1.0f), white},
                   {.type = renderer::LightType::Spot,
                    .position = {0.0f, 0.0f, 5.0f},
                    .direction = {0.0f, 0.0f, -1.0f},
                    .color = glm::vec3(25.0f * glm::pi<float>()),
                    .range = 20.0f,
                    .innerAngle = glm::radians(5.0f),
                    .outerAngle = glm::radians(10.0f)}));
    EXPECT_GT(Pixel(32, 32).r, 230);
    EXPECT_EQ(Pixel(4, 32), glm::ivec3(0));
    EXPECT_EQ(Pixel(32, 4), glm::ivec3(0));
}

TEST_F(MeshPassTest, PointLightFadesToZeroAtItsRange) {
    const MeshHandle mesh = Mesh(MakeQuad(-4.0f, 4.0f, -4.0f, 4.0f, 0.0f));
    const MaterialHandle white = Material({1.0f, 1.0f, 1.0f});
    // 1 m in front of the quad: pi candela is pi lux there, white; nothing beyond 2.5 m.
    Render(LitView(TestCamera(), {mesh, glm::mat4(1.0f), white},
                   {.position = {0.0f, 0.0f, 1.0f}, .color = glm::vec3(glm::pi<float>()), .range = 2.5f}));
    EXPECT_GT(Pixel(32, 32).r, 200);
    EXPECT_EQ(Pixel(1, 32), glm::ivec3(0)); // About 3 m from the light.
}

TEST_F(MeshPassTest, LightFallsOffWithTheSquareOfTheDistance) {
    // Physical units (ADR-0043): 2 pi candela, 2 m in front of a white matte quad, make pi / 2 lux, which
    // it reflects as half a nit, less the little that its specular lobe takes; twice as far, four times
    // less. The range is far, so that its window changes almost nothing.
    const MeshHandle mesh = Mesh(MakeQuad(-4.0f, 4.0f, -4.0f, 4.0f, 0.0f));
    MaterialDesc matte;
    matte.metallicFactor = 0.0f;
    matte.roughnessFactor = 1.0f;
    const core::Result<MaterialHandle> material = m_pass->CreateMaterial(matte);
    ASSERT_TRUE(material.has_value());
    const auto centerAt = [&](float distance) {
        Render(LitView(
            TestCamera(), {mesh, glm::mat4(1.0f), *material},
            {.position = {0.0f, 0.0f, distance}, .color = glm::vec3(2.0f * glm::pi<float>()), .range = 100.0f}));
        return static_cast<float>(Pixel(32, 32).r);
    };
    const float near = centerAt(2.0f);
    const float far = centerAt(4.0f);
    EXPECT_NEAR(near, 0.5f * 255.0f, 8.0f);
    EXPECT_NEAR(near / far, 4.0f, 0.3f);
}

TEST_F(MeshPassTest, NormalMapTiltsTheSurface) {
    // A flat quad, lit from its right side at a grazing angle. Its normal map leans every normal to the
    // right, along the tangent, so more light is caught; leaning left catches less.
    const auto normalMapped = [this](std::uint8_t red) {
        const std::array<std::uint8_t, 4> pixel{red, 128, 255, 255};
        resources::ImageData image;
        image.width = 1;
        image.height = 1;
        image.format = resources::PixelFormat::Rgba8Unorm;
        image.pixels.resize(pixel.size());
        std::memcpy(image.pixels.data(), pixel.data(), pixel.size());
        const core::Result<rhi::TextureHandle> texture = renderer::CreateTexture(Device(), image, "Normal");
        EXPECT_TRUE(texture.has_value());
        MaterialDesc desc;
        desc.metallicFactor = 0.0f;
        desc.normalTexture = texture.value_or(rhi::TextureHandle{});
        const core::Result<MaterialHandle> material = m_pass->CreateMaterial(desc);
        EXPECT_TRUE(material.has_value());
        return std::pair{texture.value_or(rhi::TextureHandle{}), material.value_or(MaterialHandle{})};
    };
    const MeshHandle mesh = Mesh(MakeQuad(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f));
    const renderer::Light right{.position = {6.0f, 0.0f, 1.0f}, .color = glm::vec3(116.0f), .range = 50.0f};

    Render(LitView(TestCamera(), {mesh, glm::mat4(1.0f), Material({1.0f, 1.0f, 1.0f})}, right));
    const int flat = Pixel(32, 32).r;
    const auto [rightTexture, rightMaterial] = normalMapped(218); // x = 0.7
    Render(LitView(TestCamera(), {mesh, glm::mat4(1.0f), rightMaterial}, right));
    const int leaningRight = Pixel(32, 32).r;
    const auto [leftTexture, leftMaterial] = normalMapped(38); // x = -0.7
    Render(LitView(TestCamera(), {mesh, glm::mat4(1.0f), leftMaterial}, right));
    const int leaningLeft = Pixel(32, 32).r;

    EXPECT_GT(leaningRight, flat + 50);
    EXPECT_LT(leaningLeft, flat);
    Device().DestroyTexture(rightTexture);
    Device().DestroyTexture(leftTexture);
}

namespace {

// A spot light from the right of the floor in the shadow tests, 5 m in front of it.
renderer::Light ShadowTestLight() {
    return {.type = renderer::LightType::Spot,
            .position = {3.0f, 0.0f, 5.0f},
            .direction = glm::normalize(glm::vec3(-3.0f, 0.0f, -5.0f)),
            .color = glm::vec3(107.0f), // About as bright as 1 nit on the floor, facing it, 5.8 m away.
            .range = 20.0f,
            .innerAngle = glm::radians(40.0f),
            .outerAngle = glm::radians(45.0f)};
}

} // namespace

TEST_F(MeshPassTest, SpotLightCastsTheShadowOfWhatStandsInItsWay) {
    // A floor facing the camera, lit from the right by a spot, and a small panel 1 m in front of it. The
    // panel's shadow falls left of the panel, from x = -1.4 to -0.1, where the camera sees the floor.
    const MeshHandle floor = Mesh(MakeQuad(-4.0f, 4.0f, -4.0f, 4.0f, 0.0f));
    const MeshHandle panel = Mesh(MakeQuad(-0.5f, 0.5f, -0.5f, 0.5f, 1.0f));
    const MaterialHandle white = Material({1.0f, 1.0f, 1.0f});
    bool panelCastsShadows = true;
    const auto render = [&](const renderer::Light& light) {
        SceneView view = LitView(TestCamera(), {floor, glm::mat4(1.0f), white}, light);
        view.draws.push_back({.mesh = panel, .material = white, .castsShadows = panelCastsShadows});
        Render(view);
    };

    render(ShadowTestLight());
    EXPECT_LT(Pixel(20, 32).r, 10);  // In the shadow.
    EXPECT_GT(Pixel(48, 32).r, 100); // The floor on the light's side stays lit.
    EXPECT_GT(Pixel(32, 32).r, 100); // So does the panel itself.

    // A draw that does not cast shadows lets the light through.
    panelCastsShadows = false;
    render(ShadowTestLight());
    EXPECT_GT(Pixel(20, 32).r, 100);
}

TEST_F(MeshPassTest, PointLightCastsShadowsToo) {
    const MeshHandle floor = Mesh(MakeQuad(-4.0f, 4.0f, -4.0f, 4.0f, 0.0f));
    const MeshHandle panel = Mesh(MakeQuad(-0.5f, 0.5f, -0.5f, 0.5f, 1.0f));
    const MaterialHandle white = Material({1.0f, 1.0f, 1.0f});
    renderer::Light light = ShadowTestLight();
    light.type = renderer::LightType::Point;
    SceneView view = LitView(TestCamera(), {floor, glm::mat4(1.0f), white}, light);
    view.draws.push_back({.mesh = panel, .material = white});
    Render(view);
    EXPECT_LT(Pixel(20, 32).r, 10);
    EXPECT_GT(Pixel(48, 32).r, 100);
}

TEST_F(MeshPassTest, DirectionalLightLightsEverywhereAtTheSameStrength) {
    // Straight at the floor, from as far as it can be: the whole floor gets the light's illuminance, wherever
    // the light's position says, and whatever its range. Half of pi lux is half a nit off white (ADR-0043).
    const MeshHandle floor = Mesh(MakeQuad(-4.0f, 4.0f, -4.0f, 4.0f, 0.0f));
    const MaterialHandle white = Material({1.0f, 1.0f, 1.0f});
    Render(LitView(TestCamera(), {floor, glm::mat4(1.0f), white},
                   {.type = renderer::LightType::Directional,
                    .position = {0.0f, 0.0f, -100.0f},
                    .direction = {0.0f, 0.0f, -1.0f},
                    .color = glm::vec3(0.5f * glm::pi<float>()),
                    .range = 0.1f}));
    EXPECT_NEAR(Pixel(32, 32).r, Pixel(4, 4).r, 2);
    // 0.5 of light, in the 8-bit target, less what the dielectric reflects instead of diffusing.
    EXPECT_GE(Pixel(32, 32).r, 118);
    EXPECT_LE(Pixel(32, 32).r, 140);
}

TEST_F(MeshPassTest, DirectionalLightCastsParallelShadows) {
    // From the right, at 31 degrees like the spot of the shadow tests: the panel's shadow falls on the floor
    // left of it. Nothing near the light's far-away position can stop it.
    const MeshHandle floor = Mesh(MakeQuad(-4.0f, 4.0f, -4.0f, 4.0f, 0.0f));
    const MeshHandle panel = Mesh(MakeQuad(-0.5f, 0.5f, -0.5f, 0.5f, 1.0f));
    const MaterialHandle white = Material({1.0f, 1.0f, 1.0f});
    SceneView view = LitView(TestCamera(), {floor, glm::mat4(1.0f), white},
                             {.type = renderer::LightType::Directional,
                              .direction = glm::normalize(glm::vec3(-3.0f, 0.0f, -5.0f)),
                              .color = glm::vec3(glm::pi<float>())});
    view.draws.push_back({.mesh = panel, .material = white});
    Render(view);
    // The shadow runs from x = -1.1 to -0.1 on the floor.
    EXPECT_LT(Pixel(22, 32).r, 10);
    EXPECT_GT(Pixel(48, 32).r, 100);
    EXPECT_GT(Pixel(32, 32).r, 100);
}

TEST_F(MeshPassTest, AlphaTestedSurfaceLetsLightThroughWhereItIsCut) {
    // The panel's alpha is below its cutoff everywhere: the G-buffer does not hold it, and shadow rays go
    // through it as well.
    const MeshHandle floor = Mesh(MakeQuad(-4.0f, 4.0f, -4.0f, 4.0f, 0.0f));
    const MeshHandle panel = Mesh(MakeQuad(-0.5f, 0.5f, -0.5f, 0.5f, 1.0f));
    const MaterialHandle white = Material({1.0f, 1.0f, 1.0f});
    MaterialDesc cut;
    cut.baseColorFactor = glm::vec4(1.0f, 1.0f, 1.0f, 0.2f);
    cut.metallicFactor = 0.0f;
    cut.alphaCutoff = 0.5f;
    SceneView view = LitView(TestCamera(), {floor, glm::mat4(1.0f), white}, ShadowTestLight());
    const core::Result<MaterialHandle> cutMaterial = m_pass->CreateMaterial(cut);
    ASSERT_TRUE(cutMaterial.has_value());
    view.draws.push_back({.mesh = panel, .material = *cutMaterial});
    Render(view);
    EXPECT_GT(Pixel(20, 32).r, 100);
}

namespace {

// A sky of 4 by 4 texels per face, in the order +X, -X, +Y, -Y, +Z, -Z, each of one color, as RGBA bytes.
// Filtering blends faces near their edges: their middles keep their own colors.
core::Result<rhi::TextureHandle> MakeSky(rhi::Device& device, const std::array<std::uint32_t, 6>& colors) {
    std::array<resources::ImageData, 6> faces;
    for (std::size_t face = 0; face < faces.size(); ++face) {
        faces[face].width = 4;
        faces[face].height = 4;
        faces[face].format = resources::PixelFormat::Rgba8Unorm;
        const std::vector<std::uint32_t> texels(16, colors[face]);
        const auto bytes = std::as_bytes(std::span(texels));
        faces[face].pixels.assign(bytes.begin(), bytes.end());
    }
    return renderer::CreateCubeTexture(device, faces, "Test sky");
}

} // namespace

TEST_F(MeshPassTest, SkyShowsWhereNothingIsDrawn) {
    // The camera looks down -Z: it sees the face that the legacy engine called front, the cube's +Z
    // (ADR-0036). A quad covers the middle of the view, where the sky must not show.
    const core::Result<rhi::TextureHandle> sky =
        MakeSky(Device(), {0xFF0000FF, 0xFF0000FF, 0xFF0000FF, 0xFF0000FF, 0xFFFF0000, 0xFF00FF00});
    ASSERT_TRUE(sky.has_value()) << core::Format(sky.error());
    const MeshHandle quad = Mesh(MakeQuad(0.8f, 1.8f, -0.5f, 0.5f, 0.0f));
    SceneView view{TestCamera(), {{quad, glm::mat4(1.0f), Material({1.0f, 1.0f, 1.0f})}}};
    view.atmosphere.sky = *sky;
    view.atmosphere.skyIntensity = 0.5f;
    Render(view);
    // Blue at half intensity straight ahead, in the middle of the +Z face; not the sky on the quad.
    const glm::ivec3 ahead = Pixel(32, 32);
    EXPECT_EQ(ahead.r, 0);
    EXPECT_EQ(ahead.g, 0);
    EXPECT_NEAR(ahead.b, 128, 1);
    EXPECT_NE(Pixel(46, 32), ahead); // The quad, 1.3 m right of the view's middle.

    // Turned half a turn, the sky shows ahead what was behind: the back face, green. A quarter turn
    // counter-clockwise brings what was on the right, red, ahead.
    view.atmosphere.skyYaw = glm::pi<float>();
    Render(view);
    EXPECT_EQ(Pixel(32, 32).r, 0);
    EXPECT_NEAR(Pixel(32, 32).g, 128, 1);
    EXPECT_EQ(Pixel(32, 32).b, 0);
    view.atmosphere.skyYaw = glm::half_pi<float>();
    Render(view);
    EXPECT_NEAR(Pixel(32, 32).r, 128, 1);
    EXPECT_EQ(Pixel(32, 32).b, 0);
    Device().DestroyTexture(*sky);
}

TEST_F(MeshPassTest, NothingIsDrawnBeyondTheViewDistance) {
    // A quad 5 m in front of the camera: drawn within 6 m, not within 4 m.
    const MeshHandle mesh = Mesh(MakeQuad(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f));
    const SceneView view{TestCamera(), {{mesh, glm::mat4(1.0f), Material({1.0f, 0.0f, 0.0f})}}};
    m_pass->SetViewDistance(6.0f);
    Render(view);
    EXPECT_FALSE(IsBackground(32, 32));
    m_pass->SetViewDistance(4.0f);
    Render(view);
    EXPECT_TRUE(IsBackground(32, 32));
}

TEST_F(MeshPassTest, CameraWithoutAFieldOfViewUsesTheRenderersAcrossTheView) {
    // A quad 2 m wide at 5 m: 90 degrees across a square view show 10 m, so it fills a fifth of the width;
    // 30 degrees show 2.68 m, so its edges are near the sides.
    const MeshHandle mesh = Mesh(MakeQuad(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f));
    CameraView camera = TestCamera();
    camera.verticalFov.reset();
    const SceneView view{camera, {{mesh, glm::mat4(1.0f), Material({1.0f, 0.0f, 0.0f})}}};
    m_pass->SetFieldOfView(glm::radians(90.0f));
    Render(view);
    EXPECT_FALSE(IsBackground(32, 32));
    EXPECT_TRUE(IsBackground(16, 32));
    m_pass->SetFieldOfView(glm::radians(30.0f));
    Render(view);
    EXPECT_FALSE(IsBackground(16, 32));
    EXPECT_TRUE(IsBackground(2, 32));
}

namespace {

// A particle pass drawing over the lit frame, with a plain white texture.
class ParticleTest : public MeshPassTest {
protected:
    void SetUp() override {
        MeshPassTest::SetUp();
        if (IsSkipped() || HasFatalFailure()) {
            return;
        }
        core::Result<renderer::ParticlePass> particles = renderer::ParticlePass::Create(Device(), kColorFormat);
        ASSERT_TRUE(particles.has_value()) << core::Format(particles.error());
        m_particles.emplace(std::move(*particles));
        resources::ImageData image;
        image.width = 1;
        image.height = 1;
        image.format = resources::PixelFormat::Rgba8Unorm;
        image.pixels.assign(4, std::byte{255});
        core::Result<rhi::TextureHandle> texture = renderer::CreateTexture(Device(), image, "White");
        ASSERT_TRUE(texture.has_value());
        m_white = *texture;
        const core::Result<rhi::TextureHandle> reactive =
            Device().CreateTexture({.extent = kExtent,
                                    .format = renderer::ParticlePass::kReactiveFormat,
                                    .usage = rhi::TextureUsage::ColorTarget | rhi::TextureUsage::TransferSrc,
                                    .name = "Reactive"});
        ASSERT_TRUE(reactive.has_value());
        m_reactive = *reactive;
    }

    void TearDown() override {
        if (m_particles.has_value()) {
            m_particles.reset();
            for (const rhi::TextureHandle texture : m_textures) {
                Device().DestroyTexture(texture);
            }
            Device().DestroyTexture(m_white);
            Device().DestroyTexture(m_reactive);
        }
        MeshPassTest::TearDown();
    }

    // An opaque square of one meter, unlit, that stays where it appears for ten seconds.
    static renderer::ParticleEffectDesc Square(rhi::TextureHandle texture, const glm::vec3& color) {
        return {.texture = texture,
                .color = {{0.0f, glm::vec4(color, 1.0f)}},
                .lit = false,
                .lifetime = {10.0f, 10.0f},
                .size = {1.0f, 1.0f},
                .softness = 0.001f};
    }
    renderer::ParticleEffectHandle Effect(const renderer::ParticleEffectDesc& desc) {
        core::Result<renderer::ParticleEffectHandle> effect = m_particles->CreateEffect(desc);
        EXPECT_TRUE(effect.has_value());
        return effect.value_or(renderer::ParticleEffectHandle{});
    }
    renderer::ParticleEffectHandle Effect(const glm::vec3& color) { return Effect(Square(m_white, color)); }

    // One particle of the effect at the origin, drawn after step seconds of its life, alone: the particles
    // of earlier frames are gone.
    void RenderOne(const renderer::ParticleEffectDesc& desc, float step) {
        RenderWithParticles({TestCamera()}, {}, 1000.0f);
        const renderer::ParticleSpawn spawn{.effect = Effect(desc), .count = 1};
        RenderWithParticles({TestCamera()}, std::span(&spawn, 1), step);
    }

    // A flipbook of two frames side by side, red then blue, each two pixels wide so that filtering at a
    // frame's center does not reach the other.
    rhi::TextureHandle RedThenBlue() {
        resources::ImageData image;
        image.width = 4;
        image.height = 1;
        image.format = resources::PixelFormat::Rgba8Unorm;
        for (const std::array<std::uint8_t, 4> pixel :
             {std::array<std::uint8_t, 4>{255, 0, 0, 255}, std::array<std::uint8_t, 4>{255, 0, 0, 255},
              std::array<std::uint8_t, 4>{0, 0, 255, 255}, std::array<std::uint8_t, 4>{0, 0, 255, 255}}) {
            for (const std::uint8_t value : pixel) {
                image.pixels.push_back(static_cast<std::byte>(value));
            }
        }
        core::Result<rhi::TextureHandle> texture = renderer::CreateTexture(Device(), image, "Red then blue");
        EXPECT_TRUE(texture.has_value());
        m_textures.push_back(texture.value_or(rhi::TextureHandle{}));
        return m_textures.back();
    }

    // Renders the meshes of the view, then simulates and draws the particles over them: they meet the
    // view's meshes, and move in its wind and attractors.
    void RenderWithParticles(const SceneView& view, std::span<const renderer::ParticleSpawn> spawns, float step) {
        Render(view);
        SceneView particles = view;
        particles.particleSpawns.assign(spawns.begin(), spawns.end());
        particles.timeStep = step;
        ASSERT_TRUE(Device()
                        .Submit([&](rhi::CommandList& commands) {
                            renderer::RenderGraph graph;
                            EXPECT_TRUE(m_particles
                                            ->AddSimulation(graph, particles, m_pass->FrameDataIndex(),
                                                            {.structure = m_scene->Structure(),
                                                             .instanceBuffer = m_pass->InstanceDataIndex(),
                                                             .materialBuffer = m_pass->MaterialDataIndex(),
                                                             .skinnedVertices = m_pass->SkinnedVertices(),
                                                             .emissiveTriangles = m_pass->EmissiveTriangles(),
                                                             .lightMap = m_pass->LightMapIndex(),
                                                             .previousLightCount = m_pass->PreviousLightCount()})
                                            .has_value());
                            m_particles->AddDraw(graph, m_pass->FrameDataIndex(), m_depth, m_color, m_reactive,
                                                 glm::vec3(1, 0, 0), glm::vec3(0, 1, 0));

                            graph.Execute(commands);
                            commands.Transition(m_color, rhi::TextureState::TransferSrc);
                            commands.CopyTextureToBuffer(m_color, m_readback);
                        })
                        .has_value());
    }

    // Particles fade in over the first tenth of their life: a step past it shows them whole.
    static constexpr float kPastFadeIn = 1.5f;

    // One meter at the origin, in pixels: the camera is 5 m away, with a field of view of 60 degrees.
    static constexpr float kPixelsPerMeter = 32.0f / 2.8867513f;

    std::optional<renderer::ParticlePass> m_particles;
    rhi::TextureHandle m_white;
    std::vector<rhi::TextureHandle> m_textures;
    rhi::TextureHandle m_reactive;
};

} // namespace

TEST_F(ParticleTest, SpawnedParticleIsDrawnWhereItAppears) {
    const renderer::ParticleSpawn spawn{.effect = Effect({0.0f, 1.0f, 0.0f}), .count = 1};
    RenderWithParticles({TestCamera()}, std::span(&spawn, 1), kPastFadeIn);
    EXPECT_EQ(Pixel(32, 32), glm::ivec3(0, 255, 0));
    EXPECT_TRUE(IsBackground(4, 4));
}

TEST_F(ParticleTest, ParticlesMarkWhatTheyCoverAsReactive) {
    const renderer::ParticleSpawn spawn{.effect = Effect({0.0f, 1.0f, 0.0f}), .count = 1};
    RenderWithParticles({TestCamera()}, std::span(&spawn, 1), kPastFadeIn);
    ASSERT_TRUE(Device()
                    .Submit([&](rhi::CommandList& commands) {
                        commands.Transition(m_reactive, rhi::TextureState::TransferSrc);
                        commands.CopyTextureToBuffer(m_reactive, m_motionReadback);
                    })
                    .has_value());
    const auto reactive = [&](std::uint32_t x, std::uint32_t y) {
        return static_cast<int>(reinterpret_cast<const std::uint8_t*>(
            Device().Mapped(m_motionReadback).data())[std::size_t{y} * kExtent.width + x]);
    };
    EXPECT_EQ(reactive(32, 32), 255) << "an opaque particle";
    EXPECT_EQ(reactive(4, 4), 0) << "no particle";
}

TEST_F(ParticleTest, ParticleDisappearsAtTheEndOfItsLife) {
    const renderer::ParticleSpawn spawn{.effect = Effect({0.0f, 1.0f, 0.0f}), .count = 1};
    RenderWithParticles({TestCamera()}, std::span(&spawn, 1), kPastFadeIn);
    ASSERT_EQ(Pixel(32, 32), glm::ivec3(0, 255, 0));
    RenderWithParticles({TestCamera()}, {}, 9.0f);
    EXPECT_TRUE(IsBackground(32, 32));
}

TEST_F(ParticleTest, NearerParticleIsDrawnOverAFartherOne) {
    // Spawned near first, so that only the sort can put the far one behind.
    const std::array<renderer::ParticleSpawn, 2> spawns{
        renderer::ParticleSpawn{.effect = Effect({1.0f, 0.0f, 0.0f}), .position = {0.0f, 0.0f, 1.0f}, .count = 1},
        renderer::ParticleSpawn{.effect = Effect({0.0f, 0.0f, 1.0f}), .position = {0.0f, 0.0f, -1.0f}, .count = 1}};
    RenderWithParticles({TestCamera()}, spawns, kPastFadeIn);
    EXPECT_EQ(Pixel(32, 32), glm::ivec3(255, 0, 0));
}

TEST_F(ParticleTest, SortSpansTheWholePool) {
    // Thousands of far particles before and after a near one, so that it sorts across the groups' runs.
    const renderer::ParticleEffectHandle far = Effect({0.0f, 0.0f, 1.0f});
    const std::array<renderer::ParticleSpawn, 3> spawns{
        renderer::ParticleSpawn{.effect = far, .position = {0.0f, 0.0f, -1.0f}, .count = 5000},
        renderer::ParticleSpawn{.effect = Effect({1.0f, 0.0f, 0.0f}), .position = {0.0f, 0.0f, 1.0f}, .count = 1},
        renderer::ParticleSpawn{.effect = far, .position = {0.0f, 0.0f, -1.0f}, .count = 5000}};
    RenderWithParticles({TestCamera()}, spawns, kPastFadeIn);
    EXPECT_EQ(Pixel(32, 32), glm::ivec3(255, 0, 0));
}

TEST_F(ParticleTest, ParticleBehindAMeshIsHidden) {
    const MeshHandle wall = Mesh(MakeQuad(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f));
    const renderer::ParticleSpawn spawn{
        .effect = Effect({0.0f, 1.0f, 0.0f}), .position = {0.0f, 0.0f, -1.0f}, .count = 1};
    RenderWithParticles({TestCamera(), {{wall, glm::mat4(1.0f), Material({1.0f, 0.0f, 0.0f})}}}, std::span(&spawn, 1),
                        kPastFadeIn);
    EXPECT_GT(Pixel(32, 32).r, 200);
    EXPECT_LT(Pixel(32, 32).g, 16);
}

TEST_F(ParticleTest, FullPoolDropsNewParticles) {
    const renderer::ParticleSpawn spawn{.effect = Effect({0.0f, 1.0f, 0.0f}), .count = renderer::kMaxParticles + 100};
    RenderWithParticles({TestCamera()}, std::span(&spawn, 1), kPastFadeIn);
    RenderWithParticles({TestCamera()}, std::span(&spawn, 1), kPastFadeIn);
    EXPECT_EQ(Pixel(32, 32), glm::ivec3(0, 255, 0));
}

TEST_F(ParticleTest, ColorFollowsItsGradientOverLife) {
    renderer::ParticleEffectDesc desc = Square(m_white, {});
    desc.color = {{0.0f, {1.0f, 0.0f, 0.0f, 1.0f}}, {1.0f, {0.0f, 0.0f, 1.0f, 1.0f}}};
    RenderOne(desc, 5.0f);
    const glm::ivec3 pixel = Pixel(32, 32);
    EXPECT_NEAR(pixel.r, 128, 2);
    EXPECT_EQ(pixel.g, 0);
    EXPECT_NEAR(pixel.b, 128, 2);
}

TEST_F(ParticleTest, EmissionCurveScalesItsLight) {
    renderer::ParticleEffectDesc desc = Square(m_white, {0.0f, 1.0f, 0.0f});
    desc.emissionOverLife = {{0.0f, 0.5f}};
    RenderOne(desc, kPastFadeIn);
    EXPECT_NEAR(Pixel(32, 32).g, 128, 2);
}

TEST_F(ParticleTest, SizeCurveShrinksIt) {
    // Half a meter across at half its life: 2.8 pixels from its center, where a whole one reaches 5.5.
    const auto edge = static_cast<std::uint32_t>(32.0f + 0.4f * kPixelsPerMeter);
    renderer::ParticleEffectDesc desc = Square(m_white, {0.0f, 1.0f, 0.0f});
    RenderOne(desc, 5.0f);
    ASSERT_EQ(Pixel(edge, 32), glm::ivec3(0, 255, 0));
    desc.sizeOverLife = {{0.0f, 1.0f}, {1.0f, 0.0f}};
    RenderOne(desc, 5.0f);
    EXPECT_EQ(Pixel(32, 32), glm::ivec3(0, 255, 0));
    EXPECT_TRUE(IsBackground(edge, 32));
}

TEST_F(ParticleTest, FlipbookPlaysAtItsFrameRateAndHoldsItsLastFrame) {
    renderer::ParticleEffectDesc desc = Square(RedThenBlue(), glm::vec3(1.0f));
    desc.columns = 2;
    desc.rows = 1;
    desc.frames = 2;
    desc.frameRate = 1.0f;
    desc.loop = false;
    desc.fadeIn = 0.0f;
    RenderOne(desc, 0.0f);
    EXPECT_EQ(Pixel(32, 32), glm::ivec3(255, 0, 0)) << "the first frame";
    RenderOne(desc, 0.5f);
    EXPECT_NEAR(Pixel(32, 32).r, 128, 2) << "half way, the frames are blended";
    EXPECT_NEAR(Pixel(32, 32).b, 128, 2);
    RenderOne(desc, 3.0f);
    EXPECT_EQ(Pixel(32, 32), glm::ivec3(0, 0, 255)) << "held on the last frame";
    desc.loop = true;
    RenderOne(desc, 4.0f);
    EXPECT_EQ(Pixel(32, 32), glm::ivec3(255, 0, 0)) << "looped back to the first";
}

TEST_F(ParticleTest, FlipbookOverLifePlaysOnce) {
    renderer::ParticleEffectDesc desc = Square(RedThenBlue(), glm::vec3(1.0f));
    desc.columns = 2;
    desc.rows = 1;
    desc.frames = 2;
    desc.fadeIn = 0.0f;
    RenderOne(desc, 9.9f);
    EXPECT_EQ(Pixel(32, 32), glm::ivec3(0, 0, 255));
}

TEST_F(ParticleTest, VelocityParticlesStretchAlongTheirMotion) {
    // Moving along +X at 0.1 m/s, stretched over 20 s of motion: 3 m long, 1 m wide.
    const auto along = static_cast<std::uint32_t>(32.0f + 1.2f * kPixelsPerMeter);
    const auto across = static_cast<std::uint32_t>(32.0f + 0.8f * kPixelsPerMeter);
    renderer::ParticleEffectDesc desc = Square(m_white, {0.0f, 1.0f, 0.0f});
    desc.direction = {1.0f, 0.0f, 0.0f};
    desc.speed = {0.1f, 0.1f};
    desc.fadeIn = 0.0f;
    RenderOne(desc, 0.0f);
    ASSERT_TRUE(IsBackground(along, 32)) << "facing the camera, it is a square";
    desc.orientation = renderer::ParticleOrientation::Velocity;
    desc.stretch = 20.0f;
    RenderOne(desc, 0.0f);
    EXPECT_EQ(Pixel(along, 32), glm::ivec3(0, 255, 0));
    EXPECT_TRUE(IsBackground(32, across));
}

TEST_F(ParticleTest, PlaneParticlesLieAcrossTheirAxis) {
    renderer::ParticleEffectDesc desc = Square(m_white, {0.0f, 1.0f, 0.0f});
    desc.orientation = renderer::ParticleOrientation::Plane;
    RenderOne(desc, kPastFadeIn);
    EXPECT_TRUE(IsBackground(32, 32)) << "a floor seen edge on";
    desc.axis = {0.0f, 0.0f, 1.0f};
    RenderOne(desc, kPastFadeIn);
    EXPECT_EQ(Pixel(32, 32), glm::ivec3(0, 255, 0)) << "a wall facing the camera";
}

TEST_F(ParticleTest, AxisParticlesTurnOnlyAroundTheirAxis) {
    // A square of 2 m whose axis leans towards the camera, in XZ: seen along -Z, it reaches 1 m either way
    // across Y, but along X its axis is foreshortened to 0.54 m.
    const auto reach = static_cast<std::uint32_t>(32.0f + 0.77f * kPixelsPerMeter);
    renderer::ParticleEffectDesc desc = Square(m_white, {0.0f, 1.0f, 0.0f});
    desc.orientation = renderer::ParticleOrientation::Axis;
    desc.axis = {1.0f, 0.0f, 2.0f};
    desc.size = {2.0f, 2.0f};
    RenderOne(desc, kPastFadeIn);
    EXPECT_EQ(Pixel(32, reach), glm::ivec3(0, 255, 0));
    EXPECT_TRUE(IsBackground(reach, 32));
}

TEST_F(ParticleTest, LineShapeSpawnsAlongTheEmittersX) {
    const auto far = static_cast<std::uint32_t>(2.0f * kPixelsPerMeter);
    renderer::ParticleEffectDesc desc = Square(m_white, {0.0f, 1.0f, 0.0f});
    desc.shape = renderer::ParticleShape::Line;
    desc.spawnBox = {2.5f, 0.0f, 0.0f};
    desc.size = {0.3f, 0.3f};
    const renderer::ParticleSpawn spawn{.effect = Effect(desc), .count = 500};
    RenderWithParticles({TestCamera()}, std::span(&spawn, 1), kPastFadeIn);
    EXPECT_EQ(Pixel(32 + far, 32), glm::ivec3(0, 255, 0));
    EXPECT_EQ(Pixel(32 - far, 32), glm::ivec3(0, 255, 0));
    EXPECT_TRUE(IsBackground(32, 32 + far));
}

TEST_F(ParticleTest, SpawnsAreSpreadAlongTheWayTheEmitterWent) {
    const auto far = static_cast<std::uint32_t>(2.0f * kPixelsPerMeter);
    renderer::ParticleEffectDesc desc = Square(m_white, {0.0f, 1.0f, 0.0f});
    desc.size = {0.3f, 0.3f};
    const renderer::ParticleSpawn spawn{.effect = Effect(desc),
                                        .position = {2.5f, 0.0f, 0.0f},
                                        .count = 500,
                                        .previousPosition = glm::vec3(-2.5f, 0, 0)};
    RenderWithParticles({TestCamera()}, std::span(&spawn, 1), kPastFadeIn);
    EXPECT_EQ(Pixel(32 + far, 32), glm::ivec3(0, 255, 0));
    EXPECT_EQ(Pixel(32 - far, 32), glm::ivec3(0, 255, 0));
    EXPECT_EQ(Pixel(32, 32), glm::ivec3(0, 255, 0));
}

TEST_F(ParticleTest, ParticlesInheritTheirEmittersVelocity) {
    // Still in the effect, carried at 1 m/s along +X by the emitter, for 1.5 s.
    renderer::ParticleEffectDesc desc = Square(m_white, {0.0f, 1.0f, 0.0f});
    desc.inheritVelocity = 1.0f;
    const renderer::ParticleSpawn spawn{.effect = Effect(desc), .count = 1, .velocity = {1.0f, 0.0f, 0.0f}};
    RenderWithParticles({TestCamera()}, std::span(&spawn, 1), 1.5f);
    EXPECT_TRUE(IsBackground(32, 32));
    EXPECT_EQ(Pixel(static_cast<std::uint32_t>(32.0f + 1.5f * kPixelsPerMeter), 32), glm::ivec3(0, 255, 0));
}

TEST_F(ParticleTest, ConeOpensOutTowardsItsRim) {
    // Leaving a disc of 2 m across +Y, opened by up to 90 degrees at the rim, at 1 m/s for a second: the
    // rim's particles go out sideways, its center's straight up.
    renderer::ParticleEffectDesc desc = Square(m_white, {0.0f, 1.0f, 0.0f});
    desc.shape = renderer::ParticleShape::Cone;
    desc.spawnRadius = 1.0f;
    desc.spread = glm::radians(90.0f);
    desc.speed = {1.0f, 1.0f};
    desc.size = {0.3f, 0.3f};
    const renderer::ParticleSpawn spawn{.effect = Effect(desc), .count = 2000};
    RenderWithParticles({TestCamera()}, std::span(&spawn, 1), 1.0f);
    EXPECT_EQ(Pixel(static_cast<std::uint32_t>(32.0f + 1.9f * kPixelsPerMeter), 32), glm::ivec3(0, 255, 0));
    EXPECT_EQ(Pixel(32, static_cast<std::uint32_t>(32.0f - 0.9f * kPixelsPerMeter)), glm::ivec3(0, 255, 0));
}

TEST_F(ParticleTest, UpdatedEffectChangesTheParticlesAlive) {
    const renderer::ParticleEffectHandle effect = Effect({0.0f, 1.0f, 0.0f});
    const renderer::ParticleSpawn spawn{.effect = effect, .count = 1};
    RenderWithParticles({TestCamera()}, std::span(&spawn, 1), kPastFadeIn);
    ASSERT_EQ(Pixel(32, 32), glm::ivec3(0, 255, 0));
    EXPECT_TRUE(m_particles->UpdateEffect(effect, Square(m_white, {1.0f, 0.0f, 0.0f})));
    RenderWithParticles({TestCamera()}, {}, 0.1f);
    EXPECT_EQ(Pixel(32, 32), glm::ivec3(255, 0, 0));
    m_particles->DestroyEffect(effect);
    EXPECT_FALSE(m_particles->UpdateEffect(effect, Square(m_white, {1.0f, 0.0f, 0.0f})));
}

namespace {

// A red wall across the view, a meter behind the origin, which particles can meet.
SceneView WithWall(MeshHandle wall, MaterialHandle red) {
    return {TestCamera(), {{wall, glm::mat4(1.0f), red}}};
}

} // namespace

TEST_F(ParticleTest, ParticlesPassThroughSurfacesWithoutCollision) {
    const SceneView view = WithWall(Mesh(MakeQuad(-3.0f, 3.0f, -3.0f, 3.0f, -1.0f)), Material({1.0f, 0.0f, 0.0f}));
    renderer::ParticleEffectDesc desc = Square(m_white, {0.0f, 1.0f, 0.0f});
    desc.direction = {0.0f, 0.0f, -1.0f};
    desc.speed = {2.0f, 2.0f};
    desc.size = {0.3f, 0.3f};
    const renderer::ParticleSpawn spawn{.effect = Effect(desc), .count = 1};
    RenderWithParticles(view, std::span(&spawn, 1), kPastFadeIn);
    EXPECT_GT(Pixel(32, 32).r, 100) << "behind the wall";
    EXPECT_LT(Pixel(32, 32).g, 16);
}

TEST_F(ParticleTest, StickingParticleStaysWhereItHits) {
    const SceneView view = WithWall(Mesh(MakeQuad(-3.0f, 3.0f, -3.0f, 3.0f, -1.0f)), Material({1.0f, 0.0f, 0.0f}));
    renderer::ParticleEffectDesc desc = Square(m_white, {0.0f, 1.0f, 0.0f});
    desc.direction = {0.0f, 0.0f, -1.0f};
    desc.speed = {2.0f, 2.0f};
    desc.size = {0.3f, 0.3f};
    desc.collision = renderer::ParticleCollision::Stick;
    const renderer::ParticleSpawn spawn{.effect = Effect(desc), .count = 1};
    RenderWithParticles(view, std::span(&spawn, 1), kPastFadeIn);
    EXPECT_EQ(Pixel(32, 32), glm::ivec3(0, 255, 0)) << "on the wall's face";
    RenderWithParticles(view, {}, 1.0f);
    EXPECT_EQ(Pixel(32, 32), glm::ivec3(0, 255, 0)) << "still there";
}

TEST_F(ParticleTest, BouncingParticleComesBackOffTheWall) {
    // Along +X at 1 m/s, and towards the wall at 2 m/s: it hits at 0.5 s, 0.5 m to the side, and comes back
    // out to about 0.85 m in front of the origin and 1.45 m to the side by 1.5 s, in steps of 0.1 s.
    const SceneView view = WithWall(Mesh(MakeQuad(-3.0f, 3.0f, -3.0f, 3.0f, -1.0f)), Material({1.0f, 0.0f, 0.0f}));
    renderer::ParticleEffectDesc desc = Square(m_white, {0.0f, 1.0f, 0.0f});
    desc.direction = glm::normalize(glm::vec3(1.0f, 0.0f, -2.0f));
    desc.speed = glm::vec2(glm::sqrt(5.0f));
    desc.size = {0.6f, 0.6f};
    desc.collision = renderer::ParticleCollision::Bounce;
    desc.bounce = 1.0f;
    RenderWithParticles(view, {}, 1000.0f);
    const renderer::ParticleSpawn spawn{.effect = Effect(desc), .count = 1};
    for (int frame = 0; frame < 15; ++frame) {
        RenderWithParticles(view, frame == 0 ? std::span(&spawn, 1) : std::span<const renderer::ParticleSpawn>{}, 0.1f);
    }
    const auto bounced = static_cast<std::uint32_t>(32.0f + 1.45f * 5.0f / 4.15f * kPixelsPerMeter);
    EXPECT_EQ(Pixel(bounced, 32), glm::ivec3(0, 255, 0));
}

TEST_F(ParticleTest, HitSpawnsTheSubEmittersEffectAndIsReported) {
    const MaterialHandle red = Material({1.0f, 0.0f, 0.0f});
    const SceneView view = WithWall(Mesh(MakeQuad(-3.0f, 3.0f, -3.0f, 3.0f, -1.0f)), red);
    const renderer::ParticleEffectHandle splash = Effect({0.0f, 0.0f, 1.0f});
    renderer::ParticleEffectDesc desc = Square(m_white, {0.0f, 1.0f, 0.0f});
    desc.direction = {0.0f, 0.0f, -1.0f};
    desc.speed = {2.0f, 2.0f};
    desc.size = {0.3f, 0.3f};
    desc.collision = renderer::ParticleCollision::Die;
    desc.reportHits = true;
    desc.onHit = {.effect = splash, .count = 1};
    const renderer::ParticleEffectHandle drop = Effect(desc);
    const renderer::ParticleSpawn spawn{.effect = drop, .count = 1};
    RenderWithParticles(view, std::span(&spawn, 1), 1.0f);
    EXPECT_GT(Pixel(32, 32).r, 100) << "the drop died on the wall";
    RenderWithParticles(view, {}, kPastFadeIn);
    EXPECT_EQ(Pixel(32, 32), glm::ivec3(0, 0, 255)) << "its splash, the next frame";

    // The CPU reads the hit once the GPU has finished its frame.
    std::vector<renderer::ParticlePass::Hit> hits;
    for (std::uint32_t frame = 0; frame < rhi::kFramesInFlight && hits.empty(); ++frame) {
        RenderWithParticles(view, {}, 0.0f);
        hits.assign(m_particles->Hits().begin(), m_particles->Hits().end());
    }
    ASSERT_EQ(hits.size(), 1U);
    EXPECT_EQ(hits[0].effect, drop);
    EXPECT_EQ(hits[0].material, red.Index());
    EXPECT_NEAR(hits[0].position.z, -1.0f, 0.01f);
    EXPECT_NEAR(glm::distance(hits[0].normal, glm::vec3(0.0f, 0.0f, 1.0f)), 0.0f, 1e-3f) << "towards the drop";
    RenderWithParticles(view, {}, 0.0f);
    EXPECT_TRUE(m_particles->Hits().empty()) << "each hit is read once";
}

TEST_F(ParticleTest, DeathSpawnsTheSubEmittersEffectWhereItDied) {
    renderer::ParticleEffectDesc desc = Square(m_white, {0.0f, 1.0f, 0.0f});
    desc.lifetime = {1.0f, 1.0f};
    desc.onDeath = {.effect = Effect({0.0f, 0.0f, 1.0f}), .count = 1};
    const renderer::ParticleSpawn spawn{.effect = Effect(desc), .count = 1};
    RenderWithParticles({TestCamera()}, std::span(&spawn, 1), kPastFadeIn);
    EXPECT_TRUE(IsBackground(32, 32)) << "it died";
    RenderWithParticles({TestCamera()}, {}, kPastFadeIn);
    EXPECT_EQ(Pixel(32, 32), glm::ivec3(0, 0, 255));
}

TEST_F(ParticleTest, WindCarriesParticlesByTheirDrag) {
    // In a wind of 1 m/s along +X, a particle of high drag soon moves with it: 1.4 m in 1.5 s.
    const auto carried = static_cast<std::uint32_t>(32.0f + 1.4f * kPixelsPerMeter);
    renderer::ParticleEffectDesc desc = Square(m_white, {0.0f, 1.0f, 0.0f});
    desc.drag = 10.0f;
    SceneView windy{TestCamera()};
    windy.atmosphere.wind = {1.0f, 0.0f, 0.0f};
    const renderer::ParticleSpawn spawn{.effect = Effect(desc), .count = 1};
    RenderWithParticles(windy, std::span(&spawn, 1), kPastFadeIn);
    EXPECT_EQ(Pixel(carried, 32), glm::ivec3(0, 255, 0));
    EXPECT_TRUE(IsBackground(32, 32));

    // Without drag, it is too heavy for the wind.
    desc.drag = 0.0f;
    RenderWithParticles(windy, {}, 1000.0f);
    const renderer::ParticleSpawn heavy{.effect = Effect(desc), .count = 1};
    RenderWithParticles(windy, std::span(&heavy, 1), kPastFadeIn);
    EXPECT_EQ(Pixel(32, 32), glm::ivec3(0, 255, 0));
}

TEST_F(ParticleTest, WindVolumesBlowOnlyInsideTheirBox) {
    const auto carried = static_cast<std::uint32_t>(32.0f + 1.4f * kPixelsPerMeter);
    renderer::ParticleEffectDesc desc = Square(m_white, {0.0f, 1.0f, 0.0f});
    desc.drag = 10.0f;
    SceneView view{TestCamera()};
    view.windVolumes.push_back(
        {.world = glm::scale(glm::mat4(1.0f), glm::vec3(10.0f)), .velocity = {1.0f, 0.0f, 0.0f}, .edge = 0.0f});
    const renderer::ParticleSpawn spawn{.effect = Effect(desc), .count = 1};
    RenderWithParticles(view, std::span(&spawn, 1), kPastFadeIn);
    EXPECT_EQ(Pixel(carried, 32), glm::ivec3(0, 255, 0));

    view.windVolumes[0].world = glm::translate(glm::mat4(1.0f), glm::vec3(50.0f, 0.0f, 0.0f));
    RenderWithParticles(view, {}, 1000.0f);
    RenderWithParticles(view, std::span(&spawn, 1), kPastFadeIn);
    EXPECT_EQ(Pixel(32, 32), glm::ivec3(0, 255, 0)) << "a box far away";
}

TEST_F(ParticleTest, TurbulenceStirsStillParticles) {
    renderer::ParticleEffectDesc desc = Square(m_white, {0.0f, 1.0f, 0.0f});
    desc.size = {0.3f, 0.3f};
    desc.turbulence = 5.0f;
    RenderOne(desc, kPastFadeIn);
    EXPECT_TRUE(IsBackground(32, 32));
}

TEST_F(ParticleTest, AttractorsPullTheirEffectOnly) {
    // 2 m/s squared towards the origin, from 2 m to its left, in five steps of 0.2 s: 1.2 m closer.
    renderer::ParticleEffectDesc desc = Square(m_white, {0.0f, 1.0f, 0.0f});
    desc.fadeIn = 0.0f;
    const renderer::ParticleEffectHandle pulled = Effect(desc);
    const renderer::ParticleEffectHandle other = Effect(desc);
    const auto start = static_cast<std::uint32_t>(32.0f - 2.0f * kPixelsPerMeter);
    const auto closer = static_cast<std::uint32_t>(32.0f - 0.8f * kPixelsPerMeter);
    for (const auto& [effect, moves] : {std::pair{pulled, true}, std::pair{other, false}}) {
        SceneView view{TestCamera()};
        view.particleAttractors.push_back({.effect = pulled, .strength = 2.0f});
        RenderWithParticles(view, {}, 1000.0f);
        const renderer::ParticleSpawn spawn{.effect = effect, .position = {-2.0f, 0.0f, 0.0f}, .count = 1};
        for (int frame = 0; frame < 5; ++frame) {
            RenderWithParticles(view, frame == 0 ? std::span(&spawn, 1) : std::span<const renderer::ParticleSpawn>{},
                                0.2f);
        }
        if (moves) {
            EXPECT_EQ(Pixel(closer, 32), glm::ivec3(0, 255, 0));
            EXPECT_TRUE(IsBackground(start, 32));
        } else {
            EXPECT_EQ(Pixel(start, 32), glm::ivec3(0, 255, 0)) << "another effect";
        }
    }
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
    const std::filesystem::path source = directory / "Green.slang";
    // The vertex shader of the G-buffer, and a pixel shader of its own, which writes a black metal that
    // glows green: nothing but its emission shows.
    std::ofstream(source) << R"(
#include "MeshCommon.slangh"
struct Output {
    float4 normal : SV_Target0;
    float2 motion : SV_Target1;
    float4 baseColor : SV_Target2;
    float4 emission : SV_Target3;
};
Output PSMain(VertexOutput input) {
    Output output;
    output.normal = float4(0.5, 0.5, 1.0, 1.0);
    output.motion = float2(0.0, 0.0);
    output.baseColor = float4(0.0, 0.0, 0.0, 1.0);
    output.emission = float4(0.0, 1.0, 0.0, 1.0);
    return output;
}
)";
    for (const char* include : {"MeshTypes.slangh", "MeshCommon.slangh"}) {
        std::filesystem::copy_file(std::filesystem::path(DAI_SHADER_SOURCE_DIR) / include, directory / include,
                                   std::filesystem::copy_options::overwrite_existing);
    }

    const auto vertex = renderer::CompileShader(DAI_SLANGC_PATH, source, renderer::ShaderStage::Vertex);
    ASSERT_TRUE(vertex.has_value()) << core::Format(vertex.error());
    const auto pixel = renderer::CompileShader(DAI_SLANGC_PATH, source, renderer::ShaderStage::Pixel);
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
    desc.metallicFactor = 0.0f;
    const core::Result<MaterialHandle> material = m_pass->CreateMaterial(desc);
    ASSERT_TRUE(material.has_value());
    const MeshHandle mesh = Mesh(MakeQuad(-2.0f, 2.0f, -2.0f, 2.0f, 0.0f));

    Render({TestCamera(), {{mesh, glm::mat4(1.0f), *material}}});
    EXPECT_GT(Pixel(21, 32).g, 0);
    EXPECT_LT(Pixel(21, 32).b, 16); // Only the white reflection of a dielectric.
    EXPECT_GT(Pixel(43, 32).b, 0);
    EXPECT_LT(Pixel(43, 32).g, 16); // Only the white reflection of a dielectric.
    Device().DestroyTexture(*texture);
}

TEST_F(MeshPassTest, TheEmissionOfATriangleIsAveragedFromItsTexture) {
    // Left half red, right half green, times 10 nits. The quad's first triangle covers mostly the right half,
    // its second mostly the left (ADR-0044).
    constexpr std::uint32_t kWidth = 64;
    std::vector<std::uint32_t> pixels(kWidth);
    for (std::uint32_t x = 0; x < kWidth; ++x) {
        pixels[x] = x < kWidth / 2 ? 0xFF0000FF : 0xFF00FF00;
    }
    resources::ImageData image;
    image.width = kWidth;
    image.height = 1;
    image.format = resources::PixelFormat::Rgba8Unorm;
    image.pixels.resize(pixels.size() * sizeof(std::uint32_t));
    std::memcpy(image.pixels.data(), pixels.data(), image.pixels.size());
    const core::Result<rhi::TextureHandle> texture = renderer::CreateTexture(Device(), image, "Glow");
    ASSERT_TRUE(texture.has_value()) << core::Format(texture.error());
    MaterialDesc desc;
    desc.emissiveFactor = glm::vec3(10.0f);
    desc.emissiveTexture = *texture;
    const core::Result<MaterialHandle> material = m_pass->CreateMaterial(desc);
    ASSERT_TRUE(material.has_value());
    const MeshHandle mesh = Mesh(MakeQuad(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f));

    // Read back once the GPU has finished the frame that averaged it.
    for (std::uint32_t frame = 0; frame <= rhi::kFramesInFlight; ++frame) {
        EXPECT_TRUE(m_pass->TriangleEmission(mesh, *material).empty()) << "frame " << frame;
        Render({TestCamera(), {{mesh, glm::mat4(1.0f), *material}}});
    }
    Render({TestCamera(), {{mesh, glm::mat4(1.0f), *material}}});
    const std::vector<glm::vec3> emission = m_pass->TriangleEmission(mesh, *material);
    ASSERT_EQ(emission.size(), 2U);
    // A quarter of the first triangle is on the red half, three quarters of the second.
    EXPECT_NEAR(emission[0].r, 2.5f, 0.5f);
    EXPECT_NEAR(emission[0].g, 7.5f, 0.5f);
    EXPECT_NEAR(emission[1].r, 7.5f, 0.5f);
    EXPECT_NEAR(emission[1].g, 2.5f, 0.5f);
    for (const glm::vec3& triangle : emission) {
        EXPECT_NEAR(triangle.r + triangle.g, 10.0f, 0.2f) << "the texture's average, times the factor";
        EXPECT_EQ(triangle.b, 0.0f);
    }
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
    // Turned away from the camera, which sees its back, lit from behind the camera: the back's normal faces
    // the light.
    const MeshHandle mesh = Mesh(MakeQuad(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f));
    const glm::mat4 turned = glm::rotate(glm::mat4(1.0f), glm::pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f));
    Render({TestCamera(), {{mesh, turned, *material}}});
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
    EXPECT_LT(Pixel(32, 32).g, 16); // Only the white reflection of a dielectric.
}

TEST_F(MeshPassTest, InstanceBufferGrowsWithTheNumberOfDraws) {
    const MeshHandle mesh = Mesh(MakeQuad(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f));
    SceneView view{TestCamera(), {}};
    view.draws.assign(5000, {mesh, glm::mat4(1.0f)});
    Render(view);
    EXPECT_FALSE(IsBackground(32, 32));
}

TEST_F(MeshPassTest, SkinnedMeshFollowsItsJointAndNotItsWorldMatrix) {
    MeshData data = MakeQuad(-0.5f, 0.5f, -0.5f, 0.5f, 0.0f);
    data.skin.assign(data.vertices.size(), resources::SkinWeights{});
    const MeshHandle mesh = Mesh(data);
    SceneView view{TestCamera(), {{mesh, glm::translate(glm::mat4(1.0f), glm::vec3(-1.0f, 0.0f, 0.0f)), {}, 0}}};
    view.jointMatrices = {glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f))};
    Render(view);
    EXPECT_TRUE(IsBackground(32, 32));
    EXPECT_TRUE(IsBackground(20, 32));
    EXPECT_FALSE(IsBackground(44, 32));
}

TEST_F(MeshPassTest, SkinnedVerticesBlendTheirJointsByWeight) {
    MeshData data = MakeQuad(-0.5f, 0.5f, -0.5f, 0.5f, 0.0f);
    // Half on a joint moved two meters right, half on a joint that stays: one meter right.
    data.skin.assign(data.vertices.size(), resources::SkinWeights{{0, 1, 0, 0}, {0.5f, 0.5f, 0.0f, 0.0f}});
    const MeshHandle mesh = Mesh(data);
    SceneView view{TestCamera(), {{mesh, glm::mat4(1.0f), {}, 1}}};
    view.jointMatrices = {glm::mat4(1.0f), glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.0f, 0.0f)),
                          glm::mat4(1.0f)};
    Render(view);
    EXPECT_TRUE(IsBackground(32, 32));
    EXPECT_FALSE(IsBackground(44, 32));
}

TEST_F(MeshPassTest, MeshWithoutSkinIsDrawnRigidEvenWithJoints) {
    const MeshHandle mesh = Mesh(MakeQuad(-0.5f, 0.5f, -0.5f, 0.5f, 0.0f));
    SceneView view{TestCamera(), {{mesh, glm::mat4(1.0f), {}, 0}}};
    view.jointMatrices = {glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f))};
    Render(view);
    EXPECT_FALSE(IsBackground(32, 32));
}

TEST_F(MeshPassTest, JointBufferGrowsWithTheNumberOfJoints) {
    MeshData data = MakeQuad(-0.5f, 0.5f, -0.5f, 0.5f, 0.0f);
    data.skin.assign(data.vertices.size(), resources::SkinWeights{});
    const MeshHandle mesh = Mesh(data);
    SceneView view{TestCamera(), {{mesh, glm::mat4(1.0f), {}, 999}}};
    view.jointMatrices.assign(1000, glm::mat4(1.0f));
    view.jointMatrices.back() = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    Render(view);
    EXPECT_FALSE(IsBackground(44, 32));
}

namespace {

// How far, in pixels, half a meter across moves on the screen at the test camera's distance of 5 m: half
// the view is 5 * tan(30 degrees) meters wide, and 32 pixels.
const float kHalfMeterInPixels = 0.5f / (5.0f * std::tan(glm::radians(30.0f))) * 32.0f;

} // namespace

TEST_F(MeshPassTest, AFirstFrameHasNoMotion) {
    const MeshHandle mesh = Mesh(MakeQuad(-10.0f, 10.0f, -10.0f, 10.0f, 0.0f));
    Render({TestCamera(), {{mesh, glm::mat4(1.0f), Material({1.0f, 1.0f, 1.0f})}}});
    EXPECT_EQ(Motion(32, 32), glm::vec2(0.0f));
}

TEST_F(MeshPassTest, AMovingCameraMovesWhatItSees) {
    const MeshHandle mesh = Mesh(MakeQuad(-10.0f, 10.0f, -10.0f, 10.0f, 0.0f));
    SceneView view{TestCamera(), {{mesh, glm::mat4(1.0f), Material({1.0f, 1.0f, 1.0f})}}};
    Render(view);
    // Half a meter to the right: the wall was further right on the screen.
    view.camera->view = glm::translate(glm::mat4(1.0f), glm::vec3(-0.5f, 0.0f, -5.0f));
    Render(view);
    for (const std::uint32_t x : {10U, 32U, 50U}) {
        EXPECT_NEAR(Motion(x, 20).x, kHalfMeterInPixels, 0.05f);
        EXPECT_NEAR(Motion(x, 20).y, 0.0f, 0.05f);
    }

    view.cut = true;
    Render(view);
    EXPECT_EQ(Motion(32, 32), glm::vec2(0.0f)) << "nothing moves across a cut";
}

TEST_F(MeshPassTest, AMovedDrawGivesWhereItWas) {
    const MeshHandle mesh = Mesh(MakeQuad(-0.5f, 0.5f, -0.5f, 0.5f, 0.0f));
    renderer::DrawItem draw{mesh, glm::translate(glm::mat4(1.0f), glm::vec3(0.5f, 0.0f, 0.0f)),
                            Material({1.0f, 1.0f, 1.0f})};
    draw.previousWorld = glm::mat4(1.0f);
    Render({TestCamera(), {draw}});
    // Half a meter right of the center: it came from the left.
    EXPECT_NEAR(Motion(37, 32).x, -kHalfMeterInPixels, 0.05f);
    EXPECT_NEAR(Motion(37, 32).y, 0.0f, 0.05f);
    EXPECT_EQ(Motion(2, 32), glm::vec2(0.0f)) << "where nothing is drawn";

    draw.previousWorld.reset();
    Render({TestCamera(), {draw}});
    EXPECT_EQ(Motion(37, 32), glm::vec2(0.0f)) << "a draw without a previous matrix stood still";
}

TEST_F(MeshPassTest, ASkinnedDrawMovesWithItsPreviousJoints) {
    MeshData data = MakeQuad(-0.5f, 0.5f, -0.5f, 0.5f, 0.0f);
    data.skin.assign(data.vertices.size(), resources::SkinWeights{});
    const MeshHandle mesh = Mesh(data);
    SceneView view{TestCamera(), {{mesh, glm::mat4(1.0f), Material({1.0f, 1.0f, 1.0f}), 0}}};
    view.jointMatrices = {glm::translate(glm::mat4(1.0f), glm::vec3(0.5f, 0.0f, 0.0f))};
    view.previousJointMatrices = {glm::mat4(1.0f)};
    Render(view);
    EXPECT_NEAR(Motion(37, 32).x, -kHalfMeterInPixels, 0.05f);
    EXPECT_NEAR(Motion(37, 32).y, 0.0f, 0.05f);

    view.previousJointMatrices.clear();
    Render(view);
    EXPECT_EQ(Motion(37, 32), glm::vec2(0.0f)) << "joints without previous matrices stood still";
}

namespace {

// The lit frames, resolved by temporal antialiasing (ADR-0037).
class TemporalTest : public MeshPassTest {
protected:
    void SetUp() override {
        MeshPassTest::SetUp();
        if (IsSkipped() || HasFatalFailure()) {
            return;
        }
        core::Result<renderer::TemporalResolvePass> resolve = renderer::TemporalResolvePass::Create(Device());
        ASSERT_TRUE(resolve.has_value()) << core::Format(resolve.error());
        m_resolve.emplace(std::move(*resolve));
        ASSERT_TRUE(m_resolve->Resize(kExtent).has_value());
        const core::Result<rhi::TextureHandle> reactive =
            Device().CreateTexture({.extent = kExtent,
                                    .format = renderer::ParticlePass::kReactiveFormat,
                                    .usage = rhi::TextureUsage::ColorTarget | rhi::TextureUsage::Sampled,
                                    .name = "Reactive"});
        ASSERT_TRUE(reactive.has_value());
        m_reactive = *reactive;
        const core::Result<rhi::BufferHandle> readback =
            Device().CreateBuffer({.size = std::uint64_t{kExtent.width} * kExtent.height * sizeof(glm::vec4),
                                   .usage = rhi::BufferUsage::TransferDst,
                                   .memory = rhi::MemoryLocation::GpuToCpu,
                                   .name = "Resolved readback"});
        ASSERT_TRUE(readback.has_value());
        m_resolvedReadback = *readback;
    }

    void TearDown() override {
        if (m_resolve.has_value()) {
            m_resolve.reset();
            Device().DestroyTexture(m_reactive);
            Device().DestroyBuffer(m_resolvedReadback);
        }
        MeshPassTest::TearDown();
    }

    // Renders and resolves frames of the view, each with the next jitter of the render scale's cycle.
    void Resolve(const SceneView& view, int frames = 1) {
        const float scale = static_cast<float>(m_renderExtent.width) / static_cast<float>(kExtent.width);
        for (int frame = 0; frame < frames; ++frame) {
            Render(view, renderer::Jitter(m_frame++, renderer::JitterPositions(scale)));
            ASSERT_TRUE(Device()
                            .Submit([&](rhi::CommandList& commands) {
                                renderer::RenderGraph graph;
                                // No particles: the reactive mask is empty.
                                graph.AddPass({.name = "Reactive",
                                               .rendering = rhi::RenderingDesc{.colorTarget = m_reactive,
                                                                               .clearColor = glm::vec4(0.0f)}},
                                              [](rhi::CommandList& /*list*/) {});
                                m_resolve->AddTo(graph, {.frameBuffer = m_pass->FrameDataIndex(),
                                                         .color = m_color,
                                                         .depth = m_depth,
                                                         .motion = m_motion,
                                                         .reactive = m_reactive});
                                graph.Execute(commands);
                                commands.Transition(m_resolve->Output(), rhi::TextureState::TransferSrc);
                                commands.CopyTextureToBuffer(m_resolve->Output(), m_resolvedReadback);
                            })
                            .has_value());
        }
    }

    // The red channel of the last resolved frame along a row, from 0 to 1.
    [[nodiscard]] std::vector<float> ResolvedRow(std::uint32_t y, std::uint32_t first, std::uint32_t last) {
        std::vector<float> values;
        for (std::uint32_t x = first; x <= last; ++x) {
            values.push_back(Resolved(x, y));
        }
        return values;
    }

    // The red channel of the last resolved frame, from 0 to 1.
    [[nodiscard]] float Resolved(std::uint32_t x, std::uint32_t y) {
        glm::vec4 pixel;
        std::memcpy(&pixel,
                    Device().Mapped(m_resolvedReadback).data() + (std::size_t{y} * kExtent.width + x) * sizeof(pixel),
                    sizeof(pixel));
        return pixel.r;
    }

    // The red channel of the last frame before the resolve, from 0 to 1.
    [[nodiscard]] float Current(std::uint32_t x, std::uint32_t y) { return static_cast<float>(Pixel(x, y).r) / 255.0f; }

    // A white wall left of a vertical edge 0.1 pixels right of the center of column 32: 5 of the 8 jitter
    // positions of that column's pixels fall on the wall. Mirrored, the wall is right of the edge.
    SceneView Edge(bool mirrored = false) {
        const float halfWidth = 5.0f * std::tan(glm::radians(30.0f));
        const float edge = ((32.6f / 32.0f) - 1.0f) * halfWidth;
        const MeshData wall =
            mirrored ? MakeQuad(edge, 10.0f, -10.0f, 10.0f, 0.0f) : MakeQuad(-10.0f, edge, -10.0f, 10.0f, 0.0f);
        return {TestCamera(), {{Mesh(wall), glm::mat4(1.0f), m_white}}};
    }

    std::optional<renderer::TemporalResolvePass> m_resolve;
    rhi::TextureHandle m_reactive;
    rhi::BufferHandle m_resolvedReadback;
    std::uint64_t m_frame = 0;
    MaterialHandle m_white;
};

} // namespace

TEST_F(TemporalTest, AFirstFrameIsResolvedAlone) {
    m_white = Material({1.0f, 1.0f, 1.0f});
    Resolve(Edge());
    for (const std::uint32_t x : {20U, 32U, 40U}) {
        EXPECT_NEAR(Resolved(x, 32), Current(x, 32), 0.003f) << x;
    }
}

TEST_F(TemporalTest, AStillEdgeConvergesToItsCoverage) {
    m_white = Material({1.0f, 1.0f, 1.0f});
    const SceneView view = Edge();
    Resolve(view, 32);
    const float wall = Resolved(20, 32);
    ASSERT_GT(wall, 0.2f);
    EXPECT_NEAR(Resolved(31, 32), wall, 0.02f * wall);
    // The resolve averages after a reversible tone map, x / (1 + x), as the screen will show the colors: the
    // edge covers 5/8 of the wall there.
    const auto mapped = [](float value) { return value / (1.0f + value); };
    EXPECT_NEAR(mapped(Resolved(32, 32)) / mapped(wall), 5.0f / 8.0f, 0.1f)
        << "the edge's pixels blend the wall and the background";
    EXPECT_NEAR(Resolved(33, 32), 0.0f, 0.01f);

    // A reset keeps none of the history: this jitter falls on the wall.
    m_resolve->Reset();
    m_frame = 0;
    Resolve(view);
    EXPECT_NEAR(Resolved(32, 32), wall, 0.02f * wall);
}

// Upscaling (ADR-0049): the frames are drawn into a 43 × 43 corner of the 64 × 64 targets, 67%.
constexpr rhi::Extent kUpscaled{43, 43};

TEST_F(TemporalTest, AnUpscaledEdgeConvergesToWhatTheOutputsResolutionShows) {
    m_white = Material({1.0f, 1.0f, 1.0f});
    const SceneView view = Edge();
    Resolve(view, 32);
    const std::vector<float> native = ResolvedRow(32, 26, 38);

    m_resolve->Reset();
    m_frame = 0;
    m_renderExtent = kUpscaled;
    Resolve(view, 64);
    const std::vector<float> upscaled = ResolvedRow(32, 26, 38);
    const auto mapped = [](float value) { return value / (1.0f + value); };
    for (std::size_t index = 0; index < native.size(); ++index) {
        EXPECT_NEAR(mapped(upscaled[index]), mapped(native[index]), 0.1f * mapped(native[0]))
            << "column " << 26 + index;
    }
}

TEST_F(TemporalTest, OnlyTheCornerDrawnIsRead) {
    m_white = Material({1.0f, 1.0f, 1.0f});
    // A full frame leaves the wall on the left, down to the targets' last rows.
    Resolve(Edge(), 4);
    ASSERT_GT(Resolved(10, 60), 0.2f);
    // Mirrored in a corner that changes size every frame, the left is dark: below the corner, the old wall
    // must not be read.
    m_resolve->Reset();
    for (int frame = 0; frame < 8; ++frame) {
        m_renderExtent = frame % 2 == 0 ? kUpscaled : rhi::Extent{50, 50};
        Resolve(Edge(true));
        for (const std::uint32_t y : {10U, 40U, 60U, 63U}) {
            EXPECT_NEAR(Resolved(10, y), 0.0f, 0.01f) << "frame " << frame << ", row " << y;
            EXPECT_GT(Resolved(54, y), 0.2f) << "frame " << frame << ", row " << y;
        }
    }
}

TEST_F(TemporalTest, AnUpscaledMovedObjectLeavesNoTrail) {
    m_white = Material({1.0f, 1.0f, 1.0f});
    m_renderExtent = kUpscaled;
    const MeshHandle background = Mesh(MakeQuad(-10.0f, 10.0f, -10.0f, 10.0f, -1.0f));
    const MeshHandle square = Mesh(MakeQuad(-0.5f, 0.5f, -0.5f, 0.5f, 0.0f));
    const glm::mat4 left = glm::translate(glm::mat4(1.0f), glm::vec3(-1.5f, 0.0f, 0.0f));
    const glm::mat4 right = glm::translate(glm::mat4(1.0f), glm::vec3(1.5f, 0.0f, 0.0f));
    SceneView view{TestCamera(),
                   {{background, glm::mat4(1.0f), Material({0.2f, 0.2f, 0.2f})}, {square, left, m_white}}};
    Resolve(view, 32);
    const float square32 = Resolved(15, 32);
    const float background32 = Resolved(48, 32);
    ASSERT_GT(square32, background32 * 2.0f) << "the square stands out";

    view.draws[1].world = right;
    view.draws[1].previousWorld = left;
    Resolve(view);
    EXPECT_NEAR(Resolved(15, 32), background32, 0.02f) << "where it was, only the background";
    EXPECT_NEAR(Resolved(48, 32), square32, 0.05f) << "it carried its history along";
}

TEST_F(TemporalTest, AMovedObjectLeavesNoTrail) {
    m_white = Material({1.0f, 1.0f, 1.0f});
    const MeshHandle background = Mesh(MakeQuad(-10.0f, 10.0f, -10.0f, 10.0f, -1.0f));
    const MeshHandle square = Mesh(MakeQuad(-0.5f, 0.5f, -0.5f, 0.5f, 0.0f));
    const glm::mat4 left = glm::translate(glm::mat4(1.0f), glm::vec3(-1.5f, 0.0f, 0.0f));
    const glm::mat4 right = glm::translate(glm::mat4(1.0f), glm::vec3(1.5f, 0.0f, 0.0f));
    SceneView view{TestCamera(),
                   {{background, glm::mat4(1.0f), Material({0.2f, 0.2f, 0.2f})}, {square, left, m_white}}};
    Resolve(view, 16);
    const float square16 = Resolved(15, 32);
    const float background16 = Resolved(48, 32);
    ASSERT_GT(square16, background16 * 2.0f) << "the square stands out";

    // It jumps across the screen in one frame, and its motion vectors say so.
    view.draws[1].world = right;
    view.draws[1].previousWorld = left;
    Resolve(view);
    EXPECT_NEAR(Resolved(15, 32), background16, 0.02f) << "where it was, only the background";
    EXPECT_NEAR(Resolved(48, 32), square16, 0.02f) << "it carried its history along";
}

namespace {

// The lit frames, through the denoiser (ADR-0039). The scene of the shadow tests, with a spot light
// of a meter's radius: the panel's shadow on the floor, left of the panel, has a wide, soft edge.
class DenoiserTest : public MeshPassTest {
protected:
    void SetUp() override {
        MeshPassTest::SetUp();
        if (IsSkipped() || HasFatalFailure()) {
            return;
        }
        core::Result<renderer::Denoiser> denoiser = renderer::Denoiser::Create(Device());
        ASSERT_TRUE(denoiser.has_value()) << core::Format(denoiser.error());
        m_denoiser.emplace(std::move(*denoiser));
        ASSERT_TRUE(m_denoiser->Resize(kExtent).has_value());
        m_floor = Mesh(MakeQuad(-4.0f, 4.0f, -4.0f, 4.0f, 0.0f));
        m_panel = Mesh(MakeQuad(-0.5f, 0.5f, -0.5f, 0.5f, 1.0f));
        m_white = Material({1.0f, 1.0f, 1.0f});
    }

    void TearDown() override {
        m_denoiser.reset();
        MeshPassTest::TearDown();
    }

    SceneView ShadowView(float radius) {
        renderer::Light light = ShadowTestLight();
        light.radius = radius;
        SceneView view = LitView(TestCamera(), {m_floor, glm::mat4(1.0f), m_white}, light);
        view.draws.push_back({.mesh = m_panel, .material = m_white});
        return view;
    }

    // Renders frames of the view through the denoiser.
    void Denoise(const SceneView& view, int frames = 1) {
        for (int frame = 0; frame < frames; ++frame) {
            Render(view, glm::vec2(0.0f), &*m_denoiser);
        }
    }

    // The red channel of every pixel of the region where the shadow's left edge falls on the floor.
    [[nodiscard]] std::vector<int> Edge() {
        std::vector<int> values;
        for (std::uint32_t y = kEdgeTop; y < kEdgeBottom; ++y) {
            for (std::uint32_t x = kEdgeLeft; x < kEdgeRight; ++x) {
                values.push_back(Pixel(x, y).r);
            }
        }
        return values;
    }

    static constexpr std::uint32_t kEdgeLeft = 4;
    static constexpr std::uint32_t kEdgeRight = 24;
    static constexpr std::uint32_t kEdgeTop = 26;
    static constexpr std::uint32_t kEdgeBottom = 38;

    std::optional<renderer::Denoiser> m_denoiser;
    MeshHandle m_floor;
    MeshHandle m_panel;
    MaterialHandle m_white;
};

// How many values differ by more than tolerance.
int Differences(const std::vector<int>& first, const std::vector<int>& second, int tolerance) {
    int count = 0;
    for (std::size_t index = 0; index < first.size(); ++index) {
        count += std::abs(first[index] - second[index]) > tolerance ? 1 : 0;
    }
    return count;
}

} // namespace

TEST_F(DenoiserTest, HardShadowsGoThroughUnchanged) {
    // A light without a radius is not noisy: the denoiser must keep its sharp shadow as it is.
    const SceneView view = ShadowView(0.0f);
    Render(view);
    const std::vector<int> raw = Edge();
    Denoise(view);
    EXPECT_EQ(Differences(raw, Edge(), 1), 0) << "the first frame";
    Denoise(view, 8);
    EXPECT_EQ(Differences(raw, Edge(), 1), 0) << "after eight frames";
}

TEST_F(DenoiserTest, ASoftShadowIsNoisyUntilDenoised) {
    const SceneView view = ShadowView(1.0f);
    // Each frame aims its rays at other points of the light: alone, frames differ along the edge.
    Render(view);
    const std::vector<int> first = Edge();
    Render(view);
    const std::vector<int> second = Edge();
    const int rawDifferences = Differences(first, second, 12);
    EXPECT_GT(rawDifferences, 20) << "the shadow's edge is noisy";

    // The average of many frames is the soft shadow without noise: the denoiser comes near it from few.
    std::vector<double> sum(first.size(), 0.0);
    constexpr int kAveraged = 64;
    for (int frame = 0; frame < kAveraged; ++frame) {
        Render(view);
        const std::vector<int> values = Edge();
        for (std::size_t index = 0; index < values.size(); ++index) {
            sum[index] += values[index];
        }
    }
    std::vector<int> average(sum.size());
    int softened = 0;
    for (std::size_t index = 0; index < sum.size(); ++index) {
        average[index] = static_cast<int>(std::lround(sum[index] / kAveraged));
        softened += average[index] > 20 && average[index] < 120 ? 1 : 0;
    }
    EXPECT_GT(softened, 20) << "the shadow has a wide, soft edge";

    Denoise(view, 16);
    const std::vector<int> denoised = Edge();
    double error = 0.0;
    for (std::size_t index = 0; index < denoised.size(); ++index) {
        error += std::abs(denoised[index] - average[index]);
    }
    EXPECT_LT(error / static_cast<double>(denoised.size()), 6.0) << "mean difference from the average, of 255";
    Denoise(view);
    EXPECT_LT(Differences(denoised, Edge(), 12), rawDifferences / 10) << "the next frame barely changes";
}

TEST_F(DenoiserTest, ALightTurnedOffGoesDarkAtOnce) {
    SceneView view = ShadowView(1.0f);
    Denoise(view, 16);
    ASSERT_GT(Pixel(48, 32).r, 100);
    // A light that reaches nothing: without any, the test's light from behind the camera would come on.
    view.lights = {{.position = {0.0f, 0.0f, 100.0f}, .range = 1.0f}};
    Denoise(view);
    // Measured on an RTX 3090: 10, what the history keeps of a penumbra's noise (ADR-0044).
    EXPECT_LT(Pixel(48, 32).r, 16) << "no light lingers in the history";
}

TEST_F(DenoiserTest, AMovedPanelLeavesNoShadowBehind) {
    SceneView view = ShadowView(1.0f);
    Denoise(view, 16);
    ASSERT_LT(Pixel(20, 32).r, 20) << "in the shadow";
    // The panel jumps out of the light's way, and its motion vectors say so.
    const glm::mat4 away = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 6.0f, 0.0f));
    view.draws[1].world = away;
    view.draws[1].previousWorld = glm::mat4(1.0f);
    Denoise(view);
    EXPECT_GT(Pixel(20, 32).r, 100) << "the floor is lit where the shadow was";
}

TEST_F(DenoiserTest, TheHistoryOutlivesAChangeOfRenderSize) {
    // The frame's corner shrinks to 48 × 48, then stays (ADR-0049). Kept across the change, and found where
    // it was, the history makes the first frame after it look as the frames do once settled at that size;
    // lost, it would be raw noise, and read at the wrong place, a shadow moved by a quarter.
    const SceneView view = ShadowView(1.0f);
    Denoise(view, 16);
    m_renderExtent = {48, 48};
    const auto edge = [this] {
        // Edge()'s region, at three quarters of the size.
        std::vector<int> values;
        for (std::uint32_t y = kEdgeTop * 3 / 4; y < kEdgeBottom * 3 / 4; ++y) {
            for (std::uint32_t x = kEdgeLeft * 3 / 4; x < kEdgeRight * 3 / 4; ++x) {
                values.push_back(Pixel(x, y).r);
            }
        }
        return values;
    };
    Render(view);
    const std::vector<int> rawFirst = edge();
    Render(view);
    const int rawDifferences = Differences(rawFirst, edge(), 12);
    ASSERT_GT(rawDifferences, 10) << "the shadow's edge is noisy at this size too";

    m_denoiser->Reset();
    m_renderExtent = kExtent;
    Denoise(view, 16);
    m_renderExtent = {48, 48};
    Denoise(view);
    const std::vector<int> first = edge();
    Denoise(view, 16);
    // Measured on an RTX 3090: 8 of 21 raw; 30 with the history read at the wrong place.
    EXPECT_LT(Differences(first, edge(), 12), rawDifferences / 2);
}

TEST_F(DenoiserTest, AResetForgetsTheHistory) {
    // After a reset, a frame comes out as it would from a denoiser that never saw another. The lighting
    // starts over too, so that both frames get the same light.
    const SceneView view = ShadowView(1.0f);
    Denoise(view, 8);
    m_denoiser->Reset();
    m_lighting->Reset();
    const std::uint32_t frame = m_frameIndex;
    Denoise(view);
    const std::vector<int> afterReset = Edge();

    core::Result<renderer::Denoiser> fresh = renderer::Denoiser::Create(Device());
    ASSERT_TRUE(fresh.has_value() && fresh->Resize(kExtent).has_value());
    m_lighting->Reset();
    m_frameIndex = frame;
    Render(view, glm::vec2(0.0f), &*fresh);
    EXPECT_EQ(Differences(afterReset, Edge(), 0), 0);
}
