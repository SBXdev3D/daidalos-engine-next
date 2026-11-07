// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/renderer/CornellBox.hpp>
#include <daidalos/renderer/Primitives.hpp>

#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <vector>

#include "GpuFixture.hpp"
#include "MeshPass.hpp"
#include "PathTracerPass.hpp"
#include "RayTracingScene.hpp"
#include "RenderGraph.hpp"

using namespace daidalos;
using gpu_tests::GpuTest;
using renderer::MaterialDesc;
using renderer::MeshPass;
using renderer::PathTracerPass;
using renderer::PathTracerSettings;
using renderer::RayTracingScene;
using renderer::SceneView;

namespace {

constexpr rhi::Format kColorFormat = rhi::Format::Rgba32Float;
constexpr rhi::Format kDepthFormat = rhi::Format::D32Float;

using Image = std::vector<glm::vec4>;

float Luminance(const glm::vec4& color) {
    return 0.2126f * color.r + 0.7152f * color.g + 0.0722f * color.b;
}

renderer::CameraView LookAt(const glm::vec3& eye, const glm::vec3& target, float verticalFov) {
    return {glm::lookAtRH(eye, target, glm::vec3(0.0f, 1.0f, 0.0f)), verticalFov, 0.1f};
}

// Light only from the frame's lights: no ambient light, no fog.
renderer::Atmosphere Dark() {
    return {
        .fogColor = glm::vec3(0.0f), .fogDensity = 0.0f, .ambient = glm::vec3(0.0f), .ambientBelow = glm::vec3(0.0f)};
}

class PathTracerTest : public GpuTest {
protected:
    void SetUp() override {
        GpuTest::SetUp();
        if (IsSkipped() || HasFatalFailure()) {
            return;
        }
        core::Result<MeshPass> pass = MeshPass::Create(Device(), kColorFormat, kDepthFormat);
        ASSERT_TRUE(pass.has_value()) << core::Format(pass.error());
        m_pass.emplace(std::move(*pass));
        core::Result<RayTracingScene> scene = RayTracingScene::Create(Device(), kColorFormat);
        ASSERT_TRUE(scene.has_value()) << core::Format(scene.error());
        m_scene.emplace(std::move(*scene));
        core::Result<PathTracerPass> tracer = PathTracerPass::Create(Device());
        ASSERT_TRUE(tracer.has_value()) << core::Format(tracer.error());
        m_tracer.emplace(std::move(*tracer));
        const core::Result<renderer::MeshHandle> cube = m_pass->CreateMesh(renderer::MakeCube(), "Cube");
        ASSERT_TRUE(cube.has_value());
        m_cube = *cube;
    }

    void TearDown() override {
        m_tracer.reset();
        m_scene.reset();
        m_pass.reset();
        GpuTest::TearDown();
    }

    renderer::MaterialHandle Material(const MaterialDesc& desc) {
        const core::Result<renderer::MaterialHandle> material = m_pass->CreateMaterial(desc);
        EXPECT_TRUE(material.has_value());
        return material.value_or(renderer::MaterialHandle{});
    }

    // Copies a texture of extent, four floats per pixel, back to the CPU. It must be TransferSrc already.
    Image Read(rhi::TextureHandle texture, rhi::Extent extent) {
        const core::Result<rhi::BufferHandle> readback =
            Device().CreateBuffer({.size = std::uint64_t{extent.width} * extent.height * sizeof(glm::vec4),
                                   .usage = rhi::BufferUsage::TransferDst,
                                   .memory = rhi::MemoryLocation::GpuToCpu,
                                   .name = "Readback"});
        EXPECT_TRUE(readback.has_value());
        EXPECT_TRUE(Device()
                        .Submit([&](rhi::CommandList& commands) {
                            commands.Transition(texture, rhi::TextureState::TransferSrc);
                            commands.CopyTextureToBuffer(texture, *readback);
                        })
                        .has_value());
        Image image(std::size_t{extent.width} * extent.height);
        std::memcpy(image.data(), Device().Mapped(*readback).data(), image.size() * sizeof(glm::vec4));
        Device().DestroyBuffer(*readback);
        return image;
    }

