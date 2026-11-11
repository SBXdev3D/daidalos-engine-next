// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/renderer/CornellBox.hpp>
#include <daidalos/renderer/Material.hpp>
#include <daidalos/renderer/Primitives.hpp>

#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>

#include <FLIP.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <vector>

#include "Denoiser.hpp"
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

    // What the mesh pass draws of the view, in linear HDR. The view is drawn frames times, each with the
    // next noise (ADR-0039): through the denoiser, the last frame is returned; without, their average.
    Image Rasterize(const SceneView& view, rhi::Extent extent, std::uint32_t frames = 1, bool denoise = false) {
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
        const rhi::TextureHandle motion =
            created(MeshPass::kMotionFormat, rhi::TextureUsage::ColorTarget | rhi::TextureUsage::Sampled, "Motion");
        core::Result<renderer::ShadingTargets> shading = m_pass->CreateShadingTargets(extent);
        EXPECT_TRUE(shading.has_value());
        std::optional<renderer::Denoiser> denoiser;
        if (denoise) {
            core::Result<renderer::Denoiser> made = renderer::Denoiser::Create(Device());
            EXPECT_TRUE(made.has_value() && made->Resize(extent).has_value());
            denoiser.emplace(std::move(*made));
        }
        Image image(std::size_t{extent.width} * extent.height, glm::vec4(0.0f));
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            EXPECT_TRUE(
                Device()
                    .Submit([&](rhi::CommandList& commands) {
                        const core::Result<std::uint32_t> structure = m_scene->Reserve(0, view.draws.size());
                        ASSERT_TRUE(structure.has_value());
                        ASSERT_TRUE(
                            m_pass
                                ->Prepare(view,
                                          {.extent = extent, .accelerationStructure = *structure, .frameIndex = frame})
                                .has_value());
                        ASSERT_TRUE(m_scene->Prepare(m_pass->TracedDraws()).has_value());
                        renderer::RenderGraph graph;
                        m_pass->AddSkinning(graph);
                        m_scene->AddBuild(graph);
                        graph.AddPass({.name = "Prepass",
                                       .rendering = rhi::RenderingDesc{.colorTarget = normals,
                                                                       .clearColor = glm::vec4(0.0f),
                                                                       .moreColorTargets = {{motion, glm::vec4(0.0f)}},
                                                                       .depthTarget = depth,
                                                                       .clearDepth = 0.0f}},
                                      [&](rhi::CommandList& list) { m_pass->RecordPrepass(list); });
                        graph.AddPass({.name = "Meshes",
                                       .rendering = MeshPass::ShadingRendering(*shading, depth, glm::vec4(0.0f)),
                                       .structures = {{m_scene->Structure(), rhi::Access::RayQueryRead}}},
                                      [&](rhi::CommandList& list) { m_pass->Record(list); });
                        if (denoiser.has_value()) {
                            denoiser->AddTo(graph, {.frameBuffer = m_pass->FrameDataIndex(),
                                                    .diffuse = shading->diffuse,
                                                    .specular = shading->specular,
                                                    .depth = depth,
                                                    .normals = normals,
                                                    .motion = motion});
                        }
                        m_pass->AddComposite(
                            graph, color,
                            {.shading = &*shading,
                             .diffuse = denoiser.has_value() ? denoiser->Diffuse() : shading->diffuse,
                             .specular = denoiser.has_value() ? denoiser->Specular() : shading->specular,
                             .depth = depth});
                        graph.Execute(commands);
                    })
                    .has_value());
            const Image drawn = Read(color, extent);
            for (std::size_t index = 0; index < image.size(); ++index) {
                image[index] = denoise ? drawn[index] : image[index] + drawn[index] / static_cast<float>(frames);
            }
        }
        denoiser.reset();
        m_pass->DestroyShadingTargets(*shading);
        Device().DestroyTexture(color);
        Device().DestroyTexture(depth);
        Device().DestroyTexture(normals);
        Device().DestroyTexture(motion);
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

