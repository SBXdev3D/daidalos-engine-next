// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/core/Assert.hpp>

#include <atomic>
#include <cstdio>
#include <cstdlib>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace daidalos::core {

namespace {

void DefaultAssertHandler(const AssertInfo& info) {
    std::fprintf(stderr, "Assertion failed: %.*s\n  Message: %.*s\n  At: %s:%u (%s)\n",
                 static_cast<int>(info.expression.size()), info.expression.data(),
                 static_cast<int>(info.message.size()), info.message.data(), info.location.file_name(),
                 static_cast<unsigned>(info.location.line()), info.location.function_name());
    std::fflush(stderr);

#if defined(_WIN32)
    if (IsDebuggerPresent()) {
        __debugbreak();
    }
#endif
    std::abort();
}

std::atomic<AssertHandler> g_assertHandler{&DefaultAssertHandler};

} // namespace

AssertHandler SetAssertHandler(AssertHandler handler) noexcept {
    return g_assertHandler.exchange(handler != nullptr ? handler : &DefaultAssertHandler);
}

namespace detail {

void ReportAssertFailure(std::string_view expression, std::string_view message, std::source_location location) {
    g_assertHandler.load()(AssertInfo{expression, message, location});
}

} // namespace detail

} // namespace daidalos::core
