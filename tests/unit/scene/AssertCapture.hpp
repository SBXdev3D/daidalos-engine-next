// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Assert.hpp>

#include <gtest/gtest.h>

namespace daidalos::test {

/// Thrown by the assert handler that tests install, so that a failed DAI_ASSERT leaves the
/// function before it touches invalid state.
struct AssertFired {};

/// Replaces the assert handler with one that throws AssertFired, for the lifetime of the object.
class ThrowingAssertHandler {
public:
    ThrowingAssertHandler() : m_previous(core::SetAssertHandler(&Throw)) {}
    ThrowingAssertHandler(const ThrowingAssertHandler&) = delete;
    ThrowingAssertHandler& operator=(const ThrowingAssertHandler&) = delete;
    ~ThrowingAssertHandler() { core::SetAssertHandler(m_previous); }

private:
    static void Throw(const core::AssertInfo&) { throw AssertFired{}; }

    core::AssertHandler m_previous;
};

} // namespace daidalos::test

// Expects the statement to fail a DAI_ASSERT. Skips the test when assertions are compiled out.
#define DAI_EXPECT_ASSERT(statement)                                                                                   \
    do {                                                                                                               \
        if (!DAI_ASSERTS_ENABLED) {                                                                                    \
            GTEST_SKIP() << "Assertions are disabled in this configuration";                                           \
        }                                                                                                              \
        const ::daidalos::test::ThrowingAssertHandler daiAssertHandler;                                                \
        EXPECT_THROW(statement, ::daidalos::test::AssertFired);                                                        \
    } while (false)
