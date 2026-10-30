// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/core/Assert.hpp>
#include <daidalos/platform/Process.hpp>

#include <SDL3/SDL.h>

#include <format>
#include <vector>

namespace daidalos::platform {

namespace {

constexpr std::string_view kModule = "platform";

} // namespace

core::Result<ProcessResult> RunProcess(std::span<const std::string> arguments) {
    DAI_ASSERT(!arguments.empty(), "RunProcess needs at least the program to run");

    std::vector<const char*> argv;
    argv.reserve(arguments.size() + 1);
    for (const std::string& argument : arguments) {
        argv.push_back(argument.c_str());
    }
    argv.push_back(nullptr);

    const SDL_PropertiesID properties = SDL_CreateProperties();
    SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv.data());
    SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
    SDL_Process* process = SDL_CreateProcessWithProperties(properties);
    SDL_DestroyProperties(properties);
    if (process == nullptr) {
        return core::MakeError(core::ErrorCode::IoError, std::string(kModule),
                               std::format("Cannot run '{}': {}", arguments[0], SDL_GetError()));
    }

    std::size_t size = 0;
    int exitCode = 0;
    void* output = SDL_ReadProcess(process, &size, &exitCode);
    SDL_DestroyProcess(process);
    if (output == nullptr) {
        return core::MakeError(core::ErrorCode::IoError, std::string(kModule),
                               std::format("Cannot read the output of '{}': {}", arguments[0], SDL_GetError()));
    }

    ProcessResult result{exitCode, std::string(static_cast<const char*>(output), size)};
    SDL_free(output);
    return result;
}

} // namespace daidalos::platform
