// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>

#include <filesystem>
#include <string>

namespace daidalos::platform {

struct CrashHandlerConfig {
    /// Directory where dumps are written. It is created if it does not exist.
    std::filesystem::path dumpDirectory;
    /// Dump files are named <filePrefix>-<YYYYMMDD>-<HHMMSS>-<process id>.dmp.
    std::string filePrefix = "crash";
};

/// Whether InstallCrashHandler writes dumps on this system (Windows only for now).
[[nodiscard]] bool CrashDumpsSupported() noexcept;

/// Installs a process-wide handler that writes a minidump when the process crashes: unhandled
/// exceptions, abort() (called by failed DAI_ASSERTs), invalid C runtime parameters and pure virtual calls.
/// On systems without dump support it does nothing and succeeds: crashes produce core dumps configured
/// by the operating system.
/// Fails with InvalidState if a handler is already installed, or IoError if the directory cannot be created.
core::Result<void> InstallCrashHandler(const CrashHandlerConfig& config);

/// Restores the handlers that were in place before InstallCrashHandler. Does nothing if none is installed.
void UninstallCrashHandler() noexcept;

} // namespace daidalos::platform
