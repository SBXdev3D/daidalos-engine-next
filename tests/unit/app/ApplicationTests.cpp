// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/app/Application.hpp>
#include <daidalos/platform/CrashHandler.hpp>

#include <SDL3/SDL_events.h>
#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

using namespace daidalos;
using app::Application;
using app::ApplicationConfig;

namespace {

class CaptureSink final : public logging::ISink {
public:
    void Write(logging::Level level, std::string_view module, std::string_view message) override {
        if (level >= logging::Level::Warning) {
            warnings.push_back(std::string(module) + ": " + std::string(message));
        }
    }

    std::vector<std::string> warnings;
};

// Records the calls it receives and quits after a given number of frames.
class RecordingHandler : public app::IApplicationHandler {
public:
    core::Result<void> OnStart(Application& app) override {
        calls.emplace_back("start");
        windowSize = app.MainWindow().Size();
        if (startError) {
            return tl::unexpected(*startError);
        }
        return {};
    }

    void OnEvent(Application& /*app*/, const platform::Event& event) override { events.push_back(event); }

    void OnFixedUpdate(Application& /*app*/, core::Seconds /*step*/) override { ++fixedUpdates; }

    void OnUpdate(Application& app, const core::FrameTime& time) override {
        calls.emplace_back("update");
        frameIndices.push_back(time.frameIndex);
        if (onUpdate) {
            onUpdate(app, time);
        }
        if (time.frameIndex + 1 >= quitAfterFrames) {
            app.RequestQuit();
        }
    }

    void OnStop(Application& /*app*/) override { calls.emplace_back("stop"); }

    std::vector<std::string> calls;
    std::vector<std::uint64_t> frameIndices;
    std::vector<platform::Event> events;
    int fixedUpdates = 0;
    platform::Extent windowSize;
    std::uint64_t quitAfterFrames = 3;
    std::optional<core::Error> startError;
    std::function<void(Application&, const core::FrameTime&)> onUpdate;
};

void PushEvent(SDL_EventType type, platform::WindowId window = {}) {
    SDL_Event event{};
    event.type = type;
    event.window.windowID = static_cast<SDL_WindowID>(window);
    ASSERT_TRUE(SDL_PushEvent(&event));
}

class ApplicationTest : public testing::Test {
protected:
    ApplicationTest() {
        m_config.log.console = false;
        m_config.log.sinks = {&m_sink};
        m_config.platform.videoDriver = "dummy";
        m_config.window.width = 320;
        m_config.window.height = 200;
        // A user.cfg left next to the tests must not change them.
        m_config.userConfigFile.clear();
    }

    // Asserts that everything Run started has been stopped.
    static void ExpectEverythingStopped() {
        EXPECT_FALSE(logging::IsEnabled(logging::Level::Critical)) << "Logging is still running";
        EXPECT_TRUE(platform::Platform::Create({.videoDriver = "dummy"}).has_value()) << "The platform is still alive";
    }

    ApplicationConfig m_config;
    CaptureSink m_sink;
    RecordingHandler m_handler;
};

} // namespace

TEST_F(ApplicationTest, RunCallsHandlerInOrderAndShutsDown) {
    Application app(m_config);

    const core::Result<void> result = app.Run(m_handler);

    ASSERT_TRUE(result.has_value()) << core::Format(result.error());
    EXPECT_EQ(m_handler.calls, (std::vector<std::string>{"start", "update", "update", "update", "stop"}));
    EXPECT_EQ(m_handler.frameIndices, (std::vector<std::uint64_t>{0, 1, 2}));
    ExpectEverythingStopped();
}

TEST_F(ApplicationTest, RunsWithOrWithoutAnAudioOutput) {
    m_config.audio = true;
    app::Application application(m_config);
    RecordingHandler handler;
    EXPECT_TRUE(application.Run(handler).has_value());
    ExpectEverythingStopped();
}

TEST_F(ApplicationTest, MainWindowUsesConfiguredSize) {
    Application app(m_config);

    ASSERT_TRUE(app.Run(m_handler).has_value());

    EXPECT_EQ(m_handler.windowSize, (platform::Extent{320, 200}));
}

TEST_F(ApplicationTest, StartFailureIsReturnedAndRollsBackEverything) {
    m_handler.startError = core::Error{core::ErrorCode::NotFound, "game", "Missing level"};
    Application app(m_config);

    const core::Result<void> result = app.Run(m_handler);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, core::ErrorCode::NotFound);
    EXPECT_EQ(result.error().message, "Starting application failed: Missing level");
    EXPECT_EQ(m_handler.calls, (std::vector<std::string>{"start"})) << "OnStop must not follow a failed OnStart";
    ExpectEverythingStopped();
}

