// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/core/Assert.hpp>
#include <daidalos/platform/Process.hpp>

#include <SDL3/SDL.h>

#include <format>
#include <utility>
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
        return core::MakeError(core::ErrorCode::IoError, kModule,
                               std::format("Cannot run '{}': {}", arguments[0], SDL_GetError()));
    }

    std::size_t size = 0;
    int exitCode = 0;
    void* output = SDL_ReadProcess(process, &size, &exitCode);
    SDL_DestroyProcess(process);
    if (output == nullptr) {
        return core::MakeError(core::ErrorCode::IoError, kModule,
                               std::format("Cannot read the output of '{}': {}", arguments[0], SDL_GetError()));
    }

    ProcessResult result{exitCode, std::string(static_cast<const char*>(output), size)};
    SDL_free(output);
    return result;
}

ChildProcess::ChildProcess(ChildProcess&& other) noexcept
    : m_process(std::exchange(other.m_process, nullptr)), m_exitCode(other.m_exitCode) {}

ChildProcess& ChildProcess::operator=(ChildProcess&& other) noexcept {
    if (this != &other) {
        if (m_process != nullptr) {
            SDL_DestroyProcess(m_process);
        }
        m_process = std::exchange(other.m_process, nullptr);
        m_exitCode = other.m_exitCode;
    }
    return *this;
}

ChildProcess::~ChildProcess() {
    if (m_process != nullptr) {
        // Closes the handle; the program runs on.
        SDL_DestroyProcess(m_process);
    }
}

bool ChildProcess::Running() {
    if (m_process == nullptr) {
        return false;
    }
    int exitCode = 0;
    if (SDL_WaitProcess(m_process, false, &exitCode)) {
        m_exitCode = exitCode;
        SDL_DestroyProcess(m_process);
        m_process = nullptr;
        return false;
    }
    return true;
}

void ChildProcess::Kill() {
    if (m_process != nullptr) {
        SDL_KillProcess(m_process, true);
        int exitCode = 0;
        SDL_WaitProcess(m_process, true, &exitCode);
        m_exitCode = exitCode;
        SDL_DestroyProcess(m_process);
        m_process = nullptr;
    }
}

core::Result<ChildProcess> StartProcess(std::span<const std::string> arguments,
                                        const std::filesystem::path& workingDirectory) {
    DAI_ASSERT(!arguments.empty(), "StartProcess needs at least the program to run");

    std::vector<const char*> argv;
    argv.reserve(arguments.size() + 1);
    for (const std::string& argument : arguments) {
        argv.push_back(argument.c_str());
    }
    argv.push_back(nullptr);

    const std::string directory = workingDirectory.string();
    const SDL_PropertiesID properties = SDL_CreateProperties();
    SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv.data());
    SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
    if (!directory.empty()) {
        SDL_SetStringProperty(properties, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING, directory.c_str());
    }
    SDL_Process* process = SDL_CreateProcessWithProperties(properties);
    SDL_DestroyProperties(properties);
    if (process == nullptr) {
        return core::MakeError(core::ErrorCode::IoError, kModule,
                               std::format("Cannot start '{}': {}", arguments[0], SDL_GetError()));
    }
    return ChildProcess(process);
}

} // namespace daidalos::platform
