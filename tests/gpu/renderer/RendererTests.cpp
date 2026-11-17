// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/platform/Platform.hpp>
#include <daidalos/renderer/Renderer.hpp>

#include <glm/ext/matrix_transform.hpp>
#include <gtest/gtest.h>

#include <cmath>
#include <optional>
#include <string>
#include <vector>

using namespace daidalos;

namespace {

// A real window on a real swapchain, skipped on machines without a display or Vulkan 1.3.
class RendererTest : public ::testing::Test {
protected:
    void SetUp() override {
        core::Result<platform::Platform> platform = platform::Platform::Create();
        if (!platform) {
            GTEST_SKIP() << core::Format(platform.error());
        }
        m_platform.emplace(std::move(*platform));

        platform::WindowConfig config;
        config.width = 320;
        config.height = 240;
        config.vulkan = true;
        core::Result<platform::Window> window = platform::Window::Create(*m_platform, config);
        if (!window) {
            // A machine without Vulkan skips. An SDL built without it is a defect of this project.
            ASSERT_EQ(window.error().message.find("not configured in SDL"), std::string::npos)
                << window.error().message;
            GTEST_SKIP() << core::Format(window.error());
        }
        m_window.emplace(std::move(*window));
    }

    // Creates a renderer, or returns nothing and skips where the device is not suitable.
    std::optional<renderer::Renderer> CreateRenderer() {
        core::Result<renderer::Renderer> created =
            renderer::Renderer::Create(*m_window, {.validation = true, .synchronizationValidation = true});
        if (!created) {
            EXPECT_EQ(created.error().code, core::ErrorCode::NotSupported) << core::Format(created.error());
            return std::nullopt;
        }
        return std::optional<renderer::Renderer>(std::move(*created));
    }

    void RenderFrames(renderer::Renderer& target, const renderer::SceneView& view, int frames) {
        std::vector<platform::Event> events;
        for (int frame = 0; frame < frames; ++frame) {
            m_platform->PollEvents(events);
            ASSERT_TRUE(target.Render(view).has_value());
        }
    }

    std::optional<platform::Platform> m_platform;
    std::optional<platform::Window> m_window;
};

renderer::SceneView TestView(renderer::MeshHandle mesh) {
    return {
        renderer::CameraView{glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -3.0f)), glm::radians(60.0f), 0.1f},
        {{mesh, glm::mat4(1.0f)}}};
}

} // namespace

TEST_F(RendererTest, RendersFramesToAWindow) {
    std::optional<renderer::Renderer> target = CreateRenderer();
    if (!target.has_value()) {
        GTEST_SKIP();
    }
    const core::Result<renderer::MeshHandle> mesh = target->CreateMesh(renderer::MakeCube(), "Cube");
    ASSERT_TRUE(mesh.has_value());

    RenderFrames(*target, TestView(*mesh), 10);
    EXPECT_EQ(target->Device().PresentedFrameCount(), 10U);
    EXPECT_EQ(target->Device().ValidationErrorCount(), 0U);
}

TEST_F(RendererTest, FollowsTheWindowSize) {
    std::optional<renderer::Renderer> target = CreateRenderer();
    if (!target.has_value()) {
        GTEST_SKIP();
    }
    const core::Result<renderer::MeshHandle> mesh = target->CreateMesh(renderer::MakeCube(), "Cube");
    ASSERT_TRUE(mesh.has_value());
    const renderer::SceneView view = TestView(*mesh);

    RenderFrames(*target, view, 3);
    for (const platform::Extent size : {platform::Extent{640, 480}, {200, 300}, {800, 200}}) {
        m_window->SetSize(size);
        RenderFrames(*target, view, 4);
    }
    EXPECT_EQ(target->Device().ValidationErrorCount(), 0U);
}

TEST_F(RendererTest, SwitchesVsyncWhileRunning) {
    std::optional<renderer::Renderer> target = CreateRenderer();
    if (!target.has_value()) {
        GTEST_SKIP();
    }
    const renderer::SceneView view{};
    target->SetVsync(false);
    RenderFrames(*target, view, 3);
    target->SetVsync(true);
    RenderFrames(*target, view, 3);
    EXPECT_EQ(target->Device().ValidationErrorCount(), 0U);
}

