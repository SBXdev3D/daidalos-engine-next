// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/platform/Platform.hpp>
#include <daidalos/renderer/Renderer.hpp>

#include <glm/ext/matrix_transform.hpp>
#include <gtest/gtest.h>

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
        core::Result<renderer::Renderer> created = renderer::Renderer::Create(*m_window, {.validation = true});
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
    const core::Result<renderer::MeshHandle> mesh = target->CreateMesh(renderer::MakeCube(glm::vec3(1.0f)), "Cube");
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
    const core::Result<renderer::MeshHandle> mesh = target->CreateMesh(renderer::MakeCube(glm::vec3(1.0f)), "Cube");
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

TEST_F(RendererTest, DestroyedMeshIsSafeWhileFramesAreInFlight) {
    std::optional<renderer::Renderer> target = CreateRenderer();
    if (!target.has_value()) {
        GTEST_SKIP();
    }
    const core::Result<renderer::MeshHandle> mesh = target->CreateMesh(renderer::MakeCube(glm::vec3(1.0f)), "Cube");
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
        const core::Result<renderer::MeshHandle> mesh = target->CreateMesh(renderer::MakeCube(glm::vec3(1.0f)), "Cube");
        ASSERT_TRUE(mesh.has_value());
        RenderFrames(*target, TestView(*mesh), 3);
        EXPECT_EQ(target->Device().ValidationErrorCount(), 0U);
    }
}
