// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <daidalos/core/Result.hpp>

#include <span>
#include <string>

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

} // namespace daidalos::platform
