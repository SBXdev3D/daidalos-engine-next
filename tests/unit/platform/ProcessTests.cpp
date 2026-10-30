// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/platform/Process.hpp>

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace daidalos;

namespace {

// Runs a command line through the system shell, which exists on every test machine.
std::vector<std::string> ShellCommand(const std::string& command) {
#ifdef _WIN32
    return {"cmd.exe", "/c", command};
#else
    return {"/bin/sh", "-c", command};
#endif
}

} // namespace

TEST(RunProcess, CollectsStandardOutput) {
    const core::Result<platform::ProcessResult> result = platform::RunProcess(ShellCommand("echo hello"));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->exitCode, 0);
    EXPECT_NE(result->output.find("hello"), std::string::npos);
}

TEST(RunProcess, CollectsStandardError) {
    const core::Result<platform::ProcessResult> result = platform::RunProcess(ShellCommand("echo oops 1>&2"));
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result->output.find("oops"), std::string::npos);
}

TEST(RunProcess, ReportsTheExitCodeOfAFailingProgram) {
    const core::Result<platform::ProcessResult> result = platform::RunProcess(ShellCommand("exit 3"));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->exitCode, 3);
}

TEST(RunProcess, FailsWhenTheProgramDoesNotExist) {
    const std::vector<std::string> arguments{"daidalos-this-program-does-not-exist"};
    const core::Result<platform::ProcessResult> result = platform::RunProcess(arguments);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, core::ErrorCode::IoError);
}
