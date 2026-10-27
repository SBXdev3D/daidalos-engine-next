// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <cstdint>
#include <variant>

namespace daidalos::platform {

/// Identifies a window in events. Never 0 for a live window.
enum class WindowId : std::uint32_t {};

/// Size of a window or surface.
struct Extent {
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    bool operator==(const Extent&) const = default;
};

/// The user or the system asked the application to quit (last window closed, Ctrl+C, logout).
struct QuitRequested {
    bool operator==(const QuitRequested&) const = default;
};

/// The user clicked the close button of a window. The window stays open until it is destroyed.
struct WindowCloseRequested {
    WindowId window{};
    bool operator==(const WindowCloseRequested&) const = default;
};

/// The drawable size of a window changed. The size is in pixels, which is what a swapchain needs;
/// it differs from the window size on high-density displays.
struct WindowResized {
    WindowId window{};
    Extent pixelSize;
    bool operator==(const WindowResized&) const = default;
};

struct WindowMinimized {
    WindowId window{};
    bool operator==(const WindowMinimized&) const = default;
};

/// The window left the minimized or maximized state.
struct WindowRestored {
    WindowId window{};
    bool operator==(const WindowRestored&) const = default;
};

struct WindowFocusGained {
    WindowId window{};
    bool operator==(const WindowFocusGained&) const = default;
};

struct WindowFocusLost {
    WindowId window{};
    bool operator==(const WindowFocusLost&) const = default;
};

/// Platform event, independent of SDL. Keyboard, mouse and gamepad events are added with the input
/// module (Phase 2).
using Event = std::variant<QuitRequested, WindowCloseRequested, WindowResized, WindowMinimized, WindowRestored,
                           WindowFocusGained, WindowFocusLost>;

} // namespace daidalos::platform
