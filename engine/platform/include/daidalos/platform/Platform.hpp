// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/platform/Event.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace daidalos::platform {

namespace detail {
struct PlatformState;
} // namespace detail

struct PlatformConfig {
    /// Video driver to use. Empty selects the best one for the system. "dummy" opens windows without
    /// a display, which is what tests and CI use.
    std::string videoDriver;
};

/// Owns the platform layer (SDL3, hidden from users, ADR-0004).
/// Only one Platform can exist at a time, and every Window must be destroyed before it.
class Platform {
public:
    /// Fails with InvalidState if a Platform already exists, or DeviceError if SDL cannot start.
    static core::Result<Platform> Create(const PlatformConfig& config = {});

    Platform(Platform&& other) noexcept;
    Platform& operator=(Platform&& other) noexcept;
    Platform(const Platform&) = delete;
    Platform& operator=(const Platform&) = delete;
    ~Platform();

    /// Appends the pending events to events, without blocking.
    void PollEvents(std::vector<Event>& events);

    /// Waits up to timeout for an event, then appends all pending events. Used while the application
    /// has nothing to draw (minimized), so that it does not spin.
    void WaitEvents(std::chrono::milliseconds timeout, std::vector<Event>& events);

    /// Name of the video driver in use, for example "windows", "x11", "wayland" or "dummy".
    [[nodiscard]] std::string_view VideoDriver() const;
    /// False when the gamepad subsystem could not start. Keyboard and mouse work either way.
    [[nodiscard]] bool GamepadsAvailable() const;

private:
    friend class Window;

    explicit Platform(std::unique_ptr<detail::PlatformState> state);

    std::unique_ptr<detail::PlatformState> m_state;
};

} // namespace daidalos::platform
