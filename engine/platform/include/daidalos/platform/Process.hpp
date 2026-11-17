// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>

#include <filesystem>
#include <span>
#include <string>

struct SDL_Process;

namespace daidalos::platform {

struct ProcessResult {
    int exitCode = 0;
    /// Standard output and standard error, interleaved.
    std::string output;
};

/// Runs a program to completion and collects its output. arguments[0] is the program. A program
/// that runs and fails is not an error here: check exitCode. Fails with IoError if it cannot start.
/// Blocks the calling thread; meant for development tools, not for the frame loop of a shipped game.
[[nodiscard]] core::Result<ProcessResult> RunProcess(std::span<const std::string> arguments);

/// A program started with StartProcess, running beside this one. Letting go of it leaves the program running.
class ChildProcess {
public:
    ChildProcess() = default;
    explicit ChildProcess(SDL_Process* process) noexcept : m_process(process) {}
    ChildProcess(ChildProcess&& other) noexcept;
    ChildProcess& operator=(ChildProcess&& other) noexcept;
    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;
    ~ChildProcess();

    /// True until the program exits. Never blocks.
    [[nodiscard]] bool Running();
    /// The exit code once the program has exited, else 0.
    [[nodiscard]] int ExitCode() const { return m_exitCode; }
    /// Ends the program at once.
    void Kill();

private:
    SDL_Process* m_process = nullptr;
    int m_exitCode = 0;
};

/// Starts a program and returns at once; arguments[0] is the program. Its standard streams go nowhere. An empty
/// working directory keeps this program's. Fails with IoError if it cannot start.
[[nodiscard]] core::Result<ChildProcess> StartProcess(std::span<const std::string> arguments,
                                                      const std::filesystem::path& workingDirectory = {});

} // namespace daidalos::platform