TEST_F(ApplicationTest, PlatformFailureIsReturnedAndRollsBackEverything) {
    m_config.platform.videoDriver = "no-such-driver";
    Application app(m_config);

    const core::Result<void> result = app.Run(m_handler);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, core::ErrorCode::DeviceError);
    EXPECT_TRUE(m_handler.calls.empty());
    ExpectEverythingStopped();
}

TEST_F(ApplicationTest, ClosingTheMainWindowEndsTheLoop) {
    m_handler.quitAfterFrames = 100;
    m_handler.onUpdate = [](Application& app, const core::FrameTime& time) {
        if (time.frameIndex == 1) {
            PushEvent(SDL_EVENT_WINDOW_CLOSE_REQUESTED, app.MainWindow().Id());
        }
    };
    Application app(m_config);

    ASSERT_TRUE(app.Run(m_handler).has_value());

    EXPECT_EQ(m_handler.frameIndices.size(), 2u) << "No update runs after the close request";
    EXPECT_EQ(m_handler.calls.back(), "stop");
}

TEST_F(ApplicationTest, SystemQuitEndsTheLoop) {
    m_handler.quitAfterFrames = 100;
    m_handler.onUpdate = [](Application&, const core::FrameTime& time) {
        if (time.frameIndex == 0) {
            PushEvent(SDL_EVENT_QUIT);
        }
    };
    Application app(m_config);

    ASSERT_TRUE(app.Run(m_handler).has_value());

    EXPECT_EQ(m_handler.frameIndices.size(), 1u);
}

TEST_F(ApplicationTest, CloseRequestForAnotherWindowDoesNotEndTheLoop) {
    m_handler.quitAfterFrames = 5;
    m_handler.onUpdate = [](Application&, const core::FrameTime& time) {
        if (time.frameIndex == 0) {
            PushEvent(SDL_EVENT_WINDOW_CLOSE_REQUESTED, static_cast<platform::WindowId>(9999));
        }
    };
    Application app(m_config);

    ASSERT_TRUE(app.Run(m_handler).has_value());

    EXPECT_EQ(m_handler.frameIndices.size(), 5u);
}

TEST_F(ApplicationTest, EventsAreForwardedToTheHandler) {
    m_handler.quitAfterFrames = 100;
    m_handler.onUpdate = [](Application&, const core::FrameTime& time) {
        if (time.frameIndex == 0) {
            PushEvent(SDL_EVENT_QUIT);
        }
    };
    Application app(m_config);

    ASSERT_TRUE(app.Run(m_handler).has_value());

    ASSERT_FALSE(m_handler.events.empty());
    EXPECT_EQ(m_handler.events.back(), platform::Event(platform::QuitRequested{}));
}

TEST_F(ApplicationTest, InputStateFollowsTheFramesEvents) {
    // Frame 0 queues a key press, which frame 1 processes; frame 2 sees the key still held.
    std::vector<bool> pressed;
    std::vector<bool> down;
    m_handler.onUpdate = [&](Application& app, const core::FrameTime& time) {
        pressed.push_back(app.Input().WasPressed(platform::Key::Space));
        down.push_back(app.Input().IsDown(platform::Key::Space));
        if (time.frameIndex == 0) {
            SDL_Event event{};
            event.type = SDL_EVENT_KEY_DOWN;
            event.key.scancode = SDL_SCANCODE_SPACE;
            event.key.down = true;
            ASSERT_TRUE(SDL_PushEvent(&event));
        }
    };
    Application app(m_config);

    ASSERT_TRUE(app.Run(m_handler).has_value());

    EXPECT_EQ(pressed, (std::vector<bool>{false, true, false}));
    EXPECT_EQ(down, (std::vector<bool>{false, true, true}));
}

TEST_F(ApplicationTest, CommandLineOverridesWindowSize) {
    m_config.arguments = {"+window.width", "640", "+window.height", "480"};
    Application app(m_config);

    ASSERT_TRUE(app.Run(m_handler).has_value());

    EXPECT_EQ(m_handler.windowSize, (platform::Extent{640, 480}));
    EXPECT_EQ(app.CVars().Find("window.width")->value, core::CVarValue(std::int64_t{640}));
}

