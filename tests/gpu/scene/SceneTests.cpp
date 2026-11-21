// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/app/Application.hpp>
#include <daidalos/ecs/Transform.hpp>
#include <daidalos/scene/Components.hpp>
#include <daidalos/scene/Level.hpp>
#include <daidalos/scene/SaveGame.hpp>
#include <daidalos/scene/Scene.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

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
    config.arguments = {"+renderer.validation", "true", "+renderer.sync_validation", "true"};
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
        core::Result<const scene::Model*> model = scene->Assets().LoadModel(kModel);
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
        scene->World().Add<scene::Camera>(camera);
        scene->SetActiveCamera(camera);
        scene->SetAtmosphere({.fog = {.density = 0.1f, .albedo = {0.1f, 0.2f, 0.3f}}});
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

TEST_F(ApplicationScene, AScenesFirstViewAndATeleportCutTheView) {
    FunctionHandler handler;
    std::vector<bool> cuts;
    handler.start = [&cuts](app::Application& app) -> core::Result<void> {
        scene::Scene scene(app.Renderer(), DAI_TEST_ASSET_DIR);
        renderer::SceneView view;
        for (int frame = 0; frame < 4; ++frame) {
            if (frame == 2) {
                scene.CutView();
            }
            scene.Extract(view);
            cuts.push_back(view.cut);
        }
        app.RequestQuit();
        return {};
    };
    RUN_OR_SKIP(handler);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(cuts, (std::vector<bool>{true, false, true, false}));
}

TEST_F(ApplicationScene, PausedSceneRunsNoSystems) {
    int frameRuns = 0;
    FunctionHandler handler;
    handler.start = [&frameRuns](app::Application& app) -> core::Result<void> {
        auto scene = std::make_unique<scene::Scene>(app.Renderer(), DAI_TEST_ASSET_DIR);
        scene->FrameSystems().Add("Count",
                                  [&frameRuns](ecs::World& /*world*/, core::Seconds /*delta*/) { ++frameRuns; });
        app.SetScene(std::move(scene));
        return {};
    };
    handler.update = [](app::Application& app, std::uint64_t frame) {
        // Frames 1 and 2 are paused; the scene runs after the handler's update.
        if (frame == 1) {
            app.CurrentScene()->SetPaused(true);
            EXPECT_TRUE(app.CurrentScene()->IsPaused());
        } else if (frame == 3) {
            app.CurrentScene()->SetPaused(false);
        } else if (frame == 4) {
            app.RequestQuit();
        }
    };
    RUN_OR_SKIP(handler);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(frameRuns, 3);
}

