// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <gtest/gtest.h>

#include <optional>

#include "EventTranslation.hpp"

using namespace daidalos::platform;
using detail::TranslateEvent;

namespace {

constexpr auto kWindow = static_cast<WindowId>(7);

SDL_Event WindowEvent(SDL_EventType type, Sint32 data1 = 0, Sint32 data2 = 0) {
    SDL_Event event{};
    event.type = type;
    event.window.windowID = 7;
    event.window.data1 = data1;
    event.window.data2 = data2;
    return event;
}

} // namespace

TEST(EventTranslation, QuitBecomesQuitRequested) {
    SDL_Event event{};
    event.type = SDL_EVENT_QUIT;

    EXPECT_EQ(TranslateEvent(event), std::optional<Event>(QuitRequested{}));
}

TEST(EventTranslation, WindowEventsKeepTheirWindowId) {
    EXPECT_EQ(TranslateEvent(WindowEvent(SDL_EVENT_WINDOW_CLOSE_REQUESTED)),
              std::optional<Event>(WindowCloseRequested{kWindow}));
    EXPECT_EQ(TranslateEvent(WindowEvent(SDL_EVENT_WINDOW_MINIMIZED)), std::optional<Event>(WindowMinimized{kWindow}));
    EXPECT_EQ(TranslateEvent(WindowEvent(SDL_EVENT_WINDOW_RESTORED)), std::optional<Event>(WindowRestored{kWindow}));
    EXPECT_EQ(TranslateEvent(WindowEvent(SDL_EVENT_WINDOW_FOCUS_GAINED)),
              std::optional<Event>(WindowFocusGained{kWindow}));
    EXPECT_EQ(TranslateEvent(WindowEvent(SDL_EVENT_WINDOW_FOCUS_LOST)), std::optional<Event>(WindowFocusLost{kWindow}));
}

TEST(EventTranslation, PixelSizeChangeBecomesResize) {
    EXPECT_EQ(TranslateEvent(WindowEvent(SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED, 1920, 1080)),
              std::optional<Event>(WindowResized{kWindow, Extent{1920, 1080}}));
}

TEST(EventTranslation, NegativeSizeIsClampedToZero) {
    EXPECT_EQ(TranslateEvent(WindowEvent(SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED, -5, 10)),
              std::optional<Event>(WindowResized{kWindow, Extent{0, 10}}));
}

TEST(EventTranslation, LogicalResizeIsIgnoredInFavourOfPixelResize) {
    // SDL sends both; only the pixel size matters for rendering.
    EXPECT_EQ(TranslateEvent(WindowEvent(SDL_EVENT_WINDOW_RESIZED, 800, 600)), std::nullopt);
}

TEST(EventTranslation, UnusedEventsAreDropped) {
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;

    EXPECT_EQ(TranslateEvent(event), std::nullopt);
}