    // Averages samples paths per pixel of the view.
    Image Trace(const SceneView& view, rhi::Extent extent, const PathTracerSettings& settings, std::uint32_t samples) {
        EXPECT_TRUE(m_tracer->Resize(extent).has_value());
        m_tracer->Reset();
        for (std::uint32_t sample = 0; sample < samples; ++sample) {
            EXPECT_TRUE(
                Device()
                    .Submit([&](rhi::CommandList& commands) {
                        const core::Result<std::uint32_t> structure = m_scene->Reserve(0, view.draws.size());
                        ASSERT_TRUE(structure.has_value());
                        ASSERT_TRUE(
                            m_pass->Prepare(view, {.extent = extent, .accelerationStructure = *structure}).has_value());
                        ASSERT_TRUE(m_scene->Prepare(m_pass->TracedDraws()).has_value());
                        renderer::RenderGraph graph;
                        m_pass->AddSkinning(graph);
                        m_scene->AddBuild(graph);
                        m_tracer->AddTo(graph,
                                        {.frameBuffer = m_pass->FrameDataIndex(),
                                         .instanceBuffer = m_pass->InstanceDataIndex(),
                                         .materialBuffer = m_pass->MaterialDataIndex(),
                                         .structure = m_scene->Structure(),
                                         .skinnedVertices = m_pass->SkinnedVertices()},
                                        settings);
                        graph.Execute(commands);
                    })
                    .has_value());
        }
        EXPECT_EQ(m_tracer->Samples(), samples);
        return Read(m_tracer->Output(), extent);
    }

    // What the mesh pass draws of the view, in linear HDR.
    Image Rasterize(const SceneView& view, rhi::Extent extent) {
        const auto created = [&](rhi::Format format, rhi::TextureUsage usage, const char* name) {
            const core::Result<rhi::TextureHandle> texture =
                Device().CreateTexture({.extent = extent, .format = format, .usage = usage, .name = name});
            EXPECT_TRUE(texture.has_value());
            return texture.value_or(rhi::TextureHandle{});
        };
        const rhi::TextureHandle color =
            created(kColorFormat, rhi::TextureUsage::ColorTarget | rhi::TextureUsage::TransferSrc, "Color");
        const rhi::TextureHandle depth =
            created(kDepthFormat, rhi::TextureUsage::DepthTarget | rhi::TextureUsage::Sampled, "Depth");
        const rhi::TextureHandle normals =
            created(MeshPass::kNormalFormat, rhi::TextureUsage::ColorTarget | rhi::TextureUsage::Sampled, "Normals");
        EXPECT_TRUE(Device()
                        .Submit([&](rhi::CommandList& commands) {
                            ASSERT_TRUE(m_pass->Prepare(view, {.extent = extent}).has_value());
                            renderer::RenderGraph graph;
                            graph.AddPass({.name = "Prepass",
                                           .rendering = rhi::RenderingDesc{.colorTarget = normals,
                                                                           .clearColor = glm::vec4(0.0f),
                                                                           .depthTarget = depth,
                                                                           .clearDepth = 0.0f}},
                                          [&](rhi::CommandList& list) { m_pass->RecordPrepass(list); });
                            graph.AddPass({.name = "Meshes",
                                           .rendering = rhi::RenderingDesc{.colorTarget = color,
                                                                           .clearColor = glm::vec4(0.0f),
                                                                           .depthTarget = depth},
                                           .textures = {{m_pass->ShadowAtlas(), rhi::Access::FragmentShaderRead}}},
                                          [&](rhi::CommandList& list) { m_pass->Record(list); });
                            graph.Execute(commands);
                        })
                        .has_value());
        Image image = Read(color, extent);
        Device().DestroyTexture(color);
        Device().DestroyTexture(depth);
        Device().DestroyTexture(normals);
        return image;
    }

