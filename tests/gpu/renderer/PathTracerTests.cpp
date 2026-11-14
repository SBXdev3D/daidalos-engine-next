// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/renderer/CornellBox.hpp>
#include <daidalos/renderer/DarkCorridor.hpp>
#include <daidalos/renderer/LightUnits.hpp>
#include <daidalos/renderer/Material.hpp>
#include <daidalos/renderer/MirrorRoom.hpp>
#include <daidalos/renderer/Primitives.hpp>

#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/packing.hpp>

#include <FLIP.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <random>
#include <vector>

#include "Denoiser.hpp"
#include "DirectLighting.hpp"
#include "DynamicResolution.hpp"
#include "GlobalIllumination.hpp"
#include "GpuFixture.hpp"
#include "MeshPass.hpp"
#include "ParticlePass.hpp"
#include "PathTracerPass.hpp"
#include "RayTracingScene.hpp"
#include "RenderGraph.hpp"
#include "TemporalResolvePass.hpp"

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

// Light only from the frame's lights: no fog. Exposed as a dim interior, for which the Cornell box and the
// mirror room are lit (ADR-0043).
renderer::Atmosphere Dark() {
    return {.fogColor = glm::vec3(0.0f), .fogDensity = 0.0f, .ev100 = 3.0f};
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
            EXPECT_TRUE(Device()
                            .Submit([&](rhi::CommandList& commands) {
                                const core::Result<std::uint32_t> structure = m_scene->Reserve(0, view.draws.size());
                                ASSERT_TRUE(structure.has_value());
                                ASSERT_TRUE(
                                    m_pass
                                        ->Prepare(view, {.extent = extent,
                                                         .accelerationStructure = *structure,
                                                         .exposure = renderer::ExposureScale(view.atmosphere.ev100)})
                                        .has_value());
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

    struct RasterOptions {
        std::uint32_t frames = 1;
        bool denoise = false;
        /// Indirect light from this global illumination, whose cache the frames fill (ADR-0040).
        renderer::GlobalIllumination* indirect = nullptr;
        /// Frames drawn first and left out of the average, while the cache fills.
        std::uint32_t warmup = 0;
        /// With indirect: reflections traced along each pixel's specular lobe (ADR-0042).
        bool reflections = false;
        /// With reflections: the denoiser follows them with their hit distance.
        bool reprojectReflections = true;
        /// Changes the view before each frame is drawn, warmup included.
        std::function<void(SceneView&, std::uint32_t)> animate;
        /// How ReSTIR DI reuses its samples (ADR-0044).
        renderer::DirectLightingSettings restir;
        /// With indirect: how its light is traced (ADR-0045).
        renderer::GlobalIlluminationSettings gi;
        /// Called with each frame drawn after the warmup, through the denoiser if there is one, and its number.
        std::function<void(const Image&, std::uint32_t)> eachFrame;
        /// Each frame is jittered and resolved by temporal antialiasing, which upscales it from this scale of
        /// the extent (ADR-0049); the last frame resolved is returned.
        bool resolve = false;
        float renderScale = 1.0f;
    };

    // What the G-buffer and the lighting draw of the view, in linear HDR. The view is drawn frames times, each with the
    // next noise (ADR-0039): through the denoiser, the last frame is returned; without, their average.
    Image Rasterize(const SceneView& view, rhi::Extent extent, const RasterOptions& options = {}) {
        const std::uint32_t frames = options.frames;
        const bool denoise = options.denoise;
        renderer::GlobalIllumination* const indirect = options.indirect;
        // The frame draws the top-left corner of its targets, which are the extent (ADR-0049).
        const rhi::Extent renderExtent = options.resolve ? renderer::RenderExtent(extent, options.renderScale) : extent;
        const float renderScale = static_cast<float>(renderExtent.width) / static_cast<float>(extent.width);
        if (indirect != nullptr) {
            EXPECT_TRUE(indirect->Resize(extent).has_value());
        }
        const auto created = [&](rhi::Format format, rhi::TextureUsage usage, const char* name) {
            const core::Result<rhi::TextureHandle> texture =
                Device().CreateTexture({.extent = extent, .format = format, .usage = usage, .name = name});
            EXPECT_TRUE(texture.has_value());
            return texture.value_or(rhi::TextureHandle{});
        };
        const rhi::TextureHandle color = created(
            kColorFormat, rhi::TextureUsage::ColorTarget | rhi::TextureUsage::TransferSrc | rhi::TextureUsage::Sampled,
            "Color");
        const rhi::TextureHandle reactive =
            created(renderer::ParticlePass::kReactiveFormat,
                    rhi::TextureUsage::ColorTarget | rhi::TextureUsage::Sampled, "Reactive");
        std::optional<renderer::TemporalResolvePass> resolver;
        if (options.resolve) {
            core::Result<renderer::TemporalResolvePass> made = renderer::TemporalResolvePass::Create(Device());
            EXPECT_TRUE(made.has_value() && made->Resize(extent).has_value());
            resolver.emplace(std::move(*made));
        }
        const rhi::TextureHandle depth =
            created(kDepthFormat, rhi::TextureUsage::DepthTarget | rhi::TextureUsage::Sampled, "Depth");
        const rhi::TextureHandle normals =
            created(MeshPass::kNormalFormat, rhi::TextureUsage::ColorTarget | rhi::TextureUsage::Sampled, "Normals");
        const rhi::TextureHandle motion =
            created(MeshPass::kMotionFormat, rhi::TextureUsage::ColorTarget | rhi::TextureUsage::Sampled, "Motion");
        const rhi::TextureHandle background =
            created(kColorFormat, rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDst, "Behind glass");
        const rhi::TextureHandle reflections =
            indirect != nullptr && options.reflections ? indirect->Reflections() : rhi::TextureHandle{};
        SceneView animated = view;
        core::Result<renderer::ShadingTargets> shading = m_pass->CreateShadingTargets(extent);
        EXPECT_TRUE(shading.has_value());
        std::optional<renderer::DirectLighting> lighting;
        if (core::Result<renderer::DirectLighting> made = renderer::DirectLighting::Create(Device());
            made.has_value() && made->Resize(extent).has_value()) {
            lighting.emplace(std::move(*made));
        }
        EXPECT_TRUE(lighting.has_value());
        std::optional<renderer::Denoiser> denoiser;
        if (denoise) {
            core::Result<renderer::Denoiser> made = renderer::Denoiser::Create(Device());
            EXPECT_TRUE(made.has_value() && made->Resize(extent).has_value());
            denoiser.emplace(std::move(*made));
        }
        Image image(std::size_t{extent.width} * extent.height, glm::vec4(0.0f));
        for (std::uint32_t drawn = 0; drawn < options.warmup + frames; ++drawn) {
            const std::uint32_t frame = m_frameIndex++;
            if (options.animate) {
                options.animate(animated, drawn);
            }
            EXPECT_TRUE(
                Device()
                    .Submit([&](rhi::CommandList& commands) {
                        const core::Result<std::uint32_t> structure = m_scene->Reserve(0, animated.draws.size());
                        ASSERT_TRUE(structure.has_value());
                        ASSERT_TRUE(
                            m_pass
                                ->Prepare(
                                    animated,
                                    {.extent = renderExtent,
                                     .targetExtent = extent,
                                     .mipBias = std::log2(renderScale),
                                     .indirect = indirect != nullptr ? indirect->Output() : rhi::TextureHandle{},
                                     .accelerationStructure = *structure,
                                     .jitter = options.resolve
                                                   ? renderer::Jitter(drawn, renderer::JitterPositions(renderScale))
                                                   : glm::vec2(0.0f),
                                     .frameIndex = frame,
                                     .reflections = reflections,
                                     .background = background,
                                     .depth = depth,
                                     .exposure = renderer::ExposureScale(animated.atmosphere.ev100),
                                     .sharpReflections =
                                         reflections ? renderer::GlobalIllumination::kSharpRoughness : 0.0f})
                                .has_value());
                        ASSERT_TRUE(m_scene->Prepare(m_pass->TracedDraws()).has_value());
                        renderer::RenderGraph graph;
                        m_pass->AddSkinning(graph);
                        m_scene->AddBuild(graph);
                        m_pass->AddEmissiveLights(graph);
                        graph.AddPass(
                            {.name = "G-buffer",
                             .rendering = MeshPass::GBufferRendering(*shading, normals, motion, depth, renderExtent)},
                            [&](rhi::CommandList& list) { m_pass->RecordGBuffer(list); });
                        if (indirect != nullptr) {
                            indirect->AddTo(graph, {.extent = renderExtent,
                                                    .frameBuffer = m_pass->FrameDataIndex(),
                                                    .instanceBuffer = m_pass->InstanceDataIndex(),
                                                    .materialBuffer = m_pass->MaterialDataIndex(),
                                                    .structure = m_scene->Structure(),
                                                    .emissiveTriangles = m_pass->EmissiveTriangles(),
                                                    .depth = depth,
                                                    .normals = normals,
                                                    .frameIndex = frame,
                                                    .reflections = options.reflections,
                                                    .settings = options.gi});
                        }
                        lighting->AddTo(graph,
                                        {.extent = renderExtent,
                                         .frameBuffer = m_pass->FrameDataIndex(),
                                         .instanceBuffer = m_pass->InstanceDataIndex(),
                                         .materialBuffer = m_pass->MaterialDataIndex(),
                                         .structure = m_scene->Structure(),
                                         .emissiveTriangles = m_pass->EmissiveTriangles(),
                                         .depth = depth,
                                         .normals = normals,
                                         .motion = motion,
                                         .baseColor = shading->baseColor,
                                         .emission = shading->emission,
                                         .indirect = indirect != nullptr ? indirect->Output() : rhi::TextureHandle{},
                                         .reflections = reflections,
                                         .diffuse = shading->diffuse,
                                         .specular = shading->specular,
                                         .lightMap = m_pass->LightMapIndex(),
                                         .previousLightCount = m_pass->PreviousLightCount(),
                                         .frameIndex = frame},
                                        options.restir);
                        if (denoiser.has_value()) {
                            denoiser->AddTo(
                                graph,
                                {.extent = renderExtent,
                                 .frameBuffer = m_pass->FrameDataIndex(),
                                 .diffuse = shading->diffuse,
                                 .specular = shading->specular,
                                 .depth = depth,
                                 .normals = normals,
                                 .motion = motion,
                                 .reflections = options.reprojectReflections ? reflections : rhi::TextureHandle{}});
                        }
                        m_pass->AddComposite(
                            graph, color,
                            {.shading = &*shading,
                             .diffuse = denoiser.has_value() ? denoiser->Diffuse() : shading->diffuse,
                             .specular = denoiser.has_value() ? denoiser->Specular() : shading->specular,
                             .depth = depth});
                        if (m_pass->HasGlass()) {
                            graph.AddPass({.name = "Behind glass",
                                           .textures = {{color, rhi::Access::TransferRead},
                                                        {background, rhi::Access::TransferWrite, true}}},
                                          [&](rhi::CommandList& list) { list.CopyTexture(color, background); });
                            graph.AddPass({.name = "Glass",
                                           .rendering = rhi::RenderingDesc{.colorTarget = color, .area = renderExtent},
                                           .textures = {{color, rhi::Access::ColorTarget},
                                                        {background, rhi::Access::FragmentShaderRead},
                                                        {depth, rhi::Access::FragmentShaderRead}},
                                           .buffers = {{m_pass->EmissiveTriangles(), rhi::Access::FragmentShaderRead}},
                                           .structures = {{m_scene->Structure(), rhi::Access::RayQueryRead}}},
                                          [&](rhi::CommandList& list) { m_pass->RecordGlass(list); });
                        }
                        if (resolver.has_value()) {
                            graph.AddPass({.name = "Reactive",
                                           .rendering = rhi::RenderingDesc{.colorTarget = reactive,
                                                                           .clearColor = glm::vec4(0.0f)}},
                                          [](rhi::CommandList& /*list*/) {});
                            resolver->AddTo(graph, {.frameBuffer = m_pass->FrameDataIndex(),
                                                    .color = color,
                                                    .depth = depth,
                                                    .motion = motion,
                                                    .reactive = reactive});
                        }
                        graph.Execute(commands);
                    })
                    .has_value());
            if (drawn < options.warmup) {
                continue;
            }
            const Image drawnImage = Read(resolver.has_value() ? resolver->Output() : color, extent);
            if (options.eachFrame) {
                options.eachFrame(drawnImage, drawn - options.warmup);
            }
            for (std::size_t index = 0; index < image.size(); ++index) {
                image[index] = denoise || options.resolve
                                   ? drawnImage[index]
                                   : image[index] + drawnImage[index] / static_cast<float>(frames);
            }
        }
        resolver.reset();
        Device().DestroyTexture(reactive);
        denoiser.reset();
        lighting.reset();
        m_pass->DestroyShadingTargets(*shading);
        Device().DestroyTexture(color);
        Device().DestroyTexture(depth);
        Device().DestroyTexture(normals);
        Device().DestroyTexture(motion);
        Device().DestroyTexture(background);
        // What was destroyed goes away, its storage image slots with it, so that a test can draw many times.
        EXPECT_TRUE(Device().WaitIdle().has_value());
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
    /// Counts the frames rasterized: the noise of each differs.
    std::uint32_t m_frameIndex = 0;
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
    // 9.6 nits at EV100 3: 1 on the screen, as the white background of the other furnaces (ADR-0043).
    view.atmosphere.skyIntensity = 9.6f;
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

class DirectLightTest : public PathTracerTest, public ::testing::WithParamInterface<renderer::LightType> {};

TEST_P(DirectLightTest, DirectLightMatchesThePathTracer) {
    // A glossy dielectric lit by one light, without shadows or anything else: one bounce of the path tracer is
    // the lighting pass's, with the same BSDF and the same falloff. With one light, ReSTIR always chooses it,
    // and its weight is exactly one (ADR-0044): the light is as exact as when every light was summed.
    MaterialDesc glossy;
    glossy.baseColorFactor = glm::vec4(0.8f, 0.5f, 0.3f, 1.0f);
    glossy.metallicFactor = 0.0f;
    glossy.roughnessFactor = 0.4f;
    SceneView view = CubeInFront(Material(glossy));
    if (GetParam() == renderer::LightType::Point) {
        view.lights.push_back({.type = renderer::LightType::Point,
                               .position = {1.5f, 1.0f, 2.0f},
                               .color = glm::vec3(1.0f, 0.9f, 0.8f) * 200.0f,
                               .range = 6.0f});
    } else {
        view.lights.push_back({.type = renderer::LightType::Spot,
                               .position = {-1.5f, 1.5f, 1.5f},
                               .direction = glm::normalize(glm::vec3(1.5f, -1.5f, -1.5f)),
                               .color = glm::vec3(0.3f, 0.5f, 1.0f) * 200.0f,
                               .range = 6.0f,
                               .innerAngle = glm::radians(20.0f),
                               .outerAngle = glm::radians(35.0f)});
    }
    constexpr rhi::Extent kExtent{64, 64};
    const Image raster = Rasterize(view, kExtent);
    const Image traced = Trace(view, kExtent, {.maxBounces = 1, .seed = 7}, 64);

    // The path tracer averages light over each pixel, and the lighting pass shades its center: they differ
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
    ASSERT_GT(errors.size(), 300U) << "The light covers much of the cube";
    std::ranges::sort(errors);
    // The path tracer averages each pixel, the lighting shades its center: with the inverse square of the
    // distance (ADR-0043), light changes faster across a pixel, and the two differ by about half a percent.
    EXPECT_NEAR(tracedTotal / rasterTotal, 1.0, 0.01);
    EXPECT_LT(errors[errors.size() / 2], 0.01f) << "Median relative difference";
    EXPECT_LT(errors[errors.size() * 9 / 10], 0.05f) << "90th percentile of the relative differences";
}

INSTANTIATE_TEST_SUITE_P(EachKind, DirectLightTest,
                         ::testing::Values(renderer::LightType::Point, renderer::LightType::Spot),
                         [](const ::testing::TestParamInfo<renderer::LightType>& info) {
                             return info.param == renderer::LightType::Point ? std::string("Point")
                                                                             : std::string("Spot");
                         });

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
    // The Cornell box's direct light, from the lighting pass and from one bounce of the path tracer. Without
    // shadows, the path tracer's image is far from the lighting's: the score tells shadows apart.
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
    // Through the pixels' centers, as the lighting samples them: what differs is shading, not edges.
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
    // Measured on an RTX 3090: 0.014, and 0.112 without shadows.
    EXPECT_LT(withShadows, 0.03f);
    EXPECT_GT(withoutShadows, 4.0f * withShadows);
}

TEST_F(PathTracerTest, DenoisedSoftShadowsMatchThePathTracer) {
    // The Cornell box's direct light, from a light of 30 cm radius: the blocks' shadows are soft. The path
    // tracer averages many points of the light; the lighting pass aims at one per frame (ADR-0039). The
    // average of its frames, unbiased (ADR-0044), is the path tracer's image, and the denoiser comes near it
    // from a few frames: nearer unbiased, since ReSTIR's default reuses its neighbours' points of the light
    // without testing them for shadows. The hard shadows of a point light are far from it.
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
    const Image averaged = Rasterize(view, kExtent, {.frames = 256, .restir = {.unbiased = true}});
    const Image denoised = Rasterize(view, kExtent, {.frames = 32, .denoise = true});
    const Image denoisedUnbiased =
        Rasterize(view, kExtent, {.frames = 32, .denoise = true, .restir = {.unbiased = true}});
    const Image noisy = Rasterize(view, kExtent);
    SceneView hard = view;
    hard.lights.back().radius = 0.0f;
    const Image hardShadows = Rasterize(hard, kExtent);

    const float averagedFlip = Flip(traced, averaged, kExtent);
    const float denoisedFlip = Flip(traced, denoised, kExtent);
    const float unbiasedFlip = Flip(traced, denoisedUnbiased, kExtent);
    const float noisyFlip = Flip(traced, noisy, kExtent);
    const float hardFlip = Flip(traced, hardShadows, kExtent);
    RecordProperty("flipAveraged", std::to_string(averagedFlip));
    RecordProperty("flip", std::to_string(denoisedFlip));
    RecordProperty("flipUnbiased", std::to_string(unbiasedFlip));
    RecordProperty("flipNoisy", std::to_string(noisyFlip));
    RecordProperty("flipHard", std::to_string(hardFlip));
    // Measured on an RTX 3090: 0.021 for the average, 0.038 denoised, 0.029 denoised unbiased, 0.067 for one
    // frame, 0.070 for hard shadows. The average still differs where a pixel's center falls on the seam of
    // two walls.
    EXPECT_LT(averagedFlip, 0.03f);
    EXPECT_LT(denoisedFlip, 0.05f);
    EXPECT_LT(unbiasedFlip, 0.04f);
    EXPECT_LT(unbiasedFlip, denoisedFlip);
    EXPECT_LT(denoisedFlip, noisyFlip);
    EXPECT_LT(denoisedFlip, hardFlip);
}

namespace {

double TotalLuminance(const Image& image) {
    double total = 0.0;
    for (const glm::vec4& pixel : image) {
        total += Luminance(pixel);
    }
    return total;
}

} // namespace

TEST_F(PathTracerTest, IndirectLightMatchesThePathTracerInTheCornellBox) {
    // The Cornell box with its light bouncing: the path tracer follows each path through eight surfaces, and
    // the lighting adds the indirect light of global illumination (ADR-0040). Direct light alone misses
    // most of the image: the ceiling, the shadows, the colors the walls throw on the blocks.
    const renderer::CornellBox box = renderer::MakeCornellBox();
    SceneView view;
    view.camera = LookAt(box.eye, box.target, box.verticalFov);
    view.atmosphere = Dark();
    view.lights.push_back(box.light);
    for (const renderer::CornellPart& part : box.parts) {
        view.draws.push_back({.mesh = m_cube, .world = part.world, .material = Material(part.material)});
    }
    constexpr rhi::Extent kExtent{96, 96};
    const Image traced = Trace(view, kExtent, {.maxBounces = 8, .seed = 17, .jitter = false}, 512);
    const Image direct = Rasterize(view, kExtent);
    core::Result<renderer::GlobalIllumination> created = renderer::GlobalIllumination::Create(Device());
    ASSERT_TRUE(created.has_value()) << core::Format(created.error());
    renderer::GlobalIllumination& indirect = *created;
    const Image averaged = Rasterize(view, kExtent, {.frames = 128, .indirect = &indirect, .warmup = 64});
    const Image denoised = Rasterize(view, kExtent, {.frames = 32, .denoise = true, .indirect = &indirect});

    const double brightness = TotalLuminance(averaged) / TotalLuminance(traced);
    const float averagedFlip = Flip(traced, averaged, kExtent);
    const float denoisedFlip = Flip(traced, denoised, kExtent);
    const float directFlip = Flip(traced, direct, kExtent);
    RecordProperty("brightness", std::to_string(brightness));
    RecordProperty("flipAveraged", std::to_string(averagedFlip));
    RecordProperty("flip", std::to_string(denoisedFlip));
    RecordProperty("flipDirect", std::to_string(directFlip));
    // Measured on an RTX 3090: brightness 1.027, FLIP 0.117 averaged, 0.123 denoised, 0.68 direct light only;
    // reading the cache where each ray first hit, as before ADR-0045: 0.150 and 0.159.
    EXPECT_NEAR(brightness, 1.0, 0.1);
    EXPECT_LT(averagedFlip, 0.14f);
    EXPECT_LT(denoisedFlip, 0.14f);
    EXPECT_LT(averagedFlip, 0.5f * directFlip);
    EXPECT_LT(denoisedFlip, 0.5f * directFlip);
    // The red wall throws red on the white ceiling near it, the green one green.
    const glm::vec4& nearRed = averaged[6 * kExtent.width + 14];
    const glm::vec4& nearGreen = averaged[6 * kExtent.width + kExtent.width - 15];
    EXPECT_GT(nearRed.r / nearRed.g, nearGreen.r / nearGreen.g);
}

namespace {

// Two rooms of 2 by 2 by 2 meters side by side, closed on every side, parted by a wall 2 cm thick at
// x = 0.03: the left room has a light, the right one none.
struct TwoRooms {
    std::vector<renderer::DrawItem> draws;
    renderer::Light light;
};

TwoRooms MakeTwoRooms(renderer::MeshHandle cube, renderer::MaterialHandle white) {
    const auto slab = [&](const glm::vec3& center, const glm::vec3& size) {
        return renderer::DrawItem{
            .mesh = cube, .world = glm::scale(glm::translate(glm::mat4(1.0f), center), size), .material = white};
    };
    TwoRooms rooms;
    // The walls go into each other, so that no ray finds a seam between them.
    rooms.draws = {slab({0.0f, -0.05f, 0.0f}, {4.4f, 0.1f, 2.4f}), slab({0.0f, 2.05f, 0.0f}, {4.4f, 0.1f, 2.4f}),
                   slab({-2.05f, 1.0f, 0.0f}, {0.1f, 2.2f, 2.4f}), slab({2.05f, 1.0f, 0.0f}, {0.1f, 2.2f, 2.4f}),
                   slab({0.0f, 1.0f, -1.05f}, {4.2f, 2.2f, 0.1f}), slab({0.0f, 1.0f, 1.05f}, {4.2f, 2.2f, 0.1f}),
                   slab({0.03f, 1.0f, 0.0f}, {0.02f, 2.2f, 2.2f})};
    rooms.light = {.position = {-1.0f, 1.5f, 0.0f}, .color = glm::vec3(100.0f), .range = 6.0f};
    return rooms;
}

} // namespace

namespace {

// The Cornell box without its lamp, lit by a panel that glows under most of its ceiling (ADR-0044): large, so
// that the path tracer's rays find it often.
SceneView CornellBoxWithPanel(renderer::MeshHandle cube,
                              const std::function<renderer::MaterialHandle(const MaterialDesc&)>& material,
                              bool light) {
    const renderer::CornellBox box = renderer::MakeCornellBox();
    SceneView view;
    view.camera = LookAt(box.eye, box.target, box.verticalFov);
    view.atmosphere = Dark();
    for (const renderer::CornellPart& part : box.parts) {
        view.draws.push_back({.mesh = cube, .world = part.world, .material = material(part.material)});
    }
    MaterialDesc glow;
    glow.baseColorFactor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    glow.metallicFactor = 0.0f;
    glow.emissiveFactor = glm::vec3(150.0f);
    glow.emissiveLight = light;
    const float ceiling = box.light.position.y + 0.25f;
    view.draws.push_back(
        {.mesh = cube,
         .world = glm::scale(glm::translate(glm::mat4(1.0f), {0.0f, ceiling - 0.02f, 0.0f}), {3.0f, 0.02f, 3.0f}),
         .material = material(glow),
         .id = 1});
    return view;
}

} // namespace

TEST_F(PathTracerTest, AnEmissivePanelLightsAsThePathTracerSees) {
    // The panel's triangles are lights: the lighting pass samples them, and two bounces of the path tracer
    // find the panel by hitting it, which counts each once. Averaged, unbiased, they agree; denoised, nearly.
    // The panel's emission is read back a few frames after it is first drawn: those frames are left out.
    const SceneView view =
        CornellBoxWithPanel(m_cube, [this](const MaterialDesc& desc) { return Material(desc); }, true);
    constexpr rhi::Extent kExtent{64, 64};
    const Image traced = Trace(view, kExtent, {.maxBounces = 2, .seed = 17, .jitter = false}, 4096);
    const Image averaged = Rasterize(view, kExtent, {.frames = 128, .warmup = 4, .restir = {.unbiased = true}});
    const Image denoised = Rasterize(view, kExtent, {.frames = 32, .denoise = true, .warmup = 4});
    const float averagedFlip = Flip(traced, averaged, kExtent);
    const float denoisedFlip = Flip(traced, denoised, kExtent);
    RecordProperty("flipAveraged", std::to_string(averagedFlip));
    RecordProperty("flip", std::to_string(denoisedFlip));
    // Measured on an RTX 3090: 0.087 for the average, 0.126 denoised; two path traces of other seeds differ
    // by 0.062.
    EXPECT_NEAR(TotalLuminance(averaged) / TotalLuminance(traced), 1.0, 0.05);
    EXPECT_LT(averagedFlip, 0.1f);
    EXPECT_LT(denoisedFlip, 0.15f);
}

TEST_F(PathTracerTest, EmissionThatIsNotALightIsOnlySeen) {
    // The same panel, whose material's extras say it is no light: it glows, and lights nothing.
    constexpr rhi::Extent kExtent{64, 64};
    const auto material = [this](const MaterialDesc& desc) { return Material(desc); };
    const Image lit = Rasterize(CornellBoxWithPanel(m_cube, material, true), kExtent, {.frames = 4, .warmup = 4});
    const Image seen = Rasterize(CornellBoxWithPanel(m_cube, material, false), kExtent, {.frames = 4, .warmup = 4});
    float panel = 0.0f;
    for (const glm::vec4& pixel : seen) {
        panel = std::max(panel, Luminance(pixel));
    }
    ASSERT_GT(panel, 1.0f) << "the panel shows";
    const auto litPixels = [&](const Image& image) {
        return std::ranges::count_if(
            image, [&](const glm::vec4& pixel) { return Luminance(pixel) > 0.01f && Luminance(pixel) < 0.5f * panel; });
    };
    EXPECT_GT(litPixels(lit), 1000) << "a light lights the room";
    EXPECT_EQ(litPixels(seen), 0) << "only the panel shows";
}

namespace {

// A room six meters across lit by a thousand small lamps of random colors, and two panels that glow on its
// side walls, the right one no light (ADR-0044).
SceneView RoomOfLights(renderer::MeshHandle cube,
                       const std::function<renderer::MaterialHandle(const MaterialDesc&)>& material) {
    MaterialDesc matte;
    matte.baseColorFactor = glm::vec4(0.7f, 0.7f, 0.7f, 1.0f);
    matte.metallicFactor = 0.0f;
    const renderer::MaterialHandle walls = material(matte);
    const auto slab = [&](const glm::vec3& center, const glm::vec3& size, renderer::MaterialHandle surface) {
        return renderer::DrawItem{
            .mesh = cube, .world = glm::scale(glm::translate(glm::mat4(1.0f), center), size), .material = surface};
    };
    SceneView view;
    view.camera = LookAt({0.0f, 1.6f, 2.6f}, {0.0f, 1.2f, -3.0f}, glm::radians(70.0f));
    view.atmosphere = Dark();
    view.draws = {
        slab({0.0f, -0.05f, 0.0f}, {6.2f, 0.1f, 6.2f}, walls), slab({0.0f, 3.05f, 0.0f}, {6.2f, 0.1f, 6.2f}, walls),
        slab({-3.05f, 1.5f, 0.0f}, {0.1f, 3.0f, 6.2f}, walls), slab({3.05f, 1.5f, 0.0f}, {0.1f, 3.0f, 6.2f}, walls),
        slab({0.0f, 1.5f, -3.05f}, {6.0f, 3.0f, 0.1f}, walls), slab({0.0f, 1.5f, 3.05f}, {6.0f, 3.0f, 0.1f}, walls)};
    MaterialDesc glow;
    glow.baseColorFactor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    glow.metallicFactor = 0.0f;
    glow.emissiveFactor = glm::vec3(60.0f, 120.0f, 200.0f);
    view.draws.push_back(slab({-2.99f, 1.5f, -1.0f}, {0.02f, 1.0f, 1.5f}, material(glow)));
    glow.emissiveFactor = glm::vec3(200.0f, 80.0f, 40.0f);
    glow.emissiveLight = false;
    view.draws.push_back(slab({2.99f, 1.5f, -1.0f}, {0.02f, 1.0f, 1.5f}, material(glow)));
    std::mt19937 random(1000);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    for (int lamp = 0; lamp < 1000; ++lamp) {
        const glm::vec3 position(-2.8f + 5.6f * unit(random), 0.2f + 2.6f * unit(random), -2.8f + 5.6f * unit(random));
        const glm::vec3 color(unit(random), unit(random), unit(random));
        view.lights.push_back({.position = position, .color = color * 0.4f, .range = 3.0f, .radius = 0.05f});
    }
    return view;
}

} // namespace

TEST_F(PathTracerTest, ARoomOfAThousandLightsMatchesThePathTracer) {
    // ReSTIR DI casts one shadow ray per pixel whatever the number of lights, and reuses what its neighbours
    // and its past found (ADR-0044): its frames, averaged, are as bright as the path tracer's direct light;
    // one frame is less noisy with spatial reuse than without, and the denoiser comes near the path tracer.
    // Testing the reused samples for shadows, unbiased, changes little.
    const SceneView view = RoomOfLights(m_cube, [this](const MaterialDesc& desc) { return Material(desc); });
    constexpr rhi::Extent kExtent{64, 64};
    const Image traced = Trace(view, kExtent, {.maxBounces = 2, .seed = 31, .jitter = false, .directOnly = true}, 256);
    const auto denoised = [&](renderer::DirectLightingSettings restir) {
        return Rasterize(view, kExtent, {.frames = 32, .denoise = true, .warmup = 4, .restir = restir});
    };
    const float biasedFlip = Flip(traced, denoised({}), kExtent);
    const float withoutSpatialFlip = Flip(traced, denoised({.spatial = false}), kExtent);
    const float unbiasedFlip = Flip(traced, denoised({.unbiased = true}), kExtent);
    const Image averaged = Rasterize(view, kExtent, {.frames = 128, .warmup = 4, .restir = {.unbiased = true}});
    const float averagedFlip = Flip(traced, averaged, kExtent);
    // One frame as ReSTIR leaves it, before the denoiser, after a few frames of history.
    const float noisyFlip = Flip(traced, Rasterize(view, kExtent, {.warmup = 8}), kExtent);
    const float noisyWithoutSpatialFlip =
        Flip(traced, Rasterize(view, kExtent, {.warmup = 8, .restir = {.spatial = false}}), kExtent);
    const float referenceFlip = Flip(
        traced, Trace(view, kExtent, {.maxBounces = 2, .seed = 37, .jitter = false, .directOnly = true}, 256), kExtent);
    RecordProperty("flip", std::to_string(biasedFlip));
    RecordProperty("flipWithoutSpatialReuse", std::to_string(withoutSpatialFlip));
    RecordProperty("flipUnbiased", std::to_string(unbiasedFlip));
    RecordProperty("flipAveraged", std::to_string(averagedFlip));
    RecordProperty("flipNoisy", std::to_string(noisyFlip));
    RecordProperty("flipNoisyWithoutSpatialReuse", std::to_string(noisyWithoutSpatialFlip));
    RecordProperty("flipTwoReferences", std::to_string(referenceFlip));
    // Measured on an RTX 3090: denoised 0.224, 0.225 without spatial reuse, 0.227 unbiased; averaged 0.165;
    // one frame 0.356, 0.379 without spatial reuse. Two path traces of other seeds differ by 0.100.
    EXPECT_NEAR(TotalLuminance(averaged) / TotalLuminance(traced), 1.0, 0.03);
    EXPECT_LT(averagedFlip, 0.25f);
    EXPECT_LT(biasedFlip, 0.3f);
    EXPECT_LT(unbiasedFlip, 0.3f);
    EXPECT_LT(noisyFlip, noisyWithoutSpatialFlip);
}

TEST_F(PathTracerTest, DISABLED_TimeTheDirectLightOfAThousandLightsAt1080p) {
    // Not a test: the GPU time of the G-buffer and of ReSTIR DI at 1920 x 1080 in the room of a thousand
    // lights, through frames with timestamps (ADR-0044 sets a budget of 1 ms for ReSTIR DI on an RTX 3090).
    const SceneView view = RoomOfLights(m_cube, [this](const MaterialDesc& desc) { return Material(desc); });
    constexpr rhi::Extent kExtent{1920, 1080};
    const auto created = [&](rhi::Format format, rhi::TextureUsage usage, const char* name) {
        const core::Result<rhi::TextureHandle> texture =
            Device().CreateTexture({.extent = kExtent, .format = format, .usage = usage, .name = name});
        EXPECT_TRUE(texture.has_value());
        return texture.value_or(rhi::TextureHandle{});
    };
    const rhi::TextureHandle depth =
        created(kDepthFormat, rhi::TextureUsage::DepthTarget | rhi::TextureUsage::Sampled, "Depth");
    const rhi::TextureHandle normals =
        created(MeshPass::kNormalFormat, rhi::TextureUsage::ColorTarget | rhi::TextureUsage::Sampled, "Normals");
    const rhi::TextureHandle motion =
        created(MeshPass::kMotionFormat, rhi::TextureUsage::ColorTarget | rhi::TextureUsage::Sampled, "Motion");
    core::Result<renderer::ShadingTargets> shading = m_pass->CreateShadingTargets(kExtent);
    ASSERT_TRUE(shading.has_value());
    std::optional<renderer::DirectLighting> lighting;
    if (core::Result<renderer::DirectLighting> made = renderer::DirectLighting::Create(Device());
        made.has_value() && made->Resize(kExtent).has_value()) {
        lighting.emplace(std::move(*made));
    }
    ASSERT_TRUE(lighting.has_value());

    constexpr std::uint32_t kWarmup = 16;
    constexpr std::uint32_t kTimed = 128;
    std::array<std::vector<std::string_view>, rhi::kFramesInFlight> groups;
    std::map<std::string, double> totals;
    std::uint32_t timed = 0;
    for (std::uint32_t frame = 0; frame < kWarmup + kTimed; ++frame) {
        core::Result<std::optional<rhi::Frame>> begun = Device().BeginFrame();
        ASSERT_TRUE(begun.has_value() && begun->has_value());
        rhi::Frame& current = **begun;
        // The times of the last frame recorded in this slot.
        const std::span<const double> stamps = Device().CompletedTimestamps();
        if (frame >= kWarmup && stamps.size() == groups[current.slot].size() + 1) {
            for (std::size_t group = 0; group < groups[current.slot].size(); ++group) {
                totals[std::string(groups[current.slot][group])] += stamps[group + 1] - stamps[group];
            }
            ++timed;
        }
        const core::Result<std::uint32_t> structure = m_scene->Reserve(current.slot, view.draws.size());
        ASSERT_TRUE(structure.has_value());
        ASSERT_TRUE(m_pass
                        ->Prepare(view, {.extent = kExtent,
                                         .accelerationStructure = *structure,
                                         .frameIndex = frame,
                                         .exposure = renderer::ExposureScale(view.atmosphere.ev100)})
                        .has_value());
        ASSERT_TRUE(m_scene->Prepare(m_pass->TracedDraws()).has_value());
        renderer::RenderGraph graph;
        m_scene->AddBuild(graph);
        m_pass->AddEmissiveLights(graph);
        graph.AddPass({.name = "G-buffer", .rendering = MeshPass::GBufferRendering(*shading, normals, motion, depth)},
                      [&](rhi::CommandList& list) { m_pass->RecordGBuffer(list); });
        lighting->AddTo(graph,
                        {.frameBuffer = m_pass->FrameDataIndex(),
                         .instanceBuffer = m_pass->InstanceDataIndex(),
                         .materialBuffer = m_pass->MaterialDataIndex(),
                         .structure = m_scene->Structure(),
                         .emissiveTriangles = m_pass->EmissiveTriangles(),
                         .depth = depth,
                         .normals = normals,
                         .motion = motion,
                         .baseColor = shading->baseColor,
                         .emission = shading->emission,
                         .diffuse = shading->diffuse,
                         .specular = shading->specular,
                         .lightMap = m_pass->LightMapIndex(),
                         .previousLightCount = m_pass->PreviousLightCount(),
                         .frameIndex = frame},
                        {});
        groups[current.slot] = graph.Execute(current.commands);
        ASSERT_TRUE(Device().EndFrame().has_value());
    }
    ASSERT_TRUE(Device().WaitIdle().has_value());
    for (const auto& [group, total] : totals) {
        const double milliseconds = total / std::max(timed, 1U);
        std::printf("%s: %.3f ms\n", group.c_str(), milliseconds);
        RecordProperty(group, std::to_string(milliseconds));
    }
    lighting.reset();
    m_pass->DestroyShadingTargets(*shading);
    for (const rhi::TextureHandle texture : {depth, normals, motion}) {
        Device().DestroyTexture(texture);
    }
}

TEST_F(PathTracerTest, NoLightLeaksThroughAThinWall) {
    // The cache's cells are 10 cm across here, wider than the wall: they hold the light of each orientation
    // apart, so the two faces of the wall never share it. First the camera fills the cache in the lit room,
    // then it looks at the wall from the dark one, whose faces no light can reach.
    MaterialDesc matte;
    matte.baseColorFactor = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
    matte.metallicFactor = 0.0f;
    const TwoRooms rooms = MakeTwoRooms(m_cube, Material(matte));
    SceneView view;
    view.atmosphere = Dark();
    view.draws = rooms.draws;
    view.lights.push_back(rooms.light);
    core::Result<renderer::GlobalIllumination> created = renderer::GlobalIllumination::Create(Device());
    ASSERT_TRUE(created.has_value()) << core::Format(created.error());
    renderer::GlobalIllumination& indirect = *created;
    constexpr rhi::Extent kExtent{64, 64};

    view.camera = LookAt({-1.8f, 1.0f, 0.8f}, {0.0f, 0.6f, -0.3f}, glm::radians(70.0f));
    const Image litRoom = Rasterize(view, kExtent, {.frames = 1, .indirect = &indirect, .warmup = 32});
    ASSERT_GT(TotalLuminance(litRoom), 100.0) << "The lit room is lit";

    // The right room, looking at the wall and the floor along it: the cache keeps the left room's cells.
    view.camera = LookAt({1.8f, 1.2f, 0.8f}, {0.0f, 0.6f, -0.3f}, glm::radians(70.0f));
    const Image darkRoom = Rasterize(view, kExtent, {.frames = 16, .indirect = &indirect, .warmup = 8});
    float brightest = 0.0f;
    for (const glm::vec4& pixel : darkRoom) {
        brightest = std::max(brightest, Luminance(pixel));
    }
    RecordProperty("brightest", std::to_string(brightest));
    EXPECT_EQ(brightest, 0.0f);
}

TEST_F(PathTracerTest, IndirectLightFollowsALightTurnedOff) {
    // The cache forgets: half a second after the light goes off, at 60 frames per second, little of its
    // bounced light is left. Light that bounces off the cache's own light fades more slowly where surfaces
    // reflect much of it: here they reflect half.
    MaterialDesc matte;
    matte.baseColorFactor = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
    matte.metallicFactor = 0.0f;
    const TwoRooms rooms = MakeTwoRooms(m_cube, Material(matte));
    SceneView view;
    view.atmosphere = Dark();
    view.draws = rooms.draws;
    view.lights.push_back(rooms.light);
    view.camera = LookAt({-1.8f, 1.0f, 0.8f}, {-0.5f, 0.6f, -0.3f}, glm::radians(70.0f));
    core::Result<renderer::GlobalIllumination> created = renderer::GlobalIllumination::Create(Device());
    ASSERT_TRUE(created.has_value()) << core::Format(created.error());
    renderer::GlobalIllumination& indirect = *created;
    constexpr rhi::Extent kExtent{64, 64};
    const double lit = TotalLuminance(Rasterize(view, kExtent, {.frames = 8, .indirect = &indirect, .warmup = 32}));

    // A light that reaches nothing: no direct light is left, only what the cache holds.
    view.lights = {{.position = {0.0f, 100.0f, 0.0f}, .range = 1.0f}};
    const double after = TotalLuminance(Rasterize(view, kExtent, {.frames = 1, .indirect = &indirect, .warmup = 30}));
    RecordProperty("remaining", std::to_string(after / lit));
    // Measured on an RTX 3090: 0.026; 0.048 when each pixel's ray read the cache where it hit, before
    // ADR-0045. The light that reaches the first surface along the ray goes off at once; what the cache
    // holds still fades.
    EXPECT_LT(after, 0.05 * lit);
}

namespace {

// The mirror room (ADR-0042), as a view: its parts, its lights and its camera; with or without its glass.
SceneView MirrorRoomView(const renderer::MirrorRoom& room, renderer::MeshHandle cube,
                         const std::function<renderer::MaterialHandle(const MaterialDesc&)>& material, bool glass) {
    SceneView view;
    view.camera = LookAt(room.eye, room.target, room.verticalFov);
    view.atmosphere = Dark();
    view.lights = room.lights;
    for (const renderer::CornellPart& part : room.parts) {
        if (glass || part.material.transmission <= 0.0f) {
            view.draws.push_back({.mesh = cube, .world = part.world, .material = material(part.material)});
        }
    }
    return view;
}

// Copies a half-float texture of extent back to the CPU, as floats. It must be TransferSrc already.
Image ReadHalf(rhi::Device& device, rhi::TextureHandle texture, rhi::Extent extent) {
    const std::size_t count = std::size_t{extent.width} * extent.height;
    const core::Result<rhi::BufferHandle> readback = device.CreateBuffer({.size = count * 4 * sizeof(std::uint16_t),
                                                                          .usage = rhi::BufferUsage::TransferDst,
                                                                          .memory = rhi::MemoryLocation::GpuToCpu,
                                                                          .name = "Readback"});
    EXPECT_TRUE(readback.has_value());
    EXPECT_TRUE(device
                    .Submit([&](rhi::CommandList& commands) {
                        commands.Transition(texture, rhi::TextureState::TransferSrc);
                        commands.CopyTextureToBuffer(texture, *readback);
                    })
                    .has_value());
    std::vector<std::uint16_t> halves(count * 4);
    std::memcpy(halves.data(), device.Mapped(*readback).data(), halves.size() * sizeof(std::uint16_t));
    device.DestroyBuffer(*readback);
    Image image(count);
    for (std::size_t index = 0; index < count; ++index) {
        for (int channel = 0; channel < 4; ++channel) {
            image[index][channel] = glm::unpackHalf1x16(halves[index * 4 + channel]);
        }
    }
    return image;
}

} // namespace

TEST_F(PathTracerTest, ReflectionsMatchThePathTracerInTheMirrorRoom) {
    // The mirror room without its glass: a mirror, a polished floor and a gold block, whose reflections the
    // path tracer follows through eight surfaces. The lighting adds the reflections of global illumination
    // (ADR-0042); without them, the indirect light stands in for what the specular lobe reflects, and the
    // mirror shows a flat grey.
    const renderer::MirrorRoom room = renderer::MakeMirrorRoom();
    const SceneView view =
        MirrorRoomView(room, m_cube, [this](const MaterialDesc& desc) { return Material(desc); }, false);
    constexpr rhi::Extent kExtent{128, 96};
    const Image traced = Trace(view, kExtent, {.maxBounces = 8, .seed = 19, .jitter = false}, 512);
    core::Result<renderer::GlobalIllumination> created = renderer::GlobalIllumination::Create(Device());
    ASSERT_TRUE(created.has_value()) << core::Format(created.error());
    renderer::GlobalIllumination& indirect = *created;
    const Image reflected =
        Rasterize(view, kExtent, {.frames = 128, .indirect = &indirect, .warmup = 64, .reflections = true});
    const Image denoised =
        Rasterize(view, kExtent, {.frames = 32, .denoise = true, .indirect = &indirect, .reflections = true});
    const Image unreflected = Rasterize(view, kExtent, {.frames = 128, .indirect = &indirect, .warmup = 64});

    const float reflectedFlip = Flip(traced, reflected, kExtent);
    const float denoisedFlip = Flip(traced, denoised, kExtent);
    const float unreflectedFlip = Flip(traced, unreflected, kExtent);
    RecordProperty("flipAveraged", std::to_string(reflectedFlip));
    RecordProperty("flip", std::to_string(denoisedFlip));
    RecordProperty("flipWithoutReflections", std::to_string(unreflectedFlip));
    // Measured on an RTX 3090: 0.188 averaged, 0.200 denoised, 0.280 without reflections. What remains is
    // mostly the indirect light's, as in the Cornell box: the lamps shine down, and the ceiling has only
    // the light that bounces up (ADR-0043).
    EXPECT_LT(reflectedFlip, 0.22f);
    EXPECT_LT(denoisedFlip, 0.23f);
    EXPECT_LT(reflectedFlip, 0.8f * unreflectedFlip);
}

TEST_F(PathTracerTest, GlassMatchesThePathTracerInTheMirrorRoom) {
    // The whole mirror room, with its window pane and its block of thick glass, drawn by the glass pass over
    // the frame (ADR-0042), against the same room without its glass.
    const renderer::MirrorRoom room = renderer::MakeMirrorRoom();
    const auto material = [this](const MaterialDesc& desc) { return Material(desc); };
    const SceneView view = MirrorRoomView(room, m_cube, material, true);
    const SceneView withoutGlass = MirrorRoomView(room, m_cube, material, false);
    constexpr rhi::Extent kExtent{128, 96};
    const Image traced = Trace(view, kExtent, {.maxBounces = 8, .seed = 23, .jitter = false}, 512);
    core::Result<renderer::GlobalIllumination> created = renderer::GlobalIllumination::Create(Device());
    ASSERT_TRUE(created.has_value()) << core::Format(created.error());
    renderer::GlobalIllumination& indirect = *created;
    const Image glass =
        Rasterize(view, kExtent, {.frames = 128, .indirect = &indirect, .warmup = 64, .reflections = true});
    const Image bare =
        Rasterize(withoutGlass, kExtent, {.frames = 128, .indirect = &indirect, .warmup = 64, .reflections = true});

    const float glassFlip = Flip(traced, glass, kExtent);
    const float bareFlip = Flip(traced, bare, kExtent);
    RecordProperty("flip", std::to_string(glassFlip));
    RecordProperty("flipWithoutGlass", std::to_string(bareFlip));
    // Measured on an RTX 3090: 0.197, and 0.207 without the glass, which covers a small part of the image.
    EXPECT_LT(glassFlip, 0.23f);
    EXPECT_LT(glassFlip, bareFlip);
}

TEST_F(PathTracerTest, ReflectionsKnowHowFarWhatTheyReflectIs) {
    // A camera a meter above a mirror floor looks 45 degrees down, at a wall 4 m away: the center's
    // reflection rises at 45 degrees, and meets the wall after 3 * sqrt(2) m. The denoiser follows a
    // reflection with that distance (ADR-0042). A rough floor, beyond renderer.reflection_roughness, traces
    // no ray: its distance is 0, and the indirect light stands in for its reflection.
    MaterialDesc mirror;
    mirror.metallicFactor = 1.0f;
    mirror.roughnessFactor = 0.0f;
    MaterialDesc matte;
    matte.metallicFactor = 0.0f;
    const glm::mat4 floor = glm::scale(glm::translate(glm::mat4(1.0f), {0.0f, -0.05f, 0.0f}), {20.0f, 0.1f, 20.0f});
    const glm::mat4 wall = glm::scale(glm::translate(glm::mat4(1.0f), {0.0f, 0.0f, -4.05f}), {20.0f, 20.0f, 0.1f});
    SceneView view;
    view.atmosphere = Dark();
    view.camera = LookAt({0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, glm::radians(30.0f));
    view.lights.push_back({.position = {0.0f, 3.0f, -2.0f}, .color = glm::vec3(100.0f), .range = 20.0f});
    view.draws.push_back({.mesh = m_cube, .world = floor, .material = Material(mirror)});
    view.draws.push_back({.mesh = m_cube, .world = wall, .material = Material(matte)});
    core::Result<renderer::GlobalIllumination> created = renderer::GlobalIllumination::Create(Device());
    ASSERT_TRUE(created.has_value()) << core::Format(created.error());
    renderer::GlobalIllumination& indirect = *created;
    constexpr rhi::Extent kExtent{32, 32};
    const auto centerDistance = [&] {
        const Image reflections = ReadHalf(Device(), indirect.Reflections(), kExtent);
        return reflections[(kExtent.height / 2) * kExtent.width + kExtent.width / 2].a;
    };
    Rasterize(view, kExtent, {.indirect = &indirect, .reflections = true});
    const float distance = centerDistance();
    RecordProperty("distance", std::to_string(distance));
    // Measured on an RTX 3090: 4.31, the pixel's center a little off the image's.
    EXPECT_NEAR(distance, 3.0f * std::sqrt(2.0f), 0.15f);

    MaterialDesc rough = mirror;
    rough.roughnessFactor = 0.9f;
    view.draws[0].material = Material(rough);
    Rasterize(view, kExtent, {.indirect = &indirect, .reflections = true});
    EXPECT_EQ(centerDistance(), 0.0f);
}

TEST_F(PathTracerTest, DenoisedReflectionsFollowTheReflectedPoint) {
    // A camera slides sideways over a polished floor that reflects a bright panel, through the denoiser. A
    // reflection's history moved with the floor smears the panel's reflection along the way; moved with the
    // reflected point, from the reflections' hit distance (ADR-0042), it stays where the path tracer puts it.
    MaterialDesc floorMaterial;
    floorMaterial.baseColorFactor = glm::vec4(0.2f, 0.2f, 0.2f, 1.0f);
    floorMaterial.metallicFactor = 1.0f;
    floorMaterial.roughnessFactor = 0.08f;
    MaterialDesc panelMaterial;
    panelMaterial.metallicFactor = 0.0f;
    panelMaterial.baseColorFactor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    panelMaterial.emissiveFactor = glm::vec3(38.4f); // Nits: 4 on the screen at EV100 3 (ADR-0043).
    const glm::mat4 floor = glm::scale(glm::translate(glm::mat4(1.0f), {0.0f, -0.05f, 0.0f}), {20.0f, 0.1f, 20.0f});
    const glm::mat4 panel = glm::scale(glm::translate(glm::mat4(1.0f), {0.0f, 1.5f, -5.0f}), {1.0f, 3.0f, 0.1f});
    SceneView view;
    view.atmosphere = Dark();
    view.draws.push_back({.mesh = m_cube, .world = floor, .material = Material(floorMaterial)});
    view.draws.push_back({.mesh = m_cube, .world = panel, .material = Material(panelMaterial)});
    const auto cameraAt = [](std::uint32_t frame) {
        const glm::vec3 eye(-1.0f + 0.03f * static_cast<float>(frame), 1.0f, 0.0f);
        return LookAt(eye, eye + glm::vec3(0.0f, -0.6f, -1.0f), glm::radians(50.0f));
    };
    constexpr std::uint32_t kFrames = 48;
    view.camera = cameraAt(kFrames - 1);
    constexpr rhi::Extent kExtent{96, 64};
    const Image traced = Trace(view, kExtent, {.maxBounces = 4, .seed = 29, .jitter = false}, 256);
    const auto slide = [&](SceneView& moving, std::uint32_t frame) { moving.camera = cameraAt(frame); };
    core::Result<renderer::GlobalIllumination> created = renderer::GlobalIllumination::Create(Device());
    ASSERT_TRUE(created.has_value()) << core::Format(created.error());
    renderer::GlobalIllumination& indirect = *created;
    const Image followed =
        Rasterize(view, kExtent,
                  {.frames = kFrames, .denoise = true, .indirect = &indirect, .reflections = true, .animate = slide});
    const Image smeared = Rasterize(view, kExtent,
                                    {.frames = kFrames,
                                     .denoise = true,
                                     .indirect = &indirect,
                                     .reflections = true,
                                     .reprojectReflections = false,
                                     .animate = slide});
    const float followedFlip = Flip(traced, followed, kExtent);
    const float smearedFlip = Flip(traced, smeared, kExtent);
    RecordProperty("flip", std::to_string(followedFlip));
    RecordProperty("flipMovedWithTheSurface", std::to_string(smearedFlip));
    // Measured on an RTX 3090: 0.184, and 0.400 moved with the floor.
    EXPECT_LT(followedFlip, 0.25f);
    EXPECT_LT(followedFlip, 0.6f * smearedFlip);
}

TEST_F(PathTracerTest, IndirectLightBouncesMatchThePathTracer) {
    // The Cornell box and the mirror room, with 0 to 3 surfaces lit by the lights along each pixel's ray before
    // the cache is read (ADR-0045), denoised, against the path tracer.
    const renderer::CornellBox box = renderer::MakeCornellBox();
    SceneView cornell;
    cornell.camera = LookAt(box.eye, box.target, box.verticalFov);
    cornell.atmosphere = Dark();
    cornell.lights.push_back(box.light);
    for (const renderer::CornellPart& part : box.parts) {
        cornell.draws.push_back({.mesh = m_cube, .world = part.world, .material = Material(part.material)});
    }
    const renderer::MirrorRoom room = renderer::MakeMirrorRoom();
    const SceneView mirrors =
        MirrorRoomView(room, m_cube, [this](const MaterialDesc& desc) { return Material(desc); }, false);
    constexpr rhi::Extent kExtent{96, 96};
    const Image tracedCornell = Trace(cornell, kExtent, {.maxBounces = 8, .seed = 17, .jitter = false}, 512);
    const Image tracedMirrors = Trace(mirrors, kExtent, {.maxBounces = 8, .seed = 19, .jitter = false}, 512);
    std::array<float, 4> cornellFlips{};
    std::array<float, 4> mirrorFlips{};
    for (std::uint32_t bounces = 0; bounces <= 3; ++bounces) {
        const auto measure = [&](const SceneView& view, const Image& traced, bool reflections) {
            core::Result<renderer::GlobalIllumination> created = renderer::GlobalIllumination::Create(Device());
            EXPECT_TRUE(created.has_value());
            const Image denoised = Rasterize(view, kExtent,
                                             {.frames = 32,
                                              .denoise = true,
                                              .indirect = &*created,
                                              .warmup = 32,
                                              .reflections = reflections,
                                              .gi = {.bounces = bounces}});
            return Flip(traced, denoised, kExtent);
        };
        cornellFlips[bounces] = measure(cornell, tracedCornell, false);
        mirrorFlips[bounces] = measure(mirrors, tracedMirrors, true);
        RecordProperty("cornell" + std::to_string(bounces), std::to_string(cornellFlips[bounces]));
        RecordProperty("mirrors" + std::to_string(bounces), std::to_string(mirrorFlips[bounces]));
    }
    // Measured on an RTX 3090, for 0 to 3 bounces: the Cornell box 0.157, 0.116, 0.114, 0.118; the mirror room
    // 0.268, 0.214, 0.217, 0.217. A bounce lit by the lights shows the shadows and the detail the cache's
    // 10 cm cells miss; more add little that the cache does not already give.
    for (std::uint32_t bounces = 1; bounces <= 3; ++bounces) {
        EXPECT_LT(cornellFlips[bounces], 0.85f * cornellFlips[0]) << bounces;
        EXPECT_LT(mirrorFlips[bounces], 0.9f * mirrorFlips[0]) << bounces;
    }
    EXPECT_LT(cornellFlips[1], 0.13f);
    EXPECT_LT(mirrorFlips[1], 0.24f);
}

namespace {

// The dark corridor (ADR-0045) as a view, from eye towards target, with its flashlight.
SceneView CorridorView(const renderer::DarkCorridor& corridor, renderer::MeshHandle cube,
                       const std::function<renderer::MaterialHandle(const MaterialDesc&)>& material,
                       const glm::vec3& target) {
    SceneView view;
    view.camera = LookAt(corridor.eye, target, corridor.verticalFov);
    view.atmosphere = {.fogColor = glm::vec3(0.0f), .fogDensity = 0.0f, .ev100 = corridor.ev100};
    view.lights.push_back(renderer::Flashlight(corridor.eye, target));
    for (const renderer::CornellPart& part : corridor.parts) {
        view.draws.push_back({.mesh = cube, .world = part.world, .material = material(part.material)});
    }
    return view;
}

// The corridor's view turned by an angle to the right, around the eye.
glm::vec3 TurnedTarget(const renderer::DarkCorridor& corridor, float degrees) {
    const glm::vec3 ahead = corridor.target - corridor.eye;
    const float angle = -glm::radians(degrees);
    const glm::vec3 turned(ahead.x * std::cos(angle) + ahead.z * std::sin(angle), ahead.y,
                           -ahead.x * std::sin(angle) + ahead.z * std::cos(angle));
    return corridor.eye + turned;
}

// The mean luminance of the pixels of an image where mask is set.
double MaskedMean(const Image& image, const std::vector<bool>& mask) {
    double total = 0.0;
    std::size_t count = 0;
    for (std::size_t index = 0; index < image.size(); ++index) {
        if (mask[index]) {
            total += Luminance(image[index]);
            ++count;
        }
    }
    return count > 0 ? total / static_cast<double>(count) : 0.0;
}

// The mean change of luminance from one image to the next, over the mean luminance.
double Flicker(const Image& before, const Image& after) {
    double change = 0.0;
    double total = 0.0;
    for (std::size_t index = 0; index < after.size(); ++index) {
        change += std::abs(Luminance(after[index]) - Luminance(before[index]));
        total += Luminance(after[index]);
    }
    return total > 0.0 ? change / total : 0.0;
}

// What the dark corridor measures of one way of tracing indirect light.
struct CorridorScores {
    float flip = 0.0f;    // Of the 32nd denoised frame after a cut.
    double flicker = 0.0; // Mean change of luminance between denoised frames that do not move, over its mean.
    int lag = 0;          // Frames after a turn of 30 degrees until the bounce-lit pixels are within 10%.
};

} // namespace

class DarkCorridorTest : public PathTracerTest {
protected:
    static constexpr rhi::Extent kExtent{160, 90};
    static constexpr float kTurn = 30.0f;
    static constexpr std::uint32_t kStill = 64; // Frames before the turn.
    static constexpr std::uint32_t kAfter = 48; // And after.

    void SetUp() override {
        PathTracerTest::SetUp();
        if (IsSkipped() || HasFatalFailure()) {
            return;
        }
        const auto material = [this](const MaterialDesc& desc) { return Material(desc); };
        m_view = CorridorView(m_corridor, m_cube, material, m_corridor.target);
        m_turned = CorridorView(m_corridor, m_cube, material, TurnedTarget(m_corridor, kTurn));
        m_traced = Trace(m_view, kExtent, {.maxBounces = 8, .seed = 41, .jitter = false}, 2048);
        m_tracedTurned = Trace(m_turned, kExtent, {.maxBounces = 8, .seed = 43, .jitter = false}, 2048);
        // Where the turned view's light has bounced at least once: no direct light reaches the pixel.
        const Image direct =
            Trace(m_turned, kExtent, {.maxBounces = 2, .seed = 47, .jitter = false, .directOnly = true}, 64);
        m_bounceOnly.resize(direct.size());
        for (std::size_t index = 0; index < direct.size(); ++index) {
            m_bounceOnly[index] = Luminance(direct[index]) <= 0.0f && Luminance(m_tracedTurned[index]) > 0.0f;
        }
    }

    CorridorScores Measure(const renderer::GlobalIlluminationSettings& settings) {
        core::Result<renderer::GlobalIllumination> created = renderer::GlobalIllumination::Create(Device());
        EXPECT_TRUE(created.has_value());
        CorridorScores scores;
        const double reference = MaskedMean(m_tracedTurned, m_bounceOnly);
        Image last;
        double flicker = 0.0;
        int within = 0;
        scores.lag = static_cast<int>(kAfter);
        Rasterize(m_view, kExtent,
                  {.frames = kStill + kAfter,
                   .denoise = true,
                   .indirect = &*created,
                   .animate = [&](SceneView& view, std::uint32_t frame) { view = frame < kStill ? m_view : m_turned; },
                   .gi = settings,
                   .eachFrame =
                       [&](const Image& image, std::uint32_t frame) {
                           if (frame == 31) {
                               scores.flip = Flip(m_traced, image, kExtent);
                           }
                           if (frame >= 33 && frame < kStill) {
                               flicker += Flicker(last, image);
                           }
                           if (frame >= kStill && scores.lag == static_cast<int>(kAfter)) {
                               const double mean = MaskedMean(image, m_bounceOnly);
                               within = std::abs(mean - reference) < 0.1 * reference ? within + 1 : 0;
                               // Within for three frames in a row, not by chance.
                               if (within == 3) {
                                   scores.lag = static_cast<int>(frame - kStill) - 2;
                               }
                           }
                           last = image;
                       }});
        scores.flicker = flicker / static_cast<double>(kStill - 33);
        return scores;
    }

    void Report(const std::string& name, const CorridorScores& scores) {
        RecordProperty(name + "_flip", std::to_string(scores.flip));
        RecordProperty(name + "_flicker", std::to_string(scores.flicker));
        RecordProperty(name + "_lag", std::to_string(scores.lag));
        std::printf("%s: FLIP %.4f, flicker %.4f, lag %d frames\n", name.c_str(), scores.flip, scores.flicker,
                    scores.lag);
    }

    renderer::DarkCorridor m_corridor = renderer::MakeDarkCorridor();
    SceneView m_view;
    SceneView m_turned;
    Image m_traced;
    Image m_tracedTurned;
    std::vector<bool> m_bounceOnly;
};

TEST_F(DarkCorridorTest, TheFlashlightsBounceMatchesThePathTracer) {
    // The dark corridor, where nearly everything seen is the flashlight's light sent back by a wall (ADR-0045):
    // with the default settings, one surface lit by the flashlight along each ray before the cache is read,
    // and with the cache read where the rays first hit, as before. The cache's cells are coarse, and it lags
    // the flashlight that moves with the camera; the bounce lit by the flashlight does neither.
    ASSERT_GT(std::ranges::count(m_bounceOnly, true), 1000) << "the turned view has pixels lit only by bounces";
    const CorridorScores bounced = Measure({});
    const CorridorScores cached = Measure({.bounces = 0});
    Report("bounce", bounced);
    Report("cache", cached);
    // Measured on an RTX 3090: FLIP 0.112, flicker 0.0078, lag 0 frames; from the cache, FLIP 0.243.
    EXPECT_LT(bounced.flip, 0.13f);
    EXPECT_LT(bounced.flip, 0.6f * cached.flip);
    EXPECT_LT(bounced.flicker, 0.0095);
    EXPECT_LE(bounced.lag, 2);
}

TEST_F(DarkCorridorTest, DISABLED_MeasureEveryWayOfTracingIndirectLight) {
    // Not a test: the scores of each setting, for ADR-0045.
    for (std::uint32_t bounces = 0; bounces <= 3; ++bounces) {
        for (std::uint32_t rays : {1U, 2U, 4U}) {
            Report("rays" + std::to_string(rays) + "_bounces" + std::to_string(bounces),
                   Measure({.bounces = bounces, .rays = rays}));
        }
    }
    for (const std::uint32_t candidates : {1U, 2U, 8U}) {
        Report("candidates" + std::to_string(candidates), Measure({.lightCandidates = candidates}));
    }
    for (const float clamp : {0.5f, 1.0f, 4.0f, 16.0f}) {
        Report("clamp" + std::to_string(clamp), Measure({.clamp = clamp}));
    }
}

TEST_F(PathTracerTest, BouncedLightIsClampedAndDirectLightIsNot) {
    // A small lamp 5 cm from a wall makes a patch far brighter than the clamp: seen directly, it stays as
    // bright; its light, bounced, is clamped (ADR-0045). With one ray, each pixel's indirect light is one
    // sample's.
    MaterialDesc matte;
    matte.baseColorFactor = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
    matte.metallicFactor = 0.0f;
    TwoRooms rooms = MakeTwoRooms(m_cube, Material(matte));
    SceneView view;
    view.atmosphere = Dark();
    view.draws = rooms.draws;
    view.lights.push_back({.position = {-1.95f, 1.0f, 0.0f}, .color = glm::vec3(50.0f), .range = 6.0f});
    view.camera = LookAt({-0.2f, 1.0f, 0.0f}, {-2.0f, 1.0f, 0.0f}, glm::radians(70.0f));
    constexpr rhi::Extent kExtent{64, 64};
    constexpr float kClamp = 2.0f;
    core::Result<renderer::GlobalIllumination> created = renderer::GlobalIllumination::Create(Device());
    ASSERT_TRUE(created.has_value()) << core::Format(created.error());
    const Image image =
        Rasterize(view, kExtent, {.frames = 1, .indirect = &*created, .warmup = 16, .gi = {.clamp = kClamp}});
    const Image indirect = ReadHalf(Device(), created->Output(), kExtent);
    float brightestBounce = 0.0f;
    for (const glm::vec4& pixel : indirect) {
        brightestBounce = std::max(brightestBounce, Luminance(pixel));
    }
    float brightest = 0.0f;
    for (const glm::vec4& pixel : image) {
        brightest = std::max(brightest, Luminance(pixel));
    }
    RecordProperty("brightestBounce", std::to_string(brightestBounce));
    RecordProperty("brightest", std::to_string(brightest));
    EXPECT_GT(brightestBounce, 0.5f * kClamp) << "the patch's light bounces";
    EXPECT_LE(brightestBounce, kClamp * 1.01f);
    EXPECT_GT(brightest, 10.0f * kClamp) << "the patch itself is not clamped";
}

TEST_F(DarkCorridorTest, DISABLED_TimeIndirectLightAt1080p) {
    // Not a test: the GPU time of each way of tracing indirect light at 1920 x 1080 in the dark corridor,
    // through frames with timestamps (ADR-0045 set a budget of 1.5 ms on an RTX 3090).
    constexpr rhi::Extent kFull{1920, 1080};
    const auto created = [&](rhi::Format format, rhi::TextureUsage usage, const char* name) {
        const core::Result<rhi::TextureHandle> texture =
            Device().CreateTexture({.extent = kFull, .format = format, .usage = usage, .name = name});
        EXPECT_TRUE(texture.has_value());
        return texture.value_or(rhi::TextureHandle{});
    };
    const rhi::TextureHandle depth =
        created(kDepthFormat, rhi::TextureUsage::DepthTarget | rhi::TextureUsage::Sampled, "Depth");
    const rhi::TextureHandle normals =
        created(MeshPass::kNormalFormat, rhi::TextureUsage::ColorTarget | rhi::TextureUsage::Sampled, "Normals");
    const rhi::TextureHandle motion =
        created(MeshPass::kMotionFormat, rhi::TextureUsage::ColorTarget | rhi::TextureUsage::Sampled, "Motion");
    core::Result<renderer::ShadingTargets> shading = m_pass->CreateShadingTargets(kFull);
    ASSERT_TRUE(shading.has_value());

    const auto time = [&](const std::string& name, const renderer::GlobalIlluminationSettings& settings) {
        core::Result<renderer::GlobalIllumination> gi = renderer::GlobalIllumination::Create(Device());
        ASSERT_TRUE(gi.has_value() && gi->Resize(kFull).has_value());
        constexpr std::uint32_t kWarmup = 16;
        constexpr std::uint32_t kTimed = 128;
        std::array<std::vector<std::string_view>, rhi::kFramesInFlight> groups;
        std::map<std::string, double> totals;
        std::uint32_t timed = 0;
        for (std::uint32_t frame = 0; frame < kWarmup + kTimed + rhi::kFramesInFlight; ++frame) {
            core::Result<std::optional<rhi::Frame>> begun = Device().BeginFrame();
            ASSERT_TRUE(begun.has_value() && begun->has_value());
            rhi::Frame& current = **begun;
            const std::span<const double> stamps = Device().CompletedTimestamps();
            if (frame >= kWarmup && stamps.size() == groups[current.slot].size() + 1) {
                for (std::size_t group = 0; group < groups[current.slot].size(); ++group) {
                    totals[std::string(groups[current.slot][group])] += stamps[group + 1] - stamps[group];
                }
                ++timed;
            }
            const core::Result<std::uint32_t> structure = m_scene->Reserve(current.slot, m_view.draws.size());
            ASSERT_TRUE(structure.has_value());
            ASSERT_TRUE(m_pass
                            ->Prepare(m_view, {.extent = kFull,
                                               .accelerationStructure = *structure,
                                               .frameIndex = frame,
                                               .exposure = renderer::ExposureScale(m_view.atmosphere.ev100)})
                            .has_value());
            ASSERT_TRUE(m_scene->Prepare(m_pass->TracedDraws()).has_value());
            renderer::RenderGraph graph;
            m_scene->AddBuild(graph);
            m_pass->AddEmissiveLights(graph);
            graph.AddPass(
                {.name = "G-buffer", .rendering = MeshPass::GBufferRendering(*shading, normals, motion, depth)},
                [&](rhi::CommandList& list) { m_pass->RecordGBuffer(list); });
            gi->AddTo(graph, {.frameBuffer = m_pass->FrameDataIndex(),
                              .instanceBuffer = m_pass->InstanceDataIndex(),
                              .materialBuffer = m_pass->MaterialDataIndex(),
                              .structure = m_scene->Structure(),
                              .emissiveTriangles = m_pass->EmissiveTriangles(),
                              .depth = depth,
                              .normals = normals,
                              .frameIndex = frame,
                              .reflections = false,
                              .settings = settings});
            groups[current.slot] = graph.Execute(current.commands);
            ASSERT_TRUE(Device().EndFrame().has_value());
        }
        ASSERT_TRUE(Device().WaitIdle().has_value());
        for (const auto& [group, total] : totals) {
            const double milliseconds = total / std::max(timed, 1U);
            std::printf("%s, %s: %.3f ms\n", name.c_str(), group.c_str(), milliseconds);
            RecordProperty(name + "_" + group, std::to_string(milliseconds));
        }
    };
    for (std::uint32_t bounces = 0; bounces <= 3; ++bounces) {
        for (std::uint32_t rays : {1U, 2U, 4U}) {
            time("rays" + std::to_string(rays) + "_bounces" + std::to_string(bounces),
                 {.bounces = bounces, .rays = rays});
        }
    }
    for (const std::uint32_t candidates : {1U, 2U, 8U}) {
        time("candidates" + std::to_string(candidates), {.lightCandidates = candidates});
    }
    m_pass->DestroyShadingTargets(*shading);
    for (const rhi::TextureHandle texture : {depth, normals, motion}) {
        Device().DestroyTexture(texture);
    }
}

TEST_F(PathTracerTest, BouncesWeighSeveralLights) {
    // In the room of a thousand lights, each surface a ray bounces off picks one light among a few, weighed by
    // what each would send it (ADR-0045): with one, the bounce is noisier, and the denoised image farther
    // from the path tracer's.
    const SceneView room = RoomOfLights(m_cube, [this](const MaterialDesc& desc) { return Material(desc); });
    constexpr rhi::Extent kExtent{96, 96};
    const Image traced = Trace(room, kExtent, {.maxBounces = 8, .seed = 31, .jitter = false}, 512);
    std::map<std::uint32_t, float> flips;
    for (const std::uint32_t candidates : {1U, 2U, 4U, 8U}) {
        core::Result<renderer::GlobalIllumination> created = renderer::GlobalIllumination::Create(Device());
        ASSERT_TRUE(created.has_value());
        const Image denoised = Rasterize(
            room, kExtent,
            {.frames = 32, .denoise = true, .indirect = &*created, .warmup = 4, .gi = {.lightCandidates = candidates}});
        flips[candidates] = Flip(traced, denoised, kExtent);
        RecordProperty("candidates" + std::to_string(candidates), std::to_string(flips[candidates]));
    }
    // Measured on an RTX 3090: 0.173 with one, 0.144 with two, 0.131 with four, 0.133 with eight.
    EXPECT_LT(flips[4], 0.9f * flips[1]);
    EXPECT_LT(flips[4], 0.15f);
}

namespace {

// Each 2 x 2 block of an image of twice the extent, averaged: a supersampled reference (ADR-0049).
Image Downsample(const Image& image, rhi::Extent extent) {
    Image result(std::size_t{extent.width} * extent.height);
    const std::size_t wide = std::size_t{extent.width} * 2;
    for (std::uint32_t y = 0; y < extent.height; ++y) {
        for (std::uint32_t x = 0; x < extent.width; ++x) {
            const std::size_t corner = std::size_t{y} * 2 * wide + std::size_t{x} * 2;
            result[std::size_t{y} * extent.width + x] =
                (image[corner] + image[corner + 1] + image[corner + wide] + image[corner + wide + 1]) * 0.25f;
        }
    }
    return result;
}

// What upscaling is measured by (ADR-0049), against the same frames rendered at twice the width and height
// with temporal antialiasing and averaged down.
struct UpscalingScores {
    float still = 0.0f;   // Mean FLIP of the last eight frames before the camera turns.
    float pan = 0.0f;     // Mean FLIP of the frames while it turns.
    double flicker = 0.0; // Mean change of luminance between still frames, over its mean.
};

} // namespace

class UpscalingTest : public PathTracerTest {
protected:
    static constexpr rhi::Extent kExtent{320, 180};
    static constexpr std::uint32_t kStill = 48;
    static constexpr std::uint32_t kPan = 32;
    static constexpr float kDegreesPerFrame = 0.5f;

    // What the frames are lit by: indirect light and its reflections, or direct light alone.
    struct Lighting {
        bool indirect = true;
        bool reflections = true;
    };

    // The frames of the view, still then turning, at the extent, rendered at scale of it and upscaled, through
    // the lighting and the denoiser.
    std::vector<Image> Frames(const SceneView& view, rhi::Extent extent, float scale, Lighting lighting = {}) {
        core::Result<renderer::GlobalIllumination> indirect = renderer::GlobalIllumination::Create(Device());
        EXPECT_TRUE(indirect.has_value());
        std::vector<Image> frames;
        const glm::mat4 still = view.camera->view;
        Rasterize(view, extent,
                  {.frames = kStill + kPan,
                   .denoise = true,
                   .indirect = lighting.indirect ? &*indirect : nullptr,
                   .reflections = lighting.indirect && lighting.reflections,
                   .animate =
                       [&](SceneView& animated, std::uint32_t frame) {
                           const float degrees =
                               frame < kStill ? 0.0f : static_cast<float>(frame - kStill + 1) * kDegreesPerFrame;
                           animated.camera->view =
                               glm::rotate(glm::mat4(1.0f), glm::radians(degrees), glm::vec3(0.0f, 1.0f, 0.0f)) * still;
                       },
                   .eachFrame = [&](const Image& image, std::uint32_t /*frame*/) { frames.push_back(image); },
                   .resolve = true,
                   .renderScale = scale});
        return frames;
    }

    std::vector<Image> Reference(const SceneView& view, Lighting lighting = {}) {
        std::vector<Image> frames = Frames(view, {kExtent.width * 2, kExtent.height * 2}, 1.0f, lighting);
        for (Image& frame : frames) {
            frame = Downsample(frame, kExtent);
        }
        return frames;
    }

    UpscalingScores Measure(const SceneView& view, const std::vector<Image>& reference, float scale,
                            Lighting lighting = {}) {
        const std::vector<Image> frames = Frames(view, kExtent, scale, lighting);
        UpscalingScores scores;
        for (std::uint32_t frame = kStill - 8; frame < kStill; ++frame) {
            scores.still += Flip(reference[frame], frames[frame], kExtent) / 8.0f;
        }
        for (std::uint32_t frame = kStill; frame < kStill + kPan; ++frame) {
            scores.pan += Flip(reference[frame], frames[frame], kExtent) / static_cast<float>(kPan);
        }
        for (std::uint32_t frame = kStill - 16; frame < kStill; ++frame) {
            scores.flicker += Flicker(frames[frame - 1], frames[frame]) / 16.0;
        }
        return scores;
    }

    void Report(const std::string& name, const UpscalingScores& scores) {
        RecordProperty(name + "_still", std::to_string(scores.still));
        RecordProperty(name + "_pan", std::to_string(scores.pan));
        RecordProperty(name + "_flicker", std::to_string(scores.flicker));
        std::printf("%s: FLIP still %.4f, panning %.4f, flicker %.4f\n", name.c_str(), scores.still, scores.pan,
                    scores.flicker);
    }

    SceneView Corridor() {
        const renderer::DarkCorridor corridor = renderer::MakeDarkCorridor();
        return CorridorView(
            corridor, m_cube, [this](const MaterialDesc& desc) { return Material(desc); }, corridor.target);
    }

    SceneView Mirrors(bool glass = true) {
        return MirrorRoomView(
            renderer::MakeMirrorRoom(), m_cube, [this](const MaterialDesc& desc) { return Material(desc); }, glass);
    }

    // The mirror room with nothing lit: its rough surfaces glow, bright and dim in turn, and its mirrors, metals
    // and glass show them. Without light to sample, its reflections hold no noise.
    SceneView GlowingMirrors() {
        SceneView view = MirrorRoomView(
            renderer::MakeMirrorRoom(), m_cube,
            [this, part = 0](const MaterialDesc& desc) mutable {
                MaterialDesc glowing = desc;
                if (desc.transmission <= 0.0f && desc.metallicFactor < 0.5f && desc.roughnessFactor > 0.3f) {
                    glowing.emissiveFactor =
                        glm::vec3((part++ % 2) == 0 ? 4.0f : 0.5f) * glm::vec3(desc.baseColorFactor);
                    glowing.emissiveLight = false;
                }
                return Material(glowing);
            },
            true);
        view.lights.clear();
        return view;
    }

    // The corridor's parts glowing, bright and dim in turn, and nothing lit: edges alone, the same at every
    // render size, so that what differs is the upscaler's.
    SceneView Edges() {
        const renderer::DarkCorridor corridor = renderer::MakeDarkCorridor();
        SceneView view = CorridorView(
            corridor, m_cube,
            [this, part = 0](const MaterialDesc& desc) mutable {
                MaterialDesc glowing = desc;
                glowing.emissiveFactor = glm::vec3((part++ % 2) == 0 ? 16.0f : 0.5f);
                glowing.emissiveLight = false;
                return Material(glowing);
            },
            corridor.target);
        view.lights.clear();
        return view;
    }
};

TEST_F(UpscalingTest, TheUpscalerAtTwoThirdsStaysNearTheOutputsResolution) {
    // ADR-0049: at 67%, the upscaler against native resolution, both against a reference supersampled twice
    // each way. In the dark corridor, lit as the game is; and on edges alone, where nothing but the upscaler
    // differs.
    const SceneView corridor = Corridor();
    const std::vector<Image> corridorReference = Reference(corridor);
    const UpscalingScores corridorNative = Measure(corridor, corridorReference, 1.0f);
    const UpscalingScores corridorUpscaled = Measure(corridor, corridorReference, 0.67f);
    const SceneView edges = Edges();
    const std::vector<Image> edgesReference = Reference(edges);
    const UpscalingScores edgesNative = Measure(edges, edgesReference, 1.0f);
    const UpscalingScores edgesUpscaled = Measure(edges, edgesReference, 0.67f);
    Report("corridor_100", corridorNative);
    Report("corridor_67", corridorUpscaled);
    Report("edges_100", edgesNative);
    Report("edges_67", edgesUpscaled);
    // Measured on an RTX 3090: the corridor 0.093 and 0.105 still, 0.091 and 0.101 panning; edges 0.0028 and
    // 0.0044 still, 0.0053 and 0.0065 panning.
    EXPECT_LT(corridorUpscaled.still, 1.2f * corridorNative.still);
    EXPECT_LT(corridorUpscaled.pan, 1.2f * corridorNative.pan);
    EXPECT_LT(edgesUpscaled.still, 0.006f);
    EXPECT_LT(edgesUpscaled.pan, 1.4f * edgesNative.pan);
}

TEST_F(UpscalingTest, DISABLED_MeasureEachScale) {
    // Not a test: the scores of each scale, for ADR-0049.
    const std::vector<std::pair<std::string, SceneView>> scenes{
        {"edges", Edges()}, {"corridor", Corridor()}, {"mirrors", Mirrors()}};
    for (const auto& [name, view] : scenes) {
        const std::vector<Image> reference = Reference(view);
        for (const float scale : {1.0f, 0.67f, 0.58f, 0.5f}) {
            Report(name + "_" + std::to_string(static_cast<int>(std::lround(scale * 100.0f))),
                   Measure(view, reference, scale));
        }
    }
}

TEST_F(UpscalingTest, DISABLED_BreakDownTheMirrorRoom) {
    // Not a test: where the mirror room's gap at 67% comes from (step 9.16). Each variant is measured at 100%
    // and 67% against its own reference; if the gap closes where the lighting holds no noise, it is the
    // lighting's, which works in render pixels, and an upscaler given the same frames cannot close it.
    struct Variant {
        std::string name;
        SceneView view;
        Lighting lighting;
    };
    const std::vector<Variant> variants{
        {"full", Mirrors(), {}},
        {"no_glass", Mirrors(false), {}},
        {"no_reflections", Mirrors(), {.reflections = false}},
        {"direct_only", Mirrors(), {.indirect = false}},
        {"glowing", GlowingMirrors(), {}},
    };
    for (const Variant& variant : variants) {
        const std::vector<Image> reference = Reference(variant.view, variant.lighting);
        const UpscalingScores native = Measure(variant.view, reference, 1.0f, variant.lighting);
        const UpscalingScores upscaled = Measure(variant.view, reference, 0.67f, variant.lighting);
        Report(variant.name + "_100", native);
        Report(variant.name + "_67", upscaled);
        std::printf("%s: 67%% over 100%%, still %.2f, panning %.2f\n", variant.name.c_str(),
                    upscaled.still / native.still, upscaled.pan / native.pan);
    }
}
