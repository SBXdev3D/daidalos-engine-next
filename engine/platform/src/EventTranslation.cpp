// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include "EventTranslation.hpp"

#include <algorithm>
#include <cstdint>

namespace daidalos::platform::detail {

namespace {

std::uint32_t NonNegative(Sint32 value) {
    return static_cast<std::uint32_t>(std::max<Sint32>(value, 0));
}

} // namespace

std::optional<Event> TranslateEvent(const SDL_Event& event) {
    const auto window = static_cast<WindowId>(event.window.windowID);

    switch (event.type) {
    case SDL_EVENT_QUIT:
        return QuitRequested{};
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        return WindowCloseRequested{window};
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        return WindowResized{window, Extent{NonNegative(event.window.data1), NonNegative(event.window.data2)}};
    case SDL_EVENT_WINDOW_MINIMIZED:
        return WindowMinimized{window};
    case SDL_EVENT_WINDOW_RESTORED:
        return WindowRestored{window};
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        return WindowFocusGained{window};
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        return WindowFocusLost{window};
    default:
        return std::nullopt;
    }
}

} // namespace daidalos::platform::detail