    // A cube a meter across, 3 m in front of a camera that it fills half of.
    SceneView CubeInFront(renderer::MaterialHandle material) {
        SceneView view;
        view.camera = LookAt(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f), glm::radians(38.0f));
        view.atmosphere = Dark();
        // Turned, so that the camera sees three faces at different angles.
        glm::mat4 world = glm::rotate(glm::mat4(1.0f), glm::radians(30.0f), glm::vec3(1.0f, 0.0f, 0.0f));
        world = glm::rotate(world, glm::radians(40.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        view.draws.push_back({.mesh = m_cube, .world = world, .material = material});
        return view;
    }

    // The Cornell box, 256 paths per pixel of up to four bounces, with a fixed seed.
    Image TraceCornellBox() {
        const renderer::CornellBox box = renderer::MakeCornellBox();
        SceneView view;
        view.camera = LookAt(box.eye, box.target, box.verticalFov);
        view.atmosphere = Dark();
        view.lights.push_back(box.light);
        for (const renderer::CornellPart& part : box.parts) {
            view.draws.push_back({.mesh = m_cube, .world = part.world, .material = Material(part.material)});
        }
        return Trace(view, kCornellExtent, {.maxBounces = 4, .seed = 1}, 256);
    }

    static std::filesystem::path CornellBoxReference() {
        return std::filesystem::path(DAI_TEST_ASSET_DIR) / "golden" / "cornell-48.bin";
    }

    static constexpr rhi::Extent kCornellExtent{48, 48};

    std::optional<MeshPass> m_pass;
    std::optional<RayTracingScene> m_scene;
    std::optional<PathTracerPass> m_tracer;
    renderer::MeshHandle m_cube;
};

constexpr rhi::Extent kSmall{32, 32};

// A white surface in a white sky reflects as much light as it receives, whatever its shape, if its BSDF
// keeps energy: the furnace test. Single-scattering microfacets lose a little at grazing angles, which
// the bounds allow; a wrong probability or a missing factor of pi would fall far outside them.
void ExpectFurnace(const Image& image, float lowest) {
    EXPECT_NEAR(image.front().r, 1.0f, 1e-4f) << "The sky, in a corner";
    const glm::vec4& center = image[(kSmall.height / 2) * kSmall.width + kSmall.width / 2];
    for (int channel = 0; channel < 3; ++channel) {
        EXPECT_GE(center[channel], lowest);
        EXPECT_LE(center[channel], 1.02f) << "A surface cannot reflect more than it receives";
    }
}

} // namespace

TEST_F(PathTracerTest, WhiteMatteSurfaceInAWhiteSkyKeepsItsEnergy) {
    MaterialDesc matte;
    matte.metallicFactor = 0.0f;
    matte.roughnessFactor = 1.0f;
    const Image image =
        Trace(CubeInFront(Material(matte)), kSmall, {.maxBounces = 8, .background = glm::vec3(1.0f), .seed = 3}, 256);
    ExpectFurnace(image, 0.88f);
}

TEST_F(PathTracerTest, WhiteMetalInAWhiteSkyKeepsItsEnergy) {
    MaterialDesc metal;
    metal.metallicFactor = 1.0f;
    metal.roughnessFactor = 0.2f;
    const Image image =
        Trace(CubeInFront(Material(metal)), kSmall, {.maxBounces = 8, .background = glm::vec3(1.0f), .seed = 5}, 256);
    ExpectFurnace(image, 0.95f);
}

TEST_F(PathTracerTest, DirectLightMatchesTheMeshPass) {
    // A glossy dielectric lit by a point light and a spot light, without shadows or anything else: one
    // bounce of the path tracer is the mesh pass's lighting, with the same BSDF and the same falloff.
    MaterialDesc glossy;
    glossy.baseColorFactor = glm::vec4(0.8f, 0.5f, 0.3f, 1.0f);
    glossy.metallicFactor = 0.0f;
    glossy.roughnessFactor = 0.4f;
    SceneView view = CubeInFront(Material(glossy));
    view.lights.push_back({.type = renderer::LightType::Point,
                           .position = {1.5f, 1.0f, 2.0f},
                           .color = {1.0f, 0.9f, 0.8f},
                           .range = 6.0f,
                           .falloff = 0.3f});
    view.lights.push_back({.type = renderer::LightType::Spot,
                           .position = {-1.5f, 1.5f, 1.5f},
                           .direction = glm::normalize(glm::vec3(1.5f, -1.5f, -1.5f)),
                           .color = {0.3f, 0.5f, 1.0f},
                           .range = 6.0f,
                           .falloff = 0.5f,
                           .innerAngle = glm::radians(20.0f),
                           .outerAngle = glm::radians(35.0f)});
    constexpr rhi::Extent kExtent{64, 64};
    const Image raster = Rasterize(view, kExtent);
    const Image traced = Trace(view, kExtent, {.maxBounces = 1, .seed = 7}, 64);

    // The path tracer averages light over each pixel, and the mesh pass shades its center: they differ
    // where a pixel spans two faces or the silhouette. Pixels whose neighbors are all lit must agree.
    const auto lit = [&](std::uint32_t x, std::uint32_t y) { return Luminance(raster[y * kExtent.width + x]) > 0.02f; };
    double rasterTotal = 0.0;
    double tracedTotal = 0.0;
    std::vector<float> errors;
    for (std::uint32_t y = 1; y + 1 < kExtent.height; ++y) {
        for (std::uint32_t x = 1; x + 1 < kExtent.width; ++x) {
            if (!(lit(x - 1, y - 1) && lit(x, y - 1) && lit(x + 1, y - 1) && lit(x - 1, y) && lit(x, y) &&
                  lit(x + 1, y) && lit(x - 1, y + 1) && lit(x, y + 1) && lit(x + 1, y + 1))) {
                continue;
            }
            const float expected = Luminance(raster[y * kExtent.width + x]);
            const float actual = Luminance(traced[y * kExtent.width + x]);
            rasterTotal += expected;
            tracedTotal += actual;
            errors.push_back(std::abs(actual - expected) / expected);
        }
    }
    ASSERT_GT(errors.size(), 500U) << "The cube covers much of the view";
    std::ranges::sort(errors);
    EXPECT_NEAR(tracedTotal / rasterTotal, 1.0, 0.005);
    EXPECT_LT(errors[errors.size() / 2], 0.01f) << "Median relative difference";
    EXPECT_LT(errors[errors.size() * 9 / 10], 0.05f) << "90th percentile of the relative differences";
}

TEST_F(PathTracerTest, CornellBoxMatchesItsReference) {
    // A regression test: the path tracer's Cornell box, compared with an image it made before. After a
    // change meant to alter it, write a new one with DISABLED_WriteCornellBoxReference.
    const Image image = TraceCornellBox();
    const std::filesystem::path golden = CornellBoxReference();
    Image reference(image.size());
    std::ifstream file(golden, std::ios::binary);
    ASSERT_TRUE(file) << "Missing " << golden.generic_string();
    file.read(reinterpret_cast<char*>(reference.data()),
              static_cast<std::streamsize>(reference.size() * sizeof(glm::vec4)));
    ASSERT_TRUE(file);

    // Another driver rounds differently, and the paths soon go elsewhere: the noise differs, the image
    // must not. The average brightness and the average difference are checked, per channel.
    for (int channel = 0; channel < 3; ++channel) {
        double total = 0.0;
        double referenceTotal = 0.0;
        double difference = 0.0;
        for (std::size_t index = 0; index < image.size(); ++index) {
            total += image[index][channel];
            referenceTotal += reference[index][channel];
            difference += std::abs(image[index][channel] - reference[index][channel]);
        }
        ASSERT_GT(referenceTotal, 0.0);
        EXPECT_NEAR(total / referenceTotal, 1.0, 0.02) << "Channel " << channel;
        EXPECT_LT(difference / referenceTotal, 0.15) << "Channel " << channel;
    }
    // The walls keep their colors: red on the left, green on the right.
    const glm::vec4& left = image[(kCornellExtent.height / 2) * kCornellExtent.width + 3];
    const glm::vec4& right = image[(kCornellExtent.height / 2) * kCornellExtent.width + kCornellExtent.width - 4];
    EXPECT_GT(left.r, 2.0f * left.g);
    EXPECT_GT(right.g, 2.0f * right.r);
}

// Run with --gtest_also_run_disabled_tests --gtest_filter=*WriteCornellBoxReference.
TEST_F(PathTracerTest, DISABLED_WriteCornellBoxReference) {
    const Image image = TraceCornellBox();
    const std::filesystem::path golden = CornellBoxReference();
    std::filesystem::create_directories(golden.parent_path());
    std::ofstream(golden, std::ios::binary)
        .write(reinterpret_cast<const char*>(image.data()),
               static_cast<std::streamsize>(image.size() * sizeof(glm::vec4)));
}