TEST_F(RendererTest, SwitchesAntialiasingWhileRunning) {
    std::optional<renderer::Renderer> target = CreateRenderer();
    if (!target.has_value()) {
        GTEST_SKIP();
    }
    const core::Result<renderer::MeshHandle> mesh = target->CreateMesh(renderer::MakeCube(), "Cube");
    ASSERT_TRUE(mesh.has_value());
    const renderer::SceneView view = TestView(*mesh);
    using renderer::Antialiasing;
    for (const Antialiasing mode : {Antialiasing::Taa, Antialiasing::Off, Antialiasing::Taa}) {
        target->SetAntialiasing(mode);
        RenderFrames(*target, view, 3);
    }
    // The history follows the window too.
    m_window->SetSize({300, 200});
    RenderFrames(*target, view, 3);
    EXPECT_EQ(target->Device().ValidationErrorCount(), 0U);
}

TEST_F(RendererTest, RendersBelowTheOutputsResolution) {
    std::optional<renderer::Renderer> target = CreateRenderer();
    if (!target.has_value()) {
        GTEST_SKIP();
    }
    const core::Result<renderer::MeshHandle> mesh = target->CreateMesh(renderer::MakeCube(), "Cube");
    ASSERT_TRUE(mesh.has_value());
    const renderer::SceneView view = TestView(*mesh);
    const platform::Extent size = m_window->PixelSize();

    // A fixed scale (ADR-0049).
    target->SetResolution({.scale = 0.67f, .dynamic = false});
    RenderFrames(*target, view, 3);
    EXPECT_EQ(target->Stats().renderWidth, static_cast<std::uint32_t>(std::lround(size.width * 0.67f)));
    EXPECT_EQ(target->Stats().renderHeight, static_cast<std::uint32_t>(std::lround(size.height * 0.67f)));

    // A scale that changes every frame keeps every history and reads nothing it should not.
    for (const float scale : {0.5f, 0.9f, 0.58f, 1.0f, 0.75f, 0.5f}) {
        target->SetResolution({.scale = scale, .dynamic = false});
        RenderFrames(*target, view, 1);
    }
    // Debug views, and no antialiasing, render at the output's resolution.
    target->SetDebugView(renderer::DebugView::Motion);
    RenderFrames(*target, view, 2);
    EXPECT_EQ(target->Stats().renderWidth, size.width);
    target->SetDebugView(renderer::DebugView::Off);
    target->SetAntialiasing(renderer::Antialiasing::Off);
    RenderFrames(*target, view, 2);
    EXPECT_EQ(target->Stats().renderWidth, size.width);
    target->SetAntialiasing(renderer::Antialiasing::Taa);

    // A budget no frame can hold: dynamic resolution goes down to its floor.
    target->SetResolution({.scale = 1.0f, .dynamic = true, .minScale = 0.5f, .targetFps = 100000.0f});
    RenderFrames(*target, view, 12);
    EXPECT_FLOAT_EQ(target->Stats().renderScale, 0.5f);
    EXPECT_EQ(target->Device().ValidationErrorCount(), 0U);
}

TEST_F(RendererTest, RendersIntoTheViewsRectangle) {
    std::optional<renderer::Renderer> target = CreateRenderer();
    if (!target.has_value()) {
        GTEST_SKIP();
    }
    const core::Result<renderer::MeshHandle> mesh = target->CreateMesh(renderer::MakeCube(), "Cube");
    ASSERT_TRUE(mesh.has_value());
    renderer::SceneView view = TestView(*mesh);
    const platform::Extent size = m_window->PixelSize();
    target->SetResolution({.scale = 1.0f, .dynamic = false});

    // An editor's viewport between its panels (ADR-0055): the scene renders at its size.
    view.viewport =
        renderer::ViewRect{.x = size.width / 5, .y = 20, .width = size.width / 2, .height = size.height / 2};
    RenderFrames(*target, view, 3);
    EXPECT_EQ(target->Stats().renderWidth, size.width / 2);
    EXPECT_EQ(target->Stats().renderHeight, size.height / 2);

    // Dragging a splitter resizes it every frame; a rectangle past the window's edge is cut to it.
    for (const std::uint32_t width : {size.width / 3, size.width / 2 + 7, size.width}) {
        view.viewport->width = width;
        RenderFrames(*target, view, 1);
    }
    EXPECT_EQ(target->Stats().renderWidth, size.width - size.width / 5);

    // Too small to draw in: the whole window.
    view.viewport = renderer::ViewRect{.x = 0, .y = 0, .width = 4, .height = 4};
    RenderFrames(*target, view, 2);
    EXPECT_EQ(target->Stats().renderWidth, size.width);
    EXPECT_EQ(target->Device().ValidationErrorCount(), 0U);
}

