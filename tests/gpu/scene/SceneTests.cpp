// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/app/Application.hpp>
#include <daidalos/ecs/Transform.hpp>
#include <daidalos/renderer/Components.hpp>

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <string>

using namespace daidalos;

namespace {

class FunctionHandler final : public app::IApplicationHandler {
public:
    core::Result<void> OnStart(app::Application& app) override { return start ? start(app) : core::Result<void>{}; }
    void OnUpdate(app::Application& app, const core::FrameTime& time) override {
        if (update) {
            update(app, time.frameIndex);
        }
    }

    std::function<core::Result<void>(app::Application&)> start;
    std::function<void(app::Application&, std::uint64_t)> update;
};

app::ApplicationConfig RenderingConfig() {
    app::ApplicationConfig config;
    config.window.width = 64;
    config.window.height = 64;
    config.rendering = true;
    config.arguments = {"+renderer.validation", "true"};
    return config;
}

/// Skips the tests on machines without a display or a Vulkan window, like continuous integration.
class ApplicationScene : public ::testing::Test {
protected:
    void SetUp() override {
        core::Result<platform::Platform> created = platform::Platform::Create();
        if (!created) {
            GTEST_SKIP() << core::Format(created.error());
        }
        platform::WindowConfig config;
        config.width = 64;
        config.height = 64;
        config.vulkan = true;
        if (core::Result<platform::Window> window = platform::Window::Create(*created, config); !window) {
            GTEST_SKIP() << core::Format(window.error());
        }
    }
};

/// Runs the handler, or skips the test on machines without Vulkan 1.3.
#define RUN_OR_SKIP(handler)                                                                                           \
    app::Application application(RenderingConfig());                                                                   \
    const core::Result<void> result = application.Run(handler);                                                        \
    if (!result && result.error().code == core::ErrorCode::NotSupported) {                                             \
        GTEST_SKIP() << core::Format(result.error());                                                                  \
    }

constexpr const char* kModel = "quad.gltf";

TEST_F(ApplicationScene, FailedStartReleasesTheSceneBeforeTheRenderer) {
    FunctionHandler handler;
    handler.start = [](app::Application& app) -> core::Result<void> {
        auto scene = std::make_unique<scene::Scene>(app.Renderer(), DAI_TEST_ASSET_DIR);
        core::Result<const renderer::Model*> model = scene->Assets().LoadModel(kModel);
        EXPECT_TRUE(model.has_value());
        app.SetScene(std::move(scene));
        return core::MakeError(core::ErrorCode::InvalidArgument, "test", "The start fails");
    };
    RUN_OR_SKIP(handler);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, core::ErrorCode::InvalidArgument);
}

TEST_F(ApplicationScene, SceneIsReplacedBetweenFrames) {
    scene::Scene* first = nullptr;
    scene::Scene* second = nullptr;
    FunctionHandler handler;
    handler.start = [&first](app::Application& app) -> core::Result<void> {
        auto scene = std::make_unique<scene::Scene>(app.Renderer(), DAI_TEST_ASSET_DIR);
        first = scene.get();
        app.SetScene(std::move(scene));
        EXPECT_EQ(app.CurrentScene(), first);
        return {};
    };
    handler.update = [&](app::Application& app, std::uint64_t frame) {
        if (frame == 0) {
            auto scene = std::make_unique<scene::Scene>(app.Renderer(), DAI_TEST_ASSET_DIR);
            second = scene.get();
            app.SetScene(std::move(scene));
            EXPECT_EQ(app.CurrentScene(), first) << "The swap waits for the end of the frame";
        } else {
            EXPECT_EQ(app.CurrentScene(), second);
            app.RequestQuit();
        }
    };
    RUN_OR_SKIP(handler);
    EXPECT_TRUE(result.has_value());
}

TEST_F(ApplicationScene, SystemsRunAndTheCurrentSceneIsDrawn) {
    int frameRuns = 0;
    FunctionHandler handler;
    handler.start = [&frameRuns](app::Application& app) -> core::Result<void> {
        auto scene = std::make_unique<scene::Scene>(app.Renderer(), DAI_TEST_ASSET_DIR);
        scene->FrameSystems().Add("Count",
                                  [&frameRuns](ecs::World& /*world*/, core::Seconds /*delta*/) { ++frameRuns; });
        const ecs::Entity camera = scene->World().Create();
        scene->World().Add<ecs::LocalTransform>(camera);
        scene->World().Add<renderer::Camera>(camera);
        scene->SetActiveCamera(camera);
        scene->SetAtmosphere({{0.1f, 0.2f, 0.3f}, 0.1f});
        app.SetScene(std::move(scene));
        return {};
    };
    handler.update = [](app::Application& app, std::uint64_t frame) {
        if (frame == 3) {
            app.RequestQuit();
        }
    };
    RUN_OR_SKIP(handler);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(frameRuns, 4);
}

TEST_F(ApplicationScene, ModelLoadsOnAWorkerDuringTheLoop) {
    bool loaded = false;
    FunctionHandler handler;
    handler.start = [&loaded](app::Application& app) -> core::Result<void> {
        app.SetScene(std::make_unique<scene::Scene>(app.Renderer(), DAI_TEST_ASSET_DIR));
        app.CurrentScene()->Assets().LoadModelAsync(kModel, app.Workers(),
                                                    [&loaded](core::Result<const renderer::Model*> model) {
                                                        EXPECT_TRUE(model.has_value());
                                                        loaded = true;
                                                    });
        return {};
    };
    handler.update = [&loaded](app::Application& app, std::uint64_t frame) {
        if (loaded || frame > 100000) {
            app.RequestQuit();
        }
    };
    RUN_OR_SKIP(handler);
    EXPECT_TRUE(result.has_value());
    EXPECT_TRUE(loaded);
}

TEST_F(ApplicationScene, MissingModelReportsNotFoundThroughTheCallback) {
    core::ErrorCode code = core::ErrorCode::InvalidArgument;
    bool called = false;
    FunctionHandler handler;
    handler.start = [&](app::Application& app) -> core::Result<void> {
        app.SetScene(std::make_unique<scene::Scene>(app.Renderer(), DAI_TEST_ASSET_DIR));
        app.CurrentScene()->Assets().LoadModelAsync("missing.gltf", app.Workers(),
                                                    [&](core::Result<const renderer::Model*> model) {
                                                        ASSERT_FALSE(model.has_value());
                                                        code = model.error().code;
                                                        called = true;
                                                    });
        return {};
    };
    handler.update = [&called](app::Application& app, std::uint64_t frame) {
        if (called || frame > 100000) {
            app.RequestQuit();
        }
    };
    RUN_OR_SKIP(handler);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(code, core::ErrorCode::NotFound);
}

TEST_F(ApplicationScene, SceneDestroyedWhileLoadingNeverCallsBack) {
    bool called = false;
    FunctionHandler handler;
    handler.start = [&called](app::Application& app) -> core::Result<void> {
        app.SetScene(std::make_unique<scene::Scene>(app.Renderer(), DAI_TEST_ASSET_DIR));
        app.CurrentScene()->Assets().LoadModelAsync(
            kModel, app.Workers(), [&called](core::Result<const renderer::Model*> /*model*/) { called = true; });
        app.SetScene(nullptr);
        return {};
    };
    handler.update = [](app::Application& app, std::uint64_t frame) {
        if (frame == 5) {
            app.RequestQuit();
        }
    };
    RUN_OR_SKIP(handler);
    EXPECT_TRUE(result.has_value());
    EXPECT_FALSE(called);
}

} // namespace
