// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <string>
#include <vector>

struct SDL_Gamepad;

namespace daidalos::platform::detail {

/// State behind a Platform. Windows keep a pointer to it, which stays valid when the Platform is moved.
struct PlatformState {
    PlatformState() = default;
    PlatformState(const PlatformState&) = delete;
    PlatformState& operator=(const PlatformState&) = delete;
    ~PlatformState();

    std::string videoDriver;
    int liveWindows = 0;
    /// False when SDL's gamepad subsystem could not start; the platform then works without gamepads.
    bool gamepadsAvailable = false;
    /// Gamepads opened on connection, closed on disconnection or shutdown.
    std::vector<SDL_Gamepad*> gamepads;
};

} // namespace daidalos::platform::detail