TEST_F(ApplicationTest, ConfigFileSetsWindowSizeAndCommandLineWins) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "daidalos_app_test.cfg";
    {
        std::ofstream file(path, std::ios::binary);
        file << "window.width 400\nwindow.height 300\n";
    }
    m_config.configFiles = {path};
    m_config.arguments = {"+window.height", "100"};
    Application app(m_config);

    const core::Result<void> result = app.Run(m_handler);
    std::filesystem::remove(path);

    ASSERT_TRUE(result.has_value()) << core::Format(result.error());
    EXPECT_EQ(m_handler.windowSize, (platform::Extent{400, 100}));
}

TEST_F(ApplicationTest, LaterConfigFileWinsAndMissingOnesAreSkipped) {
    const std::filesystem::path engine = std::filesystem::temp_directory_path() / "daidalos_app_engine.cfg";
    const std::filesystem::path game = std::filesystem::temp_directory_path() / "daidalos_app_game.cfg";
    {
        std::ofstream(engine, std::ios::binary) << "window.width 400\nwindow.height 300\n";
        std::ofstream(game, std::ios::binary) << "window.height 250\n";
    }
    m_config.configFiles = {engine, std::filesystem::temp_directory_path() / "daidalos_missing_app.cfg", game};
    Application app(m_config);

    const core::Result<void> result = app.Run(m_handler);
    std::filesystem::remove(engine);
    std::filesystem::remove(game);

    ASSERT_TRUE(result.has_value()) << core::Format(result.error());
    EXPECT_EQ(m_handler.windowSize, (platform::Extent{400, 250}));
}

TEST_F(ApplicationTest, PlayerSettingsComeAfterConfigFilesAndBeforeTheCommandLine) {
    const std::filesystem::path engine = std::filesystem::temp_directory_path() / "daidalos_app_engine.cfg";
    const std::filesystem::path user = std::filesystem::temp_directory_path() / "daidalos_app_user.cfg";
    {
        std::ofstream(engine, std::ios::binary) << "window.width 400\nwindow.height 300\n";
        std::ofstream(user, std::ios::binary) << "window.width 500\nwindow.height 350\n";
    }
    m_config.configFiles = {engine};
    m_config.userConfigFile = user;
    m_config.arguments = {"+window.height", "100"};
    Application app(m_config);

    const core::Result<void> result = app.Run(m_handler);
    std::filesystem::remove(engine);
    std::filesystem::remove(user);

    ASSERT_TRUE(result.has_value()) << core::Format(result.error());
    EXPECT_EQ(m_handler.windowSize, (platform::Extent{500, 100}));
}

TEST_F(ApplicationTest, ChangedSettingsAreSavedWithTheEarlierOnesButNotTheCommandLine) {
    const std::filesystem::path user = std::filesystem::temp_directory_path() / "daidalos_app_saved.cfg";
    std::ofstream(user, std::ios::binary) << "view.fov 75\n";
    m_config.userConfigFile = user;
    m_config.arguments = {"+window.width", "640"};
    m_handler.quitAfterFrames = 1;
    m_handler.onUpdate = [](Application& app, const core::FrameTime& /*time*/) {
        EXPECT_TRUE(app.ChangeSetting("input.mouse_sensitivity", "0.004").has_value());
        EXPECT_TRUE(app.ChangeSetting("renderer.antialiasing", "off").has_value());
        const core::Result<void> unknown = app.ChangeSetting("input.unknown", "1");
        ASSERT_FALSE(unknown.has_value());
        EXPECT_EQ(unknown.error().code, core::ErrorCode::NotFound);
        EXPECT_FALSE(app.ChangeSetting("window.fullscreen", "maybe").has_value());
        EXPECT_FLOAT_EQ(app.MouseSensitivity(), 0.004f);
        const core::Result<void> saved = app.SaveSettings();
        EXPECT_TRUE(saved.has_value()) << core::Format(saved.error());
    };
    Application app(m_config);

    const core::Result<void> result = app.Run(m_handler);
    std::ifstream file(user, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    std::filesystem::remove(user);

    ASSERT_TRUE(result.has_value()) << core::Format(result.error());
    const core::Result<std::vector<core::ConfigLine>> lines = core::ParseConfigLines(text, "saved");
    ASSERT_TRUE(lines.has_value()) << text;
    ASSERT_EQ(lines->size(), 3u) << text;
    EXPECT_EQ((*lines)[0].name, "input.mouse_sensitivity");
    EXPECT_EQ((*lines)[0].value, "0.004");
    EXPECT_EQ((*lines)[1].name, "renderer.antialiasing");
    EXPECT_EQ((*lines)[1].value, "off");

    EXPECT_EQ((*lines)[2].name, "view.fov");
    EXPECT_EQ((*lines)[2].value, "75");
}

TEST_F(ApplicationTest, MissingConfigFileUsesDefaults) {
    m_config.configFiles = {std::filesystem::temp_directory_path() / "daidalos_missing_app.cfg"};
    Application app(m_config);

    ASSERT_TRUE(app.Run(m_handler).has_value());

    EXPECT_EQ(m_handler.windowSize, (platform::Extent{320, 200}));
}

TEST_F(ApplicationTest, InvalidSettingFailsStartup) {
    m_config.arguments = {"+window.width", "wide"};
    Application app(m_config);

    const core::Result<void> result = app.Run(m_handler);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, core::ErrorCode::ParseError);
    EXPECT_TRUE(m_handler.calls.empty());
    ExpectEverythingStopped();
}

