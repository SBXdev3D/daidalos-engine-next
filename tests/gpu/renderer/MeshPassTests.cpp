// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

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
                                    ->Prepare(view, {.extent = kExtent,
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
                    }
                    const rhi::BufferUse skinned{m_pass->SkinnedVertices(), rhi::Access::VertexInputRead};
                    graph.AddPass(
                        {.name = "Prepass",
                         .rendering = rhi::RenderingDesc{.colorTarget = m_normals,
                                                         .clearColor = glm::vec4(0.0f),
                                                         .moreColorTargets = {{m_motion, glm::vec4(0.0f)}},
                                                         .depthTarget = m_depth,
                                                         .clearDepth = 0.0f},
                         .buffers = {skinned, {m_pass->PreviousSkinnedPositions(), rhi::Access::VertexShaderRead}}},
                        [&](rhi::CommandList& list) { m_pass->RecordPrepass(list); });
                    graph.AddPass({.name = "Meshes",
                                   .rendering = MeshPass::ShadingRendering(m_shading, m_depth, kBackground),
                                   .buffers = {skinned, {skinned.buffer, rhi::Access::FragmentShaderRead}},
                                   .structures = {{traced, rhi::Access::RayQueryRead}}},
                                  [&](rhi::CommandList& list) { m_pass->Record(list); });
                    if (denoiser != nullptr && view.camera.has_value()) {
                        denoiser->AddTo(graph, {.frameBuffer = m_pass->FrameDataIndex(),
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
    renderer::ShadingTargets m_shading;
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
    // The panel's alpha is below its cutoff everywhere: the mesh pass does not draw it, and shadow rays go
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

// A particle pass drawing over the mesh pass's frame, with a plain white texture.
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
            Device().DestroyTexture(m_white);
            Device().DestroyTexture(m_reactive);
        }
        MeshPassTest::TearDown();
    }

    // An opaque square of one meter, unlit, that stays where it appears for ten seconds.
    renderer::ParticleEffectHandle Effect(const glm::vec3& color) {
        core::Result<renderer::ParticleEffectHandle> effect =
            m_particles->CreateEffect({.texture = m_white,
                                       .startColor = glm::vec4(color, 1.0f),
                                       .endColor = glm::vec4(color, 1.0f),
                                       .lit = false,
                                       .lifetime = {10.0f, 10.0f},
                                       .size = {1.0f, 1.0f},
                                       .softness = 0.001f});
        EXPECT_TRUE(effect.has_value());
        return effect.value_or(renderer::ParticleEffectHandle{});
    }

    // Renders the meshes of the view, then simulates and draws the particles over them.
    void RenderWithParticles(const SceneView& view, std::span<const renderer::ParticleSpawn> spawns, float step) {
        Render(view);
        ASSERT_TRUE(
            Device()
                .Submit([&](rhi::CommandList& commands) {
                    renderer::RenderGraph graph;
                    EXPECT_TRUE(
                        m_particles->AddSimulation(graph, spawns, step, m_pass->FrameDataIndex(), m_depth, m_normals)
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

    std::optional<renderer::ParticlePass> m_particles;
    rhi::TextureHandle m_white;
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
    // The vertex shader of the mesh pass, which the pre-pass shares, and a pixel shader of its own, which
    // writes green and no light to the mesh pass's targets.
    std::ofstream(source) << R"(
#include "MeshCommon.slangh"
struct Output {
    float4 base : SV_Target0;
    float4 diffuse : SV_Target1;
    float4 specular : SV_Target2;
    float4 albedo : SV_Target3;
};
Output PSMain(VertexOutput input) {
    Output output;
    output.base = float4(0.0, 1.0, 0.0, 1.0);
    output.diffuse = float4(0.0, 0.0, 0.0, 0.0);
    output.specular = float4(0.0, 0.0, 0.0, 0.0);
    output.albedo = float4(0.0, 0.0, 0.0, 0.0);
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

// The mesh pass's frames, resolved by temporal antialiasing (ADR-0037).
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

    // Renders and resolves frames of the view, each with the next jitter.
    void Resolve(const SceneView& view, int frames = 1) {
        for (int frame = 0; frame < frames; ++frame) {
            Render(view, renderer::Jitter(m_frame++));
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
    // positions of that column's pixels fall on the wall.
    SceneView Edge() {
        const float halfWidth = 5.0f * std::tan(glm::radians(30.0f));
        const float edge = ((32.6f / 32.0f) - 1.0f) * halfWidth;
        return {TestCamera(), {{Mesh(MakeQuad(-10.0f, edge, -10.0f, 10.0f, 0.0f)), glm::mat4(1.0f), m_white}}};
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

// The mesh pass's frames, through the denoiser (ADR-0039). The scene of the shadow tests, with a spot light
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
    EXPECT_LT(Pixel(48, 32).r, 10) << "no light lingers in the history";
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

TEST_F(DenoiserTest, AResetForgetsTheHistory) {
    // After a reset, a frame comes out as it would from a denoiser that never saw another.
    const SceneView view = ShadowView(1.0f);
    Denoise(view, 8);
    m_denoiser->Reset();
    const std::uint32_t frame = m_frameIndex;
    Denoise(view);
    const std::vector<int> afterReset = Edge();

    core::Result<renderer::Denoiser> fresh = renderer::Denoiser::Create(Device());
    ASSERT_TRUE(fresh.has_value() && fresh->Resize(kExtent).has_value());
    m_frameIndex = frame;
    Render(view, glm::vec2(0.0f), &*fresh);
    EXPECT_EQ(Differences(afterReset, Edge(), 0), 0);
}