TEST_F(RendererTest, UpscalesWithFsr) {
    std::optional<renderer::Renderer> target = CreateRenderer();
    if (!target.has_value()) {
        GTEST_SKIP();
    }
    const core::Result<renderer::MeshHandle> mesh = target->CreateMesh(renderer::MakeCube(), "Cube");
    ASSERT_TRUE(mesh.has_value());
    const renderer::SceneView view = TestView(*mesh);

    // AMD FSR 3.1 in the engine's upscaler's place (ADR-0050), at a scale that changes every frame, then
    // back to the engine's and to FSR again: each change of upscaler starts its history over.
    for (const float scale : {0.67f, 0.5f, 0.9f, 1.0f, 0.58f}) {
        target->SetResolution({.scale = scale, .dynamic = false, .upscaler = renderer::Upscaler::Fsr3});
        RenderFrames(*target, view, 2);
    }
    target->SetResolution({.scale = 0.67f, .dynamic = false});
    RenderFrames(*target, view, 2);
    target->SetResolution({.scale = 0.67f, .dynamic = false, .upscaler = renderer::Upscaler::Fsr3});
    RenderFrames(*target, view, 2);
    EXPECT_EQ(target->Device().ValidationErrorCount(), 0U);
}

TEST_F(RendererTest, ShowsEachDebugView) {
    std::optional<renderer::Renderer> target = CreateRenderer();
    if (!target.has_value()) {
        GTEST_SKIP();
    }
    const core::Result<renderer::MeshHandle> mesh = target->CreateMesh(renderer::MakeCube(), "Cube");
    ASSERT_TRUE(mesh.has_value());
    const renderer::SceneView view = TestView(*mesh);
    using renderer::DebugView;
    for (const DebugView debugView : {DebugView::Rays, DebugView::Path, DebugView::Motion, DebugView::Normals,
                                      DebugView::Indirect, DebugView::RadianceCache, DebugView::Reflections,
                                      DebugView::Lights, DebugView::Bloom, DebugView::Fog, DebugView::Off}) {
        target->SetDebugView(debugView);
        RenderFrames(*target, view, 3);
    }
    // Without indirect light, then with it again: the cache starts over.
    target->SetIndirectLight(false);
    RenderFrames(*target, view, 2);
    target->SetIndirectLight(true);
    RenderFrames(*target, view, 2);
    EXPECT_EQ(target->Device().ValidationErrorCount(), 0U);
}

TEST_F(RendererTest, DrawsTheLensAndShutter) {
    // Depth of field, motion blur, grain, vignette, chromatic aberration and lens dirt, all at once, then
    // none: frames draw without a validation error (ADR-0063).
    std::optional<renderer::Renderer> target = CreateRenderer();
    if (!target.has_value()) {
        GTEST_SKIP();
    }
    const core::Result<renderer::MeshHandle> mesh = target->CreateMesh(renderer::MakeCube(), "Cube");
    ASSERT_TRUE(mesh.has_value());
    renderer::SceneView view = TestView(*mesh);
    view.camera->aperture = 1.4f;
    view.camera->focusDistance = 1.0f;
    target->SetImage(
        {.filmGrain = 1.0f, .vignette = 1.0f, .chromaticAberration = 1.0f, .lensDirt = 1.0f, .motionBlur = 0.5f});
    RenderFrames(*target, view, 4);
    view.camera->aperture = 0.0f;
    target->SetImage({});
    RenderFrames(*target, view, 2);
    EXPECT_EQ(target->Device().ValidationErrorCount(), 0U);
}