TEST_F(ApplicationScene, ModelLoadsOnAWorkerDuringTheLoop) {
    bool loaded = false;
    FunctionHandler handler;
    handler.start = [&loaded](app::Application& app) -> core::Result<void> {
        app.SetScene(std::make_unique<scene::Scene>(app.Renderer(), DAI_TEST_ASSET_DIR));
        app.CurrentScene()->Assets().LoadModelAsync(kModel, app.Workers(),
                                                    [&loaded](core::Result<const scene::Model*> model) {
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
                                                    [&](core::Result<const scene::Model*> model) {
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
            kModel, app.Workers(), [&called](core::Result<const scene::Model*> /*model*/) { called = true; });
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

#if DAI_DEV_TOOLS
TEST_F(ApplicationScene, ToolsDrawEveryWindowOverAScene) {
    FunctionHandler handler;
    ecs::Entity light;
    handler.start = [&light](app::Application& app) -> core::Result<void> {
        auto scene = std::make_unique<scene::Scene>(app.Renderer(), DAI_TEST_ASSET_DIR);
        DAI_TRY_ASSIGN(const scene::Model* model, scene->Assets().LoadModel(kModel));
        DAI_TRY(scene::Instantiate(scene->World(), *model));
        light = scene->World().Create();
        scene->World().Add<ecs::LocalTransform>(light);
        scene->World().Add<scene::Light>(light);
        scene->World().Add<scene::NodeName>(light, scene::NodeName{"Lamp"});
        app.SetScene(std::move(scene));

        app::DevTools* tools = app.Tools();
        EXPECT_NE(tools, nullptr);
        if (tools != nullptr) {
            tools->AddWindow("Game", [] {});
        }
        return {};
    };
    std::uint64_t validationErrors = 0;
    handler.update = [&light, &validationErrors](app::Application& app, std::uint64_t frame) {
        app::DevTools& tools = *app.Tools();
        if (frame == 1) {
            app.SetToolsOpen(true);
            EXPECT_TRUE(tools.IsOpen());
            tools.GetConsole().Execute("windows");
            tools.GetConsole().Execute("view.fov 70");
            tools.Select(light);
        } else if (frame == 4) {
            app.SetToolsOpen(false);
        } else if (frame == 6) {
            validationErrors = app.Renderer().Device().ValidationErrorCount();
            tools.GetConsole().Execute("quit");
        }
    };
    RUN_OR_SKIP(handler);
    ASSERT_TRUE(result.has_value()) << core::Format(result.error());
    EXPECT_DOUBLE_EQ(std::get<double>(application.CVars().Find("view.fov")->value), 70.0);
    EXPECT_EQ(validationErrors, 0U);
}
#endif

TEST_F(ApplicationScene, ATriggerPlaysItsSequenceOnce) {
    // A trigger at the origin, a sequence that moves a crate and sends an event (ADR-0071), and something that
    // sets triggers off walking in, out and in again.
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "daidalos-trigger-test";
    std::filesystem::create_directories(root);
    std::ofstream(root / "boo.sequence.json")
        << R"({"cues": [{"at": 0, "event": "boo"}, {"at": 0, "move": {"entity": "Crate", "to": [0, 3, 0], "seconds": 0}}]})";
    std::vector<std::string> events;
    glm::vec3 crate{0.0f};
    FunctionHandler handler;
    handler.start = [&](app::Application& app) -> core::Result<void> {
        scene::Scene scene(app.Renderer(), root);
        ecs::World& world = scene.World();
        const ecs::Entity trigger = world.Create();
        world.Add<ecs::LocalTransform>(trigger).scale = glm::vec3(2.0f);
        world.Add<scene::Trigger>(trigger, scene::Trigger{.sequence = "boo.sequence.json"});
        const ecs::Entity box = world.Create();
        world.Add<ecs::LocalTransform>(box);
        world.Add<scene::NodeName>(box, "Crate");
        const ecs::Entity walker = world.Create();
        world.Add<ecs::LocalTransform>(walker).translation = glm::vec3(5.0f, 0.0f, 0.0f);
        world.Add<scene::TriggerActivator>(walker);
        const auto frame = [&] {
            ecs::PropagateTransforms(world);
            scene.RunFrame(core::Seconds(0.1), 0.0f);
            for (std::string& event : scene.TakeEvents()) {
                events.push_back(std::move(event));
            }
        };
        frame();
        for (const float x : {0.0f, 5.0f, 0.0f}) {
            world.Get<ecs::LocalTransform>(walker).translation.x = x;
            frame();
            frame();
        }
        crate = world.Get<ecs::LocalTransform>(box).translation;
        app.RequestQuit();
        return {};
    };
    RUN_OR_SKIP(handler);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(events, (std::vector<std::string>{"boo"}));
    EXPECT_EQ(crate, glm::vec3(0.0f, 3.0f, 0.0f));
    std::filesystem::remove_all(root);
}

TEST_F(ApplicationScene, CaptionsLastTheirTimeOnceEachAndAFewAtATime) {
    // ADR-0075: a subtitle cue shows its line; the same line again starts over rather than showing twice;
    // past kMaxCaptions the oldest goes; each goes when its seconds are up.
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "daidalos-caption-test";
    std::filesystem::create_directories(root);
    std::ofstream(root / "line.sequence.json")
        << R"({"cues": [{"at": 0, "subtitle": {"speaker": "speaker.you", "text": "subtitle.a", "seconds": 0.5}}]})";
    std::size_t afterCue = 0;
    std::size_t afterRepeat = 0;
    std::size_t afterMany = 0;
    std::size_t afterTime = 0;
    std::string oldestLeft;
    FunctionHandler handler;
    handler.start = [&](app::Application& app) -> core::Result<void> {
        scene::Scene scene(app.Renderer(), root);
        const auto frame = [&] { scene.RunFrame(core::Seconds(0.1), 0.0f); };
        DAI_TRY(scene.PlaySequenceFile("line.sequence.json"));
        frame();
        afterCue = scene.Captions().size();
        scene.ShowCaption({.text = "caption.x", .seconds = 2.0f});
        scene.ShowCaption({.text = "caption.x", .seconds = 2.0f});
        afterRepeat = scene.Captions().size();
        for (int index = 0; index < 6; ++index) {
            scene.ShowCaption({.text = "caption." + std::to_string(index), .seconds = 2.0f});
        }
        afterMany = scene.Captions().size();
        oldestLeft = scene.Captions().front().text;
        for (int index = 0; index < 25; ++index) {
            frame();
        }
        afterTime = scene.Captions().size();
        app.RequestQuit();
        return {};
    };
    RUN_OR_SKIP(handler);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(afterCue, 1U);
    EXPECT_EQ(afterRepeat, 2U);
    EXPECT_EQ(afterMany, scene::Scene::kMaxCaptions);
    EXPECT_EQ(oldestLeft, "caption.2");
    EXPECT_EQ(afterTime, 0U);
    std::filesystem::remove_all(root);
}

TEST_F(ApplicationScene, ACameraCueSaysWhereItHasTheCamera) {
    // ADR-0120: while a smooth camera cue has the camera, the game can ask where it is (to open doors on its
    // way); before it and once it is skipped, nothing has it.
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "daidalos-cinematic-test";
    std::filesystem::create_directories(root);
    std::ofstream(root / "flight.sequence.json") << R"({"cues": [{"at": 0, "camera": {"path": "smooth", "keys": [
        {"at": 0, "position": [0, 2, 0], "target": [0, 2, -1]},
        {"at": 1, "position": [0, 2, -5], "target": [0, 2, -6]},
        {"at": 2, "position": [0, 2, -10], "target": [0, 2, -11]}]}}]})";
    std::optional<glm::vec3> before;
    std::optional<glm::vec3> during;
    std::optional<glm::vec3> skipped;
    FunctionHandler handler;
    handler.start = [&](app::Application& app) -> core::Result<void> {
        scene::Scene scene(app.Renderer(), root);
        before = scene.CinematicPosition();
        DAI_TRY(scene.PlaySequenceFile("flight.sequence.json"));
        for (int index = 0; index < 10; ++index) {
            scene.RunFrame(core::Seconds(0.1), 0.0f);
        }
        during = scene.CinematicPosition();
        scene.StopCinematic();
        skipped = scene.CinematicPosition();
        app.RequestQuit();
        return {};
    };
    RUN_OR_SKIP(handler);
    EXPECT_TRUE(result.has_value());
    EXPECT_FALSE(before.has_value());
    ASSERT_TRUE(during.has_value());
    // About a second in, near the middle key, and moving on: not stopped at either end.
    EXPECT_LT(during->z, -3.0f);
    EXPECT_GT(during->z, -7.0f);
    EXPECT_FALSE(skipped.has_value());
    std::filesystem::remove_all(root);
}

