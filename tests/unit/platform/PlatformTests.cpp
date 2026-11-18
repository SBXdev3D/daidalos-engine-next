// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/platform/Platform.hpp>

#include <SDL3/SDL_events.h>
#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <utility>
#include <vector>

using namespace daidalos;
using namespace daidalos::platform;

namespace {

// Tests never open a real window (CONVENTIONS.md, section 9).
PlatformConfig Headless() {
    PlatformConfig config;
    config.videoDriver = "dummy";
    return config;
}

} // namespace

TEST(Platform, ExecutableDirectoryHoldsTheRunningTests) {
    const std::filesystem::path directory = ExecutableDirectory();
    ASSERT_FALSE(directory.empty());
    EXPECT_TRUE(directory.is_absolute());
    EXPECT_TRUE(std::filesystem::is_directory(directory));
}

TEST(Platform, CreatesWithRequestedVideoDriver) {
    core::Result<Platform> platform = Platform::Create(Headless());

    ASSERT_TRUE(platform.has_value()) << core::Format(platform.error());
    EXPECT_EQ(platform->VideoDriver(), "dummy");
}

TEST(Platform, SecondPlatformFailsWhileFirstExists) {
    core::Result<Platform> first = Platform::Create(Headless());
    ASSERT_TRUE(first.has_value()) << core::Format(first.error());

    const core::Result<Platform> second = Platform::Create(Headless());

    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error().code, core::ErrorCode::InvalidState);
}

TEST(Platform, CanBeCreatedAgainAfterDestruction) {
    for (int i = 0; i < 3; ++i) {
        const core::Result<Platform> platform = Platform::Create(Headless());
        ASSERT_TRUE(platform.has_value()) << "Iteration " << i << ": " << core::Format(platform.error());
    }
}

TEST(Platform, MovedPlatformStaysUsable) {
    core::Result<Platform> created = Platform::Create(Headless());
    ASSERT_TRUE(created.has_value()) << core::Format(created.error());

    Platform platform = std::move(*created);

    EXPECT_EQ(platform.VideoDriver(), "dummy");
    EXPECT_FALSE(Platform::Create(Headless()).has_value()) << "The moved platform still owns SDL";
}

TEST(Platform, UnknownVideoDriverFailsWithDeviceError) {
    PlatformConfig config;
    config.videoDriver = "no-such-driver";

    const core::Result<Platform> platform = Platform::Create(config);

    ASSERT_FALSE(platform.has_value());
    EXPECT_EQ(platform.error().code, core::ErrorCode::DeviceError);
    EXPECT_TRUE(Platform::Create(Headless()).has_value()) << "A failed Create must not block a retry";
}

TEST(Platform, PollEventsTranslatesQueuedEvents) {
    core::Result<Platform> platform = Platform::Create(Headless());
    ASSERT_TRUE(platform.has_value()) << core::Format(platform.error());
    std::vector<Event> drain;
    platform->PollEvents(drain);

    SDL_Event quit{};
    quit.type = SDL_EVENT_QUIT;
    ASSERT_TRUE(SDL_PushEvent(&quit));
    std::vector<Event> events;
    platform->PollEvents(events);

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0], Event(QuitRequested{}));
}

TEST(Platform, PollEventsAppendsToExistingEvents) {
    core::Result<Platform> platform = Platform::Create(Headless());
    ASSERT_TRUE(platform.has_value()) << core::Format(platform.error());
    std::vector<Event> events = {WindowFocusLost{}};

    SDL_Event quit{};
    quit.type = SDL_EVENT_QUIT;
    ASSERT_TRUE(SDL_PushEvent(&quit));
    platform->PollEvents(events);

    ASSERT_GE(events.size(), 2u);
    EXPECT_EQ(events.front(), Event(WindowFocusLost{}));
    EXPECT_EQ(events.back(), Event(QuitRequested{}));
}

TEST(Platform, WaitEventsReturnsAfterTimeoutWithoutEvents) {
    core::Result<Platform> platform = Platform::Create(Headless());
    ASSERT_TRUE(platform.has_value()) << core::Format(platform.error());
    std::vector<Event> drain;
    platform->PollEvents(drain);

    std::vector<Event> events;
    platform->WaitEvents(std::chrono::milliseconds{1}, events);

    EXPECT_TRUE(events.empty());
}

TEST(Platform, WaitEventsReturnsQueuedEvent) {
    core::Result<Platform> platform = Platform::Create(Headless());
    ASSERT_TRUE(platform.has_value()) << core::Format(platform.error());

    SDL_Event quit{};
    quit.type = SDL_EVENT_QUIT;
    ASSERT_TRUE(SDL_PushEvent(&quit));
    std::vector<Event> events;
    platform->WaitEvents(std::chrono::seconds{10}, events);

    ASSERT_FALSE(events.empty());
    EXPECT_EQ(events.back(), Event(QuitRequested{}));
}

TEST(Platform, GamepadThatCannotBeOpenedIsNotReported) {
    core::Result<Platform> platform = Platform::Create(Headless());
    ASSERT_TRUE(platform.has_value()) << core::Format(platform.error());
    std::vector<Event> drain;
    platform->PollEvents(drain);

    // No gamepad has this id, so opening it fails and neither its connection nor its removal is reported.
    SDL_Event event{};
    event.type = SDL_EVENT_GAMEPAD_ADDED;
    event.gdevice.which = 999999;
    ASSERT_TRUE(SDL_PushEvent(&event));
    event.type = SDL_EVENT_GAMEPAD_REMOVED;
    ASSERT_TRUE(SDL_PushEvent(&event));
    std::vector<Event> events;
    platform->PollEvents(events);

    EXPECT_TRUE(events.empty());
}

TEST(Platform, InputEventsArePassedThrough) {
    core::Result<Platform> platform = Platform::Create(Headless());
    ASSERT_TRUE(platform.has_value()) << core::Format(platform.error());
    std::vector<Event> drain;
    platform->PollEvents(drain);

    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_A;
    event.key.down = true;
    ASSERT_TRUE(SDL_PushEvent(&event));
    std::vector<Event> events;
    platform->PollEvents(events);

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0], Event(KeyPressed{WindowId{}, Key::A, false}));
}

TEST(Platform, ExecutableFileNameFollowsTheSystem) {
#ifdef _WIN32
    EXPECT_EQ(daidalos::platform::ExecutableFileName("sandbox"), "sandbox.exe");
#else
    EXPECT_EQ(daidalos::platform::ExecutableFileName("sandbox"), "sandbox");
#endif
}