TEST_F(RendererTest, CreatesNoPipelineOnceItHasDrawnAFrame) {
    // Every setting a player or a quality preset can change, and every debug view, after the first frame:
    // none may create a pipeline, which would stall a frame in play (ADR-0052).
    std::optional<renderer::Renderer> target = CreateRenderer();
    if (!target.has_value()) {
        GTEST_SKIP();
    }
    const core::Result<renderer::MeshHandle> mesh = target->CreateMesh(renderer::MakeCube(), "Cube");
    ASSERT_TRUE(mesh.has_value());
    renderer::SceneView view = TestView(*mesh);
    view.fogVolumes.push_back({.world = glm::mat4(1.0f), .density = 0.5f, .falloffHeight = 0.2f});
    RenderFrames(*target, view, 2);
    rhi::Device& device = target->Device();
    EXPECT_GT(device.PipelineCreationSeconds(), 0.0);
    device.LockPipelineCreation(true);

    using renderer::DebugView;
    for (const DebugView debugView : {DebugView::Rays, DebugView::Path, DebugView::Motion, DebugView::Normals,
                                      DebugView::Indirect, DebugView::RadianceCache, DebugView::Reflections,
                                      DebugView::Lights, DebugView::Bloom, DebugView::Fog, DebugView::Off}) {
        target->SetDebugView(debugView);
        RenderFrames(*target, view, 2);
    }
    for (const renderer::Upscaler upscaler : {renderer::Upscaler::Fsr3, renderer::Upscaler::Engine}) {
        for (const float scale : {0.5f, 0.67f, 1.0f}) {
            target->SetResolution({.scale = scale, .dynamic = false, .upscaler = upscaler});
            RenderFrames(*target, view, 2);
        }
    }
    target->SetResolution({.scale = 1.0f, .dynamic = true, .minScale = 0.5f});
    target->SetAntialiasing(renderer::Antialiasing::Off);
    RenderFrames(*target, view, 2);
    target->SetAntialiasing(renderer::Antialiasing::Taa);
    for (const bool hdr : {true, false}) {
        target->SetImage({.autoExposure = !hdr,
                          .bloom = hdr ? 0.0f : 0.04f,
                          .toneMapper = hdr ? renderer::ToneMapper::Neutral : renderer::ToneMapper::AgX,
                          .hdr = hdr});
        RenderFrames(*target, view, 2);
    }
    for (const bool on : {false, true}) {
        target->SetIndirectLight(on);
        target->SetDenoiser(on);
        target->SetReflections(on);
        target->SetReflectionRoughness(on ? 0.9f : 0.3f);
        target->SetRestir(on, !on);
        target->SetVolumetricFog(on, on ? 128 : 32, on ? 8 : 32);
        target->SetIndirectLightConfig({.bounces = on ? 3U : 0U, .rays = on ? 4U : 1U});
        RenderFrames(*target, view, 2);
    }
    EXPECT_EQ(device.LatePipelineCount(), 0U);
    EXPECT_EQ(device.ValidationErrorCount(), 0U);
}

