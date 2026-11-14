// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// Crashes on purpose after installing the crash handler. Run by CrashHandlerTests.
// Usage: crash_helper <dump directory> <access-violation|abort|assert>

#include <daidalos/core/Assert.hpp>
#include <daidalos/platform/CrashHandler.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <string_view>

using namespace daidalos;

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fputs("Usage: crash_helper <dump directory> <access-violation|abort|assert>\n", stderr);
        return 2;
    }
    // If the handler fails, the process must still end without a Windows Error Reporting dialog.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);

    platform::CrashHandlerConfig config;
    config.dumpDirectory = argv[1];
    config.filePrefix = "helper";
    if (const core::Result<void> result = platform::InstallCrashHandler(config); !result) {
        std::fprintf(stderr, "%s\n", core::Format(result.error()).c_str());
        return 2;
    }

    const std::string_view mode = argv[2];
    if (mode == "access-violation") {
        volatile int* pointer = nullptr;
        *pointer = 1;
    } else if (mode == "abort") {
        std::abort();
    } else if (mode == "assert") {
        volatile bool condition = false;
        DAI_ASSERT(condition, "Crash helper assertion");
        std::abort(); // Reached only when assertions are disabled.
    }
    return 2;
}
