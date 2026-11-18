// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// crash-report <dump or directory>... [--symbols <directory>]...
// Prints the report of each minidump: the exception, where, and the failing thread's stack (ADR-0080).
// A directory stands for the dumps in it.

#include <daidalos/platform/CrashReport.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

using namespace daidalos;

int main(int argc, char** argv) {
    std::vector<std::filesystem::path> dumps;
    std::vector<std::filesystem::path> symbols;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--symbols" && index + 1 < argc) {
            symbols.emplace_back(argv[++index]);
            continue;
        }
        const std::filesystem::path path(argument);
        std::error_code error;
        if (std::filesystem::is_directory(path, error)) {
            for (const auto& entry : std::filesystem::directory_iterator(path, error)) {
                if (entry.path().extension() == ".dmp") {
                    dumps.push_back(entry.path());
                }
            }
        } else {
            dumps.push_back(path);
        }
    }
    if (dumps.empty()) {
        std::fputs("Usage: crash-report <dump or directory>... [--symbols <directory>]...\n", stderr);
        return 2;
    }
    if (!platform::CrashReportsSupported()) {
        std::fputs("Crash dumps are read on Windows only\n", stderr);
        return 2;
    }
    std::ranges::sort(dumps);
    int failed = 0;
    for (const std::filesystem::path& dump : dumps) {
        const core::Result<platform::CrashReport> report = platform::ReadCrashDump(dump, symbols);
        if (!report) {
            std::fprintf(stderr, "%s\n", core::Format(report.error()).c_str());
            ++failed;
            continue;
        }
        std::fputs(platform::FormatCrashReport(*report).c_str(), stdout);
        std::fputs("\n", stdout);
    }
    return failed == 0 ? 0 : 1;
}
