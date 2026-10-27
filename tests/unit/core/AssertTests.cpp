// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/core/Assert.hpp>

#include <gtest/gtest.h>

#include <string>

using namespace daidalos::core;

namespace {

struct CapturedFailure {
    int count = 0;
    std::string expression;
    std::string message;
    std::string file;
    unsigned line = 0;
};

CapturedFailure g_captured;

void CapturingHandler(const AssertInfo& info) {
    ++g_captured.count;
    g_captured.expression = info.expression;
    g_captured.message = info.message;
    g_captured.file = info.location.file_name();
    g_captured.line = static_cast<unsigned>(info.location.line());
}

class AssertTest : public testing::Test {
protected:
    void SetUp() override {
        if (!DAI_ASSERTS_ENABLED) {
            GTEST_SKIP() << "Assertions are disabled in this configuration";
        }
        g_captured = {};
        m_previous = SetAssertHandler(&CapturingHandler);
    }

    void TearDown() override {
        if (DAI_ASSERTS_ENABLED) {
            SetAssertHandler(m_previous);
        }
    }

private:
    AssertHandler m_previous = nullptr;
};

} // namespace

TEST_F(AssertTest, PassingAssertionDoesNotCallHandler) {
    int value = 2;
    DAI_ASSERT(value == 2, "Value is two");
    EXPECT_EQ(g_captured.count, 0);
}

TEST_F(AssertTest, FailingAssertionReportsExpressionMessageAndCallSite) {
    int value = 2;
    const unsigned expectedLine = __LINE__ + 1;
    DAI_ASSERT(value == 3, "Value is not three");

    EXPECT_EQ(g_captured.count, 1);
    EXPECT_EQ(g_captured.expression, "value == 3");
    EXPECT_EQ(g_captured.message, "Value is not three");
    EXPECT_NE(g_captured.file.find("AssertTests.cpp"), std::string::npos);
    EXPECT_EQ(g_captured.line, expectedLine);
}

TEST_F(AssertTest, ConditionIsEvaluatedOnce) {
    int evaluations = 0;
    DAI_ASSERT(++evaluations == 0, "Side effect counter");
    EXPECT_EQ(evaluations, 1);
}

TEST_F(AssertTest, SetAssertHandlerReturnsPreviousHandler) {
    const AssertHandler previous = SetAssertHandler(nullptr);
    EXPECT_EQ(previous, &CapturingHandler);
    SetAssertHandler(&CapturingHandler);
}

using AssertDeathTest = AssertTest;

TEST_F(AssertDeathTest, DefaultHandlerAborts) {
    SetAssertHandler(nullptr);
    bool invariantHolds = false;
    EXPECT_DEATH(DAI_ASSERT(invariantHolds, "Fatal invariant"), "Fatal invariant");
}

TEST(AssertDisabled, ConditionIsNotEvaluatedWhenAssertionsAreDisabled) {
    if (DAI_ASSERTS_ENABLED) {
        GTEST_SKIP() << "Assertions are enabled in this configuration";
    }
    int evaluations = 0;
    DAI_ASSERT(++evaluations == 0, "Never evaluated");
    EXPECT_EQ(evaluations, 0);
}
