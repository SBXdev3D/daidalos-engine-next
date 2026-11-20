// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/app/Application.hpp>
#include <daidalos/core/Error.hpp>

#include <functional>
#include <memory>
#include <optional>

namespace sandbox {

/// How the game runs: on its own, or inside the level editor (ADR-0110).
struct GameOptions {
    /// Played inside another application, the editor: the developer windows and the window's title are the
    /// host's, and quitting ends the game, not the application.
    bool embedded = false;
    /// Called instead of quitting the application, when embedded.
    std::function<void()> quit;
};

/// The demo game (docs/DEMO.md) as an application handler: the sandbox runs it, and the editor plays levels in its
/// own window with it.
class Game : public daidalos::app::IApplicationHandler {
public:
    [[nodiscard]] static std::unique_ptr<Game> Create(GameOptions options = {});
    /// Why the game stopped, if it failed.
    [[nodiscard]] virtual const std::optional<daidalos::core::Error>& Failure() const = 0;
};

} // namespace sandbox
