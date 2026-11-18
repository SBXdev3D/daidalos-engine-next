// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

// Crash reports read from the minidumps the crash handler writes (ADR-0080): what failed, where, and the
// stack of the thread that failed, with function names and lines where symbols are found.
namespace daidalos::platform {

struct CrashFrame {
    std::uint64_t address = 0;
    /// The module's file name ("sandbox.exe"), and the address's offset in it; empty outside any module.
    std::string module;
    std::uint64_t offset = 0;
    /// From the module's symbols (its PDB), when found.
    std::string function;
    std::string file;
    std::uint32_t line = 0;
};

struct CrashReport {
    std::filesystem::path dump;
    std::uint32_t exceptionCode = 0;
    /// "access violation", "stack overflow", "abort or failed assertion"...
    std::string exception;
    /// For an access violation: whether it read, wrote or executed, and at which address.
    std::string detail;
    std::uint64_t exceptionAddress = 0;
    std::uint32_t threadId = 0;
    /// The failing thread's stack, innermost first.
    std::vector<CrashFrame> stack;
    /// Every module loaded, by file name, with its version when it has one.
    std::vector<std::string> modules;
};

/// Whether ReadCrashDump reads dumps on this system (Windows only, as the crash handler).
[[nodiscard]] bool CrashReportsSupported() noexcept;

/// Reads a minidump. Symbols are looked for beside each module's image as it was when the process ran, and in
/// symbolPaths. Fails with NotFound for a missing file, ParseError for a file that is not a dump or holds no
/// exception, and NotSupported on systems without dump support.
[[nodiscard]] core::Result<CrashReport> ReadCrashDump(const std::filesystem::path& dump,
                                                      std::span<const std::filesystem::path> symbolPaths = {});

/// The report as text, for people: the exception, then one line per frame.
[[nodiscard]] std::string FormatCrashReport(const CrashReport& report);

/// Writes a report beside each dump of a directory that has none yet, as <dump>.txt, with the end of
/// previousLog when it is given and exists (the log of the run that crashed). Returns the reports written.
/// A dump that cannot be read gets a report that says why.
std::vector<std::filesystem::path> ReportNewCrashes(const std::filesystem::path& directory,
                                                    const std::filesystem::path& previousLog = {});

} // namespace daidalos::platform
