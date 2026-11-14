// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <source_location>
#include <string_view>

namespace daidalos::core {

/// Information passed to the assert handler when an assertion fails.
struct AssertInfo {
    std::string_view expression;
    std::string_view message;
    std::source_location location;
};

using AssertHandler = void (*)(const AssertInfo& info);

/// Replaces the function called when an assertion fails and returns the previous one.
/// Passing nullptr restores the default handler, which prints the failure to stderr,
/// breaks into the debugger if one is attached, and aborts.
/// A custom handler may return, in which case execution continues after the assertion;
/// this is meant for tests.
AssertHandler SetAssertHandler(AssertHandler handler) noexcept;

namespace detail {

void ReportAssertFailure(std::string_view expression, std::string_view message,
                         std::source_location location = std::source_location::current());

} // namespace detail

} // namespace daidalos::core

// DAI_ASSERT checks programming errors and invariants. It must not be used for failures caused
// by data, files or devices: those return an Error (ADR-0003).
// Assertions are enabled in Debug builds, or in any build when DAI_FORCE_ASSERTS is defined.
// When disabled, the condition is not evaluated.
#if !defined(NDEBUG) || defined(DAI_FORCE_ASSERTS)
#define DAI_ASSERTS_ENABLED 1
#define DAI_ASSERT(condition, message)                                                                                 \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            ::daidalos::core::detail::ReportAssertFailure(#condition, message);                                        \
        }                                                                                                              \
    } while (false)
#else
#define DAI_ASSERTS_ENABLED 0
#define DAI_ASSERT(condition, message)                                                                                 \
    do {                                                                                                               \
        (void)sizeof(condition);                                                                                       \
    } while (false)
#endif
