// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/platform/CrashHandler.hpp>
#include <daidalos/platform/CrashReport.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
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

TEST(CrashReportTest, RefusesWhatIsNotADump) {
    const std::filesystem::path directory = TestDirectory("crash_report_not_a_dump");
    std::filesystem::create_directories(directory);
    EXPECT_FALSE(platform::ReadCrashDump(directory / "missing.dmp").has_value());
    {
        std::ofstream(directory / "text.dmp") << "not a dump";
    }
    const core::Result<platform::CrashReport> report = platform::ReadCrashDump(directory / "text.dmp");
    ASSERT_FALSE(report.has_value());
    EXPECT_EQ(report.error().code,
              platform::CrashReportsSupported() ? core::ErrorCode::ParseError : core::ErrorCode::NotSupported);
    std::filesystem::remove_all(directory);
}

TEST_F(CrashHandlerTest, UninstallWithoutInstallIsSafe) {
    platform::UninstallCrashHandler();
    platform::UninstallCrashHandler();
}

#if defined(DAI_CRASH_HELPER_PATH)

namespace {

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

namespace {

bool HasFunction(const platform::CrashReport& report, std::string_view name) {
    return std::ranges::any_of(report.stack,
                               [name](const platform::CrashFrame& frame) { return frame.function == name; });
}

} // namespace

TEST(CrashReportTest, AnAccessViolationIsReadWithItsStack) {
    const std::filesystem::path directory = TestDirectory("crash_report_violation");
    RunCrashHelper(directory, L"access-violation");
    const std::vector<std::filesystem::path> dumps = DumpFiles(directory);
    ASSERT_EQ(dumps.size(), 1u);
    const core::Result<platform::CrashReport> report = platform::ReadCrashDump(dumps[0]);
    ASSERT_TRUE(report.has_value()) << core::Format(report.error());
    EXPECT_EQ(report->exceptionCode, 0xC0000005u);
    EXPECT_EQ(report->exception, "access violation");
    EXPECT_EQ(report->detail, "writing 0x0");
    ASSERT_FALSE(report->stack.empty());
    EXPECT_EQ(report->stack[0].module, "daidalos_crash_helper.exe");
    EXPECT_TRUE(HasFunction(*report, "main")) << platform::FormatCrashReport(*report);
    EXPECT_NE(report->stack[0].file.find("CrashHelper.cpp"), std::string::npos) << platform::FormatCrashReport(*report);

    // A report is written once, beside its dump.
    const std::vector<std::filesystem::path> written = platform::ReportNewCrashes(directory);
    ASSERT_EQ(written.size(), 1u);
    std::ifstream text(written[0]);
    std::string first;
    std::getline(text, first);
    EXPECT_EQ(first, "Crash: access violation, writing 0x0");
    EXPECT_TRUE(platform::ReportNewCrashes(directory).empty());
    text.close();
    std::filesystem::remove_all(directory);
}

TEST(CrashReportTest, AnAbortIsReadDownToItsCaller) {
    const std::filesystem::path directory = TestDirectory("crash_report_abort");
    RunCrashHelper(directory, L"abort");
    const std::vector<std::filesystem::path> dumps = DumpFiles(directory);
    ASSERT_EQ(dumps.size(), 1u);
    const core::Result<platform::CrashReport> report = platform::ReadCrashDump(dumps[0]);
    ASSERT_TRUE(report.has_value()) << core::Format(report.error());
    EXPECT_EQ(report->exception, "abort or failed assertion");
    EXPECT_TRUE(HasFunction(*report, "main")) << platform::FormatCrashReport(*report);
    std::filesystem::remove_all(directory);
}

INSTANTIATE_TEST_SUITE_P(Platform, CrashDumpTest, testing::Values(L"access-violation", L"abort", L"assert"),
                         [](const testing::TestParamInfo<const wchar_t*>& info) {
                             const std::wstring mode = info.param;
                             return mode == L"access-violation" ? std::string("AccessViolation")
                                                                : (mode == L"abort" ? "Abort" : "Assert");
                         });

#endif
