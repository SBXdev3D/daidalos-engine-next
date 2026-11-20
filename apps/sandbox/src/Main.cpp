// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/app/Application.hpp>
#include <daidalos/platform/Platform.hpp>

#include <cstdio>
#include <memory>
#include <string>
#include <utility>

#include "Game.hpp"

using namespace daidalos;

int main(int argc, char** argv) {
    app::ApplicationConfig config;
    config.log.minimumLevel = logging::Level::Debug;
    config.log.filePath = "sandbox.log";
    config.crashDumpDirectory = "crashdumps";
    // The engine's settings, then the game's: the build leaves commented copies of both next to the
    // executable.
    config.configFiles = {"daidalos.cfg", "sandbox.cfg"};
    config.arguments.assign(argv + 1, argv + argc);
    config.window.title = "Daidalos Sandbox";
    config.rendering = true;
    config.audio = true;

    app::Application application(std::move(config));
    const std::unique_ptr<sandbox::Game> game = sandbox::Game::Create();
    core::Result<void> result = application.Run(*game);
    if (result && game->Failure().has_value()) {
        result = tl::unexpected(*game->Failure());
    }
    if (!result) {
        const std::string message = core::Format(result.error());
        std::fprintf(stderr, "%s\n", message.c_str());
        // Started from a file browser, the player has no console: the reason must be on screen.
        platform::ShowErrorMessage("Daidalos Sandbox", message);
        return 1;
    }
    return 0;
}