TEST_F(ApplicationScene, ASavedGameRestoresTheWorldItLeft) {
    // A world made from the same "level" twice (ADR-0072): changes made to the first come back in the second.
    struct Made {
        ecs::Entity crate;
        ecs::Entity lamp;
        ecs::Entity trigger;
        ecs::Entity keyed;
    };
    const auto make = [](scene::Scene& scene) {
        ecs::World& world = scene.World();
        Made made;
        made.crate = world.Create();
        world.Add<ecs::LocalTransform>(made.crate);
        world.Add<scene::LevelEntityId>(made.crate, std::uint64_t{7});
        made.lamp = world.Create();
        world.Add<ecs::LocalTransform>(made.lamp);
        world.Add<scene::LevelEntityId>(made.lamp, std::uint64_t{8});
        world.Add<scene::Light>(made.lamp);
        made.trigger = world.Create();
        world.Add<ecs::LocalTransform>(made.trigger);
        world.Add<scene::LevelEntityId>(made.trigger, std::uint64_t{9});
        world.Add<scene::Trigger>(made.trigger, scene::Trigger{.sequence = "x.sequence.json"});
        made.keyed = world.Create();
        world.Add<ecs::LocalTransform>(made.keyed);
        world.Add<scene::SaveKey>(made.keyed, "door#0");
        return made;
    };
    scene::SaveData saved;
    glm::vec3 crate{0.0f};
    glm::vec3 door{0.0f};
    bool lamp = true;
    bool fired = false;
    bool power = true;
    FunctionHandler handler;
    handler.start = [&](app::Application& app) -> core::Result<void> {
        {
            scene::Scene first(app.Renderer(), DAI_TEST_ASSET_DIR);
            const Made made = make(first);
            ecs::World& world = first.World();
            world.Get<ecs::LocalTransform>(made.crate).translation = {1.0f, 2.0f, 3.0f};
            world.Get<ecs::LocalTransform>(made.keyed).translation = {0.0f, 2.5f, 0.0f};
            world.Get<scene::Light>(made.lamp).enabled = false;
            world.Add<scene::TriggerState>(made.trigger).fired = true;
            first.Power().Set("hall", false, 0.0);
            scene::SaveScene(first, saved);
        }
        scene::Scene second(app.Renderer(), DAI_TEST_ASSET_DIR);
        const Made made = make(second);
        scene::LoadScene(second, saved);
        ecs::World& world = second.World();
        crate = world.Get<ecs::LocalTransform>(made.crate).translation;
        door = world.Get<ecs::LocalTransform>(made.keyed).translation;
        lamp = world.Get<scene::Light>(made.lamp).enabled;
        fired = world.Get<scene::TriggerState>(made.trigger).fired;
        power = second.Power().IsOn("hall");
        app.RequestQuit();
        return {};
    };
    RUN_OR_SKIP(handler);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(crate, glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(door, glm::vec3(0.0f, 2.5f, 0.0f));
    EXPECT_FALSE(lamp);
    EXPECT_TRUE(fired);
    EXPECT_FALSE(power);
}
