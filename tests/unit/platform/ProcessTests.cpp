// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/platform/Process.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <thread>
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

TEST(StartProcess, ReturnsAtOnceAndSeesTheProgramEnd) {
#ifdef _WIN32
    const std::vector<std::string> arguments{"cmd.exe", "/c", "ping -n 2 127.0.0.1 > nul & exit 4"};
#else
    const std::vector<std::string> arguments{"/bin/sh", "-c", "sleep 1; exit 4"};
#endif
    core::Result<platform::ChildProcess> child = platform::StartProcess(arguments);
    ASSERT_TRUE(child.has_value());
    EXPECT_TRUE(child->Running()) << "The program waits about a second";
    while (child->Running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_EQ(child->ExitCode(), 4);
}

TEST(StartProcess, KillEndsTheProgram) {
#ifdef _WIN32
    const std::vector<std::string> arguments{"cmd.exe", "/c", "ping -n 30 127.0.0.1 > nul"};
#else
    const std::vector<std::string> arguments{"/bin/sh", "-c", "sleep 30"};
#endif
    core::Result<platform::ChildProcess> child = platform::StartProcess(arguments);
    ASSERT_TRUE(child.has_value());
    child->Kill();
    EXPECT_FALSE(child->Running());
}

TEST(StartProcess, FailsWhenTheProgramDoesNotExist) {
    const std::vector<std::string> arguments{"daidalos-this-program-does-not-exist"};
    EXPECT_FALSE(platform::StartProcess(arguments).has_value());
}