TEST_F(RendererTest, AdaptsTheEyeWithinTheLevelsRange) {
    std::optional<renderer::Renderer> target = CreateRenderer();
    if (!target.has_value()) {
        GTEST_SKIP();
    }
    const core::Result<renderer::MeshHandle> mesh = target->CreateMesh(renderer::MakeCube(), "Cube");
    ASSERT_TRUE(mesh.has_value());
    // A cube that fills the view, a meter away, in black air.
    renderer::SceneView view = TestView(*mesh);
    view.draws[0].world = glm::scale(glm::mat4(1.0f), glm::vec3(4.0f));
    view.atmosphere.ev100 = 4.0f;
    view.atmosphere.autoExposure = true;
    view.atmosphere.ev100Min = 1.0f;
    view.atmosphere.ev100Max = 6.0f;
    view.timeStep = 1.0f;

    // Unlit, the frame is black: the eye opens as far as the level lets it, and no further.
    RenderFrames(*target, view, 30);
    renderer::FrameStats stats = target->Stats();
    ASSERT_FALSE(std::isnan(stats.meteredEv100));
    EXPECT_LT(stats.meteredEv100, 1.0f);
    EXPECT_NEAR(stats.ev100, 1.0f, 0.05f);
    EXPECT_EQ(target->ExposureHistogram().size(), 128U);

    // A lamp half a meter from the cube, thousands of nits on its face: the eye closes, to the level's brightest.
    view.lights.push_back({.position = glm::vec3(0.0f, 0.0f, 2.5f), .color = glm::vec3(50000.0f), .range = 20.0f});
    RenderFrames(*target, view, 30);
    stats = target->Stats();
    EXPECT_GT(stats.meteredEv100, 6.0f);
    EXPECT_NEAR(stats.ev100, 6.0f, 0.05f);
    EXPECT_FLOAT_EQ(stats.ev100Min, 1.0f);
    EXPECT_FLOAT_EQ(stats.ev100Max, 6.0f);

    // Without auto-exposure, the eye stays at the atmosphere's exposure.
    target->SetImage({.autoExposure = false});
    RenderFrames(*target, view, 3);
    EXPECT_FLOAT_EQ(target->Stats().ev100, 4.0f);
    EXPECT_TRUE(target->ExposureHistogram().empty());
    EXPECT_EQ(target->Device().ValidationErrorCount(), 0U);
}

TEST_F(RendererTest, DestroyedMeshIsSafeWhileFramesAreInFlight) {
    std::optional<renderer::Renderer> target = CreateRenderer();
    if (!target.has_value()) {
        GTEST_SKIP();
    }
    const core::Result<renderer::MeshHandle> mesh = target->CreateMesh(renderer::MakeCube(), "Cube");
    ASSERT_TRUE(mesh.has_value());
    const renderer::SceneView view = TestView(*mesh);

    RenderFrames(*target, view, 3);
    target->DestroyMesh(*mesh);
    RenderFrames(*target, view, 3);
    EXPECT_EQ(target->Device().ValidationErrorCount(), 0U);
}

TEST_F(RendererTest, CanBeStartedAndStoppedRepeatedly) {
    for (int attempt = 0; attempt < 3; ++attempt) {
        std::optional<renderer::Renderer> target = CreateRenderer();
        if (!target.has_value()) {
            GTEST_SKIP();
        }
        const core::Result<renderer::MeshHandle> mesh = target->CreateMesh(renderer::MakeCube(), "Cube");
        ASSERT_TRUE(mesh.has_value());
        RenderFrames(*target, TestView(*mesh), 3);
        EXPECT_EQ(target->Device().ValidationErrorCount(), 0U);
    }
}

TEST_F(RendererTest, CountsWhatItDrawsAndHolds) {
    std::optional<renderer::Renderer> target = CreateRenderer();
    if (!target.has_value()) {
        GTEST_SKIP();
    }
    const rhi::MemoryStats before = target->Device().Memory();
    const core::Result<renderer::MeshHandle> mesh = target->CreateMesh(renderer::MakeCube(), "Cube");
    ASSERT_TRUE(mesh.has_value());
    EXPECT_EQ(target->Device().Memory().buffers, before.buffers + 2) << "Vertices and indices";
    EXPECT_GT(before.budgetBytes, 0U);

    // A second cube far beyond the view distance is not drawn.
    renderer::SceneView view = TestView(*mesh);
    view.draws.push_back({*mesh, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -5000.0f))});
    RenderFrames(*target, view, 2);

    const renderer::FrameStats stats = target->Stats();
    EXPECT_EQ(stats.draws, 2U);
    EXPECT_EQ(stats.drawnDraws, 1U);
    EXPECT_EQ(stats.triangles, 12U);
    EXPECT_EQ(stats.lights, 0U);
    EXPECT_EQ(stats.meshes, 1U);
    EXPECT_GT(target->Device().Memory().usedBytes, 0U);
}