TEST_F(ApplicationTest, OutOfRangeWindowSizeFailsStartup) {
    m_config.arguments = {"+window.width", "0"};
    Application app(m_config);

    const core::Result<void> result = app.Run(m_handler);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, core::ErrorCode::InvalidArgument);
    EXPECT_EQ(result.error().message, "Starting main window failed: window.width is 0; it must be between 1 and 32768");
}

TEST_F(ApplicationTest, UnknownSettingIsReportedAsWarning) {
    m_config.arguments = {"+window.widht", "640"};
    Application app(m_config);

    ASSERT_TRUE(app.Run(m_handler).has_value());

    ASSERT_EQ(m_sink.warnings.size(), 1u);
    EXPECT_EQ(m_sink.warnings[0], "app: Unknown setting 'window.widht' was ignored");
}

TEST_F(ApplicationTest, HandlerCvarsReceiveValuesGivenBeforeRegistration) {
    struct Handler final : RecordingHandler {
        core::Result<void> OnStart(Application& app) override {
            difficulty = app.CVars().Register<std::int64_t>("game.difficulty", 1, "Difficulty").Get();
            return {};
        }
        std::int64_t difficulty = 0;
    } handler;
    m_config.arguments = {"+game.difficulty", "3"};
    Application app(m_config);

    ASSERT_TRUE(app.Run(handler).has_value());

    EXPECT_EQ(handler.difficulty, 3);
    EXPECT_TRUE(m_sink.warnings.empty());
}

TEST_F(ApplicationTest, FixedUpdatesFollowElapsedTime) {
    // A frame lasts at least the sleep below, so 20 frames span at least 3 fixed steps of 1/60 s.
    m_handler.quitAfterFrames = 20;
    m_handler.onUpdate = [](Application&, const core::FrameTime&) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds{3};
        while (std::chrono::steady_clock::now() < end) {
        }
    };
    Application app(m_config);

    ASSERT_TRUE(app.Run(m_handler).has_value());

    EXPECT_GE(m_handler.fixedUpdates, 3);
}

TEST_F(ApplicationTest, CrashHandlerIsInstalledAndRemoved) {
    m_config.crashDumpDirectory = std::filesystem::temp_directory_path() / "daidalos_app_crash";
    m_handler.quitAfterFrames = 1;
    bool installedDuringRun = false;
    m_handler.onUpdate = [&](Application&, const core::FrameTime&) {
        // A second installation fails while the application's handler is in place.
        installedDuringRun = !platform::InstallCrashHandler({m_config.crashDumpDirectory, "x"}).has_value();
    };
    Application app(m_config);

    ASSERT_TRUE(app.Run(m_handler).has_value());

    EXPECT_TRUE(installedDuringRun);
    const core::Result<void> afterRun = platform::InstallCrashHandler({m_config.crashDumpDirectory, "x"});
    EXPECT_TRUE(afterRun.has_value()) << "The handler must be removed at shutdown";
    platform::UninstallCrashHandler();
    std::filesystem::remove_all(m_config.crashDumpDirectory);
}

TEST(ApplicationRendering, DriverWithoutVulkanStopsTheStartupWithAnError) {
    ApplicationConfig config;
    config.platform.videoDriver = "dummy";
    config.rendering = true;
    Application application(std::move(config));
    RecordingHandler handler;

    const core::Result<void> result = application.Run(handler);

    ASSERT_FALSE(result.has_value());
    EXPECT_FALSE(result.error().message.empty());
    EXPECT_TRUE(handler.calls.empty()); // The handler never starts.
}
