// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/platform/Event.hpp>
#include <daidalos/platform/InputCodes.hpp>

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace daidalos::platform {

namespace detail {
struct PlatformState;
} // namespace detail

/// Directory of the running executable, or empty if the system cannot tell.
[[nodiscard]] std::filesystem::path ExecutableDirectory();

/// The file name of a program of this name on this system: "sandbox.exe" on Windows, "sandbox" elsewhere.
[[nodiscard]] std::string ExecutableFileName(std::string_view program);

/// What the keyboard's layout prints on a key, in UTF-8: "Z" for Key::W on AZERTY. Empty when the system
/// cannot tell, as before a Platform exists.
[[nodiscard]] std::string KeyLabel(Key key);

/// Shows a modal error box, for failures a player must see, such as a GPU the engine cannot use.
/// Works before and without a Platform. Blocks until the box is closed; logs a warning if none can be shown.
void ShowErrorMessage(const std::string& title, const std::string& message);

struct PlatformConfig {
    /// Video driver to use. Empty selects the best one for the system. "dummy" opens windows without
    /// a display, which is what tests and CI use.
    std::string videoDriver;
    /// Gamepads through DirectInput, for old pads that speak nothing else. Off by default: on some machines a
    /// device makes DirectInput's enumeration take tens of seconds, and SDL's other drivers (XInput, raw input,
    /// HID) see every recent pad.
    bool directInputGamepads = false;
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
    /// Shakes every open gamepad that can (ADR-0076): low and high are the strengths of its heavy and light
    /// motors, from 0 to 1, for the duration or until the next call. Pads without motors ignore it.
    void RumbleGamepads(float low, float high, std::chrono::milliseconds duration);

private:
    friend class Window;

    explicit Platform(std::unique_ptr<detail::PlatformState> state);

    std::unique_ptr<detail::PlatformState> m_state;
};

} // namespace daidalos::platform
