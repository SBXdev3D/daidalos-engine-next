// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/platform/CrashHandler.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

using namespace daidalos;

namespace {

std::filesystem::path TestDirectory(const std::string& name) {
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / ("daidalos_" + name);
    std::filesystem::remove_all(directory);
    return directory;
}

std::vector<std::filesystem::path> DumpFiles(const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> files;
    if (std::filesystem::exists(directory)) {
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (entry.path().extension() == ".dmp") {
                files.push_back(entry.path());
            }
        }
    }
    return files;
}

class CrashHandlerTest : public testing::Test {
protected:
    void TearDown() override { platform::UninstallCrashHandler(); }
};

} // namespace

TEST_F(CrashHandlerTest, InstallsAndCreatesTheDumpDirectory) {
    const std::filesystem::path directory = TestDirectory("crash_install") / "nested";

    const core::Result<void> result = platform::InstallCrashHandler({directory, "test"});

    ASSERT_TRUE(result.has_value()) << core::Format(result.error());
    if (platform::CrashDumpsSupported()) {
        EXPECT_TRUE(std::filesystem::is_directory(directory));
    }
    std::filesystem::remove_all(directory.parent_path());
}

TEST_F(CrashHandlerTest, SecondInstallFailsWithInvalidState) {
    const std::filesystem::path directory = TestDirectory("crash_twice");
    ASSERT_TRUE(platform::InstallCrashHandler({directory, "test"}).has_value());

    const core::Result<void> second = platform::InstallCrashHandler({directory, "test"});

    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error().code, core::ErrorCode::InvalidState);
    std::filesystem::remove_all(directory);
}

TEST_F(CrashHandlerTest, CanInstallAgainAfterUninstall) {
    const std::filesystem::path directory = TestDirectory("crash_again");
    ASSERT_TRUE(platform::InstallCrashHandler({directory, "test"}).has_value());
    platform::UninstallCrashHandler();

    EXPECT_TRUE(platform::InstallCrashHandler({directory, "test"}).has_value());
    std::filesystem::remove_all(directory);
}

TEST_F(CrashHandlerTest, UninstallWithoutInstallIsSafe) {
    platform::UninstallCrashHandler();
    platform::UninstallCrashHandler();
}

#if defined(DAI_CRASH_HELPER_PATH)

namespace {

// Runs the crash helper and returns its exit code.
DWORD RunCrashHelper(const std::filesystem::path& directory, const std::wstring& mode) {
    std::wstring commandLine =
        L"\"" + std::filesystem::path(DAI_CRASH_HELPER_PATH).wstring() + L"\" \"" + directory.wstring() + L"\" " + mode;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup,
                        &process)) {
        ADD_FAILURE() << "Cannot start the crash helper, error " << GetLastError();
        return 0;
    }
    WaitForSingleObject(process.hProcess, 60'000);
    DWORD exitCode = 0;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return exitCode;
}

class CrashDumpTest : public testing::TestWithParam<const wchar_t*> {};

} // namespace

TEST_P(CrashDumpTest, CrashWritesOneDump) {
    const std::filesystem::path directory = TestDirectory("crash_dump");

    const DWORD exitCode = RunCrashHelper(directory, GetParam());

    EXPECT_NE(exitCode, 0u);
    EXPECT_NE(exitCode, 2u) << "The helper did not crash";
    const std::vector<std::filesystem::path> dumps = DumpFiles(directory);
    ASSERT_EQ(dumps.size(), 1u);
    EXPECT_TRUE(dumps[0].filename().string().starts_with("helper-"));
    EXPECT_GT(std::filesystem::file_size(dumps[0]), 0u);
    std::filesystem::remove_all(directory);
}

INSTANTIATE_TEST_SUITE_P(Platform, CrashDumpTest, testing::Values(L"access-violation", L"abort", L"assert"),
                         [](const testing::TestParamInfo<const wchar_t*>& info) {
                             const std::wstring mode = info.param;
                             return mode == L"access-violation" ? std::string("AccessViolation")
                                                                : (mode == L"abort" ? "Abort" : "Assert");
                         });

#endif
