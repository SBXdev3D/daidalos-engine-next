// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/app/Application.hpp>
#include <daidalos/logging/Log.hpp>

#include <cstdio>

#include "Editor.hpp"

using namespace daidalos;

int main(int argc, char** argv) {
    app::ApplicationConfig config;
    config.log.minimumLevel = logging::Level::Debug;
    config.log.filePath = "editor.log";
    config.crashDumpDirectory = "crashdumps";
    config.configFiles = {"daidalos.cfg", "editor.cfg"};
    // The editor's settings are not the player's: none are written.
    config.userConfigFile.clear();
    config.arguments.assign(argv + 1, argv + argc);
    config.window.title = "Daidalos Editor";
    config.rendering = true;
    // The game played in the editor's window is heard (ADR-0110); the editor's own scenes stay silent.
    config.audio = true;

    app::Application application(config);
    editor::Editor editor;
    core::Result<void> result = application.Run(editor);
    if (!result) {
        std::fprintf(stderr, "%s\n", core::Format(result.error()).c_str());
        return 1;
    }
    return 0;
}