TEST_F(PathTracerTest, SkyLightsWhatItSurrounds) {
    // The furnace again, with a white sky cube instead of a white background: what leaves the scene
    // finds the sky, and the sky shows where nothing is.
    MaterialDesc matte;
    matte.metallicFactor = 0.0f;
    matte.roughnessFactor = 1.0f;
    const core::Result<rhi::TextureHandle> sky =
        MakeSky(Device(), {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF});
    ASSERT_TRUE(sky.has_value()) << core::Format(sky.error());
    SceneView view = CubeInFront(Material(matte));
    view.atmosphere.sky = *sky;
    const Image image = Trace(view, kSmall, {.maxBounces = 8, .seed = 3}, 256);
    ExpectFurnace(image, 0.88f);
    Device().DestroyTexture(*sky);
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

namespace {

// The mean FLIP error of an image against a reference (Andersson et al. 2020), for HDR images: 0 when
// they look the same, 1 when they could not differ more.
float Flip(const Image& reference, const Image& test, rhi::Extent extent) {
    std::vector<float> referenceColors;
    std::vector<float> testColors;
    for (std::size_t index = 0; index < reference.size(); ++index) {
        for (int channel = 0; channel < 3; ++channel) {
            referenceColors.push_back(reference[index][channel]);
            testColors.push_back(test[index][channel]);
        }
    }
    FLIP::Parameters parameters;
    float mean = 0.0f;
    float* errors = nullptr;
    FLIP::evaluate(referenceColors.data(), testColors.data(), static_cast<int>(extent.width),
                   static_cast<int>(extent.height), true, parameters, false, true, mean, &errors);
    delete[] errors;
    return mean;
}

} // namespace

TEST_F(PathTracerTest, RayTracedShadowsMatchThePathTracer) {
    // The Cornell box's direct light, from the mesh pass and from one bounce of the path tracer. Without
    // shadows, the path tracer's image is far from the mesh pass's: the score tells shadows apart.
    const renderer::CornellBox box = renderer::MakeCornellBox();
    SceneView view;
    view.camera = LookAt(box.eye, box.target, box.verticalFov);
    view.atmosphere = Dark();
    view.lights.push_back(box.light);
    for (const renderer::CornellPart& part : box.parts) {
        view.draws.push_back({.mesh = m_cube, .world = part.world, .material = Material(part.material)});
    }
    constexpr rhi::Extent kExtent{96, 96};
    const Image raster = Rasterize(view, kExtent);
    // Through the pixels' centers, as the mesh pass samples them: what differs is shading, not edges.
    const Image traced = Trace(view, kExtent, {.maxBounces = 1, .seed = 11, .jitter = false}, 1);
    SceneView unshadowed = view;
    for (renderer::DrawItem& draw : unshadowed.draws) {
        draw.castsShadows = false;
    }
    const Image tracedWithoutShadows = Trace(unshadowed, kExtent, {.maxBounces = 1, .seed = 11, .jitter = false}, 1);

    const float withShadows = Flip(traced, raster, kExtent);
    const float withoutShadows = Flip(tracedWithoutShadows, raster, kExtent);
    RecordProperty("flip", std::to_string(withShadows));
    RecordProperty("flipWithoutShadows", std::to_string(withoutShadows));
    // Measured on an RTX 3090: 0.014, and 0.113 without shadows.
    EXPECT_LT(withShadows, 0.03f);
    EXPECT_GT(withoutShadows, 4.0f * withShadows);
}

TEST_F(PathTracerTest, DenoisedSoftShadowsMatchThePathTracer) {
    // The Cornell box's direct light, from a light of 30 cm radius: the blocks' shadows are soft. The path
    // tracer averages many points of the light; the mesh pass aims at one per frame (ADR-0039). The average
    // of its frames is the path tracer's image, and the denoiser comes near it from a few frames. The hard
    // shadows of a point light are far from it.
    const renderer::CornellBox box = renderer::MakeCornellBox();
    SceneView view;
    view.camera = LookAt(box.eye, box.target, box.verticalFov);
    view.atmosphere = Dark();
    view.lights.push_back(box.light);
    view.lights.back().radius = 0.3f;
    for (const renderer::CornellPart& part : box.parts) {
        view.draws.push_back({.mesh = m_cube, .world = part.world, .material = Material(part.material)});
    }
    constexpr rhi::Extent kExtent{96, 96};
    const Image traced = Trace(view, kExtent, {.maxBounces = 1, .seed = 13, .jitter = false}, 256);
    const Image averaged = Rasterize(view, kExtent, 256);
    const Image denoised = Rasterize(view, kExtent, 32, true);
    const Image noisy = Rasterize(view, kExtent);
    SceneView hard = view;
    hard.lights.back().radius = 0.0f;
    const Image hardShadows = Rasterize(hard, kExtent);

    const float averagedFlip = Flip(traced, averaged, kExtent);
    const float denoisedFlip = Flip(traced, denoised, kExtent);
    const float noisyFlip = Flip(traced, noisy, kExtent);
    const float hardFlip = Flip(traced, hardShadows, kExtent);
    RecordProperty("flipAveraged", std::to_string(averagedFlip));
    RecordProperty("flip", std::to_string(denoisedFlip));
    RecordProperty("flipNoisy", std::to_string(noisyFlip));
    RecordProperty("flipHard", std::to_string(hardFlip));
    // Measured on an RTX 3090: 0.025 for the average, 0.032 denoised, 0.084 for one frame, 0.083 for hard
    // shadows. The average still differs where a pixel's center falls on the seam of two walls.
    EXPECT_LT(averagedFlip, 0.03f);
    EXPECT_LT(denoisedFlip, 0.04f);
    EXPECT_LT(denoisedFlip, noisyFlip);
    EXPECT_LT(denoisedFlip, hardFlip);
}
