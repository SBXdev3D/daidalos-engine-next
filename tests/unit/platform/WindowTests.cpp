// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/platform/Platform.hpp>
#include <daidalos/platform/Window.hpp>

#include <SDL3/SDL_events.h>
#include <gtest/gtest.h>

#include <optional>
#include <utility>
#include <vector>

#if defined(_WIN32) && defined(_DEBUG) && !defined(__SANITIZE_ADDRESS__)
#include <crtdbg.h>
#define DAI_HAS_CRT_LEAK_CHECK 1
#else
#define DAI_HAS_CRT_LEAK_CHECK 0
#endif

using namespace daidalos;
using namespace daidalos::platform;

namespace {

class WindowTest : public testing::Test {
protected:
    void SetUp() override {
        PlatformConfig config;
        config.videoDriver = "dummy";
        core::Result<Platform> platform = Platform::Create(config);
        ASSERT_TRUE(platform.has_value()) << core::Format(platform.error());
        m_platform.emplace(std::move(*platform));
    }

    Window CreateWindow(const WindowConfig& config = {}) {
        core::Result<Window> window = Window::Create(*m_platform, config);
        EXPECT_TRUE(window.has_value()) << core::Format(window.error());
        return std::move(*window);
    }

    std::optional<Platform> m_platform;
};

} // namespace

TEST_F(WindowTest, HasRequestedSizeAndTitle) {
    WindowConfig config;
    config.title = "Test window";
    config.width = 640;
    config.height = 480;

    const Window window = CreateWindow(config);

    EXPECT_EQ(window.Size(), (Extent{640, 480}));
    EXPECT_EQ(window.PixelSize(), (Extent{640, 480})) << "The dummy driver has no pixel scaling";
    EXPECT_EQ(window.Title(), "Test window");
    EXPECT_NE(window.Id(), WindowId{});
    EXPECT_FALSE(window.IsMinimized());
}

TEST_F(WindowTest, SetTitleChangesTitle) {
    Window window = CreateWindow();

    window.SetTitle("Renamed");

    EXPECT_EQ(window.Title(), "Renamed");
}

TEST_F(WindowTest, EachWindowHasItsOwnId) {
    const Window first = CreateWindow();
    const Window second = CreateWindow();

    EXPECT_NE(first.Id(), second.Id());
}

TEST_F(WindowTest, ZeroSizeFailsWithInvalidArgument) {
    WindowConfig config;
    config.width = 0;

    const core::Result<Window> window = Window::Create(*m_platform, config);

    ASSERT_FALSE(window.has_value());
    EXPECT_EQ(window.error().code, core::ErrorCode::InvalidArgument);
}

TEST_F(WindowTest, OversizedWindowFailsWithInvalidArgument) {
    WindowConfig config;
    config.height = 0x80000000u;

    const core::Result<Window> window = Window::Create(*m_platform, config);

    ASSERT_FALSE(window.has_value());
    EXPECT_EQ(window.error().code, core::ErrorCode::InvalidArgument);
}

TEST_F(WindowTest, MovedWindowKeepsItsId) {
    Window original = CreateWindow();
    const WindowId id = original.Id();

    const Window moved = std::move(original);

    EXPECT_EQ(moved.Id(), id);
}

TEST_F(WindowTest, MoveAssignmentDestroysThePreviousWindow) {
    Window target = CreateWindow();
    Window source = CreateWindow();
    const WindowId sourceId = source.Id();

    target = std::move(source);

    EXPECT_EQ(target.Id(), sourceId);
    // m_platform is destroyed after this test; its destructor asserts that no window is left.
}

TEST_F(WindowTest, CloseRequestIsReportedWithTheWindowId) {
    const Window window = CreateWindow();
    std::vector<Event> drain;
    m_platform->PollEvents(drain);

    SDL_Event close{};
    close.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
    close.window.windowID = static_cast<SDL_WindowID>(window.Id());
    ASSERT_TRUE(SDL_PushEvent(&close));
    std::vector<Event> events;
    m_platform->PollEvents(events);

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0], Event(WindowCloseRequested{window.Id()}));
}

TEST_F(WindowTest, RepeatedCreateAndDestroyDoesNotLeak) {
    // Window creation queues events; they are drained so that the queue does not count as growth.
    std::vector<Event> events;
    events.reserve(1024);
    // One window first, so that one-time allocations made by SDL are not counted.
    {
        const Window warmUp = CreateWindow();
    }
    m_platform->PollEvents(events);
    events.clear();

#if DAI_HAS_CRT_LEAK_CHECK
    _CrtMemState before{};
    _CrtMemCheckpoint(&before);
#endif

    for (int i = 0; i < 100; ++i) {
        const core::Result<Window> window = Window::Create(*m_platform, WindowConfig{});
        ASSERT_TRUE(window.has_value()) << "Iteration " << i << ": " << core::Format(window.error());
        m_platform->PollEvents(events);
        events.clear();
    }

#if DAI_HAS_CRT_LEAK_CHECK
    _CrtMemState after{};
    _CrtMemState difference{};
    _CrtMemCheckpoint(&after);
    if (_CrtMemDifference(&difference, &before, &after)) {
        _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
        _CrtMemDumpStatistics(&difference);
        ADD_FAILURE() << "Heap memory grew while creating and destroying windows";
    }
#else
    // On Linux, the ASan preset runs LeakSanitizer on the whole process instead.
#endif
}
