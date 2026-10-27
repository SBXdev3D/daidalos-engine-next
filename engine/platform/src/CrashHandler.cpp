// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/logging/Log.hpp>
#include <daidalos/platform/CrashHandler.hpp>

#include <format>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
// dbghelp.h must come after windows.h.
#include <dbghelp.h>

#include <array>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#endif

namespace daidalos::platform {

namespace {

constexpr std::string_view kModule = "platform";

} // namespace

#if defined(_WIN32)

namespace {

// Crash handling is process-wide by nature. Everything the handler needs is prepared at installation,
// so that writing a dump does not allocate.
struct CrashState {
    bool installed = false;
    std::array<wchar_t, 1024> directory{};
    std::array<wchar_t, 64> filePrefix{};
    LPTOP_LEVEL_EXCEPTION_FILTER previousFilter = nullptr;
    _crt_signal_t previousAbortHandler = SIG_DFL;
    _invalid_parameter_handler previousInvalidParameterHandler = nullptr;
    _purecall_handler previousPurecallHandler = nullptr;
    unsigned int previousAbortBehavior = 0;
};

CrashState s_crash;
std::atomic<bool> s_dumpWritten{false};

constexpr DWORD kExitCode = 3; // the exit code of abort()
constexpr DWORD kDumpHereException = 0xE0000DA1;

void WriteDump(EXCEPTION_POINTERS* exception) {
    // Only the first crash is dumped, even if several threads crash or the handler itself crashes.
    if (s_dumpWritten.exchange(true)) {
        return;
    }

    SYSTEMTIME time{};
    GetLocalTime(&time);
    std::array<wchar_t, 1200> path{};
    const int length = std::swprintf(path.data(), path.size(), L"%ls\\%ls-%04u%02u%02u-%02u%02u%02u-%lu.dmp",
                                     s_crash.directory.data(), s_crash.filePrefix.data(), time.wYear, time.wMonth,
                                     time.wDay, time.wHour, time.wMinute, time.wSecond, GetCurrentProcessId());
    if (length < 0) {
        return;
    }

    HANDLE file = CreateFileW(path.data(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        std::fputs("Crash: cannot create the dump file\n", stderr);
        return;
    }

    MINIDUMP_EXCEPTION_INFORMATION exceptionInfo{GetCurrentThreadId(), exception, FALSE};
    const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo |
                                                 MiniDumpWithUnloadedModules);
    const BOOL written = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type,
                                           exception != nullptr ? &exceptionInfo : nullptr, nullptr, nullptr);
    CloseHandle(file);

    if (written) {
        std::fwprintf(stderr, L"Crash: dump written to %ls\n", path.data());
    } else {
        std::fputs("Crash: writing the dump failed\n", stderr);
    }
}

LONG DumpFilter(EXCEPTION_POINTERS* exception) {
    WriteDump(exception);
    return EXCEPTION_EXECUTE_HANDLER;
}

// Writes a dump from a handler that has no exception context (abort, CRT callbacks). Raising an exception
// gives the dump a context that points at the caller. This function holds no C++ objects, as __try requires.
void WriteDumpHere() {
    __try {
        RaiseException(kDumpHereException, 0, 0, nullptr);
    } __except (DumpFilter(GetExceptionInformation())) {
    }
}

LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS* exception) {
    return DumpFilter(exception);
}

void OnAbort(int) {
    WriteDumpHere();
    // Returning lets abort() terminate the process.
}

void OnInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t) {
    WriteDumpHere();
    TerminateProcess(GetCurrentProcess(), kExitCode);
}

void OnPureCall() {
    WriteDumpHere();
    TerminateProcess(GetCurrentProcess(), kExitCode);
}

} // namespace

bool CrashDumpsSupported() noexcept {
    return true;
}

core::Result<void> InstallCrashHandler(const CrashHandlerConfig& config) {
    if (s_crash.installed) {
        return core::MakeError(core::ErrorCode::InvalidState, std::string(kModule),
                               "A crash handler is already installed");
    }

    std::error_code error;
    std::filesystem::create_directories(config.dumpDirectory, error);
    const std::filesystem::path directory = std::filesystem::absolute(config.dumpDirectory, error);
    if (error) {
        return core::MakeError(core::ErrorCode::IoError, std::string(kModule),
                               std::format("Cannot create the crash dump directory '{}': {}",
                                           config.dumpDirectory.generic_string(), error.message()));
    }

    const std::wstring directoryText = directory.wstring();
    const std::wstring prefixText = std::filesystem::path(config.filePrefix).wstring();
    if (directoryText.size() >= s_crash.directory.size() || prefixText.size() >= s_crash.filePrefix.size()) {
        return core::MakeError(core::ErrorCode::InvalidArgument, std::string(kModule),
                               "Crash dump directory or file prefix is too long");
    }

    s_crash = CrashState{};
    directoryText.copy(s_crash.directory.data(), directoryText.size());
    prefixText.copy(s_crash.filePrefix.data(), prefixText.size());
    s_dumpWritten.store(false);

    s_crash.previousFilter = SetUnhandledExceptionFilter(&OnUnhandledException);
    s_crash.previousAbortHandler = std::signal(SIGABRT, &OnAbort);
    s_crash.previousInvalidParameterHandler = _set_invalid_parameter_handler(&OnInvalidParameter);
    s_crash.previousPurecallHandler = _set_purecall_handler(&OnPureCall);
    // The dump replaces the abort message box and the Windows Error Reporting dialog.
    s_crash.previousAbortBehavior = _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    s_crash.installed = true;

    DAI_LOG_DEBUG(kModule, "Crash handler installed, dumps go to '{}'", directory.generic_string());
    return {};
}

void UninstallCrashHandler() noexcept {
    if (!s_crash.installed) {
        return;
    }
    SetUnhandledExceptionFilter(s_crash.previousFilter);
    std::signal(SIGABRT, s_crash.previousAbortHandler);
    _set_invalid_parameter_handler(s_crash.previousInvalidParameterHandler);
    _set_purecall_handler(s_crash.previousPurecallHandler);
    _set_abort_behavior(s_crash.previousAbortBehavior, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    s_crash.installed = false;
}

#else

namespace {

bool s_installed = false;

} // namespace

bool CrashDumpsSupported() noexcept {
    return false;
}

core::Result<void> InstallCrashHandler(const CrashHandlerConfig&) {
    if (s_installed) {
        return core::MakeError(core::ErrorCode::InvalidState, std::string(kModule),
                               "A crash handler is already installed");
    }
    s_installed = true;
    DAI_LOG_DEBUG(kModule, "Crash dumps are not supported on this system; core dumps are left to the OS");
    return {};
}

void UninstallCrashHandler() noexcept {
    s_installed = false;
}

#endif

} // namespace daidalos::platform
