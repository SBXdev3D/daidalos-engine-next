// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/core/Result.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <string>

using namespace daidalos::core;

namespace {

Result<int> ParsePositive(int value) {
    if (value <= 0) {
        return MakeError(ErrorCode::InvalidArgument, "test", "Value must be positive");
    }
    return value;
}

Result<void> Validate(bool valid) {
    if (!valid) {
        return MakeError(ErrorCode::InvalidState, "test", "Invalid");
    }
    return {};
}

} // namespace

TEST(Result, HoldsValueOnSuccess) {
    const Result<int> result = ParsePositive(42);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 42);
}

TEST(Result, HoldsErrorOnFailure) {
    const Result<int> result = ParsePositive(-1);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::InvalidArgument);
    EXPECT_EQ(result.error().module, "test");
    EXPECT_EQ(result.error().message, "Value must be positive");
}

TEST(Result, VoidResultReportsSuccessAndFailure) {
    EXPECT_TRUE(Validate(true).has_value());

    const Result<void> failure = Validate(false);
    ASSERT_FALSE(failure.has_value());
    EXPECT_EQ(failure.error().code, ErrorCode::InvalidState);
}

TEST(Result, ChainStopsAtFirstError) {
    int laterStepCalls = 0;
    const Result<int> result = ParsePositive(-5)
                                   .and_then([](int value) { return ParsePositive(value * 2); })
                                   .and_then([&](int value) -> Result<int> {
                                       ++laterStepCalls;
                                       return value;
                                   });

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::InvalidArgument);
    EXPECT_EQ(laterStepCalls, 0);
}

TEST(Result, ChainPropagatesValues) {
    const Result<int> result = ParsePositive(3).and_then([](int value) { return ParsePositive(value * 2); });
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 6);
}

TEST(Result, MapTransformsValueAndKeepsError) {
    EXPECT_EQ(ParsePositive(2).map([](int value) { return std::to_string(value); }).value(), "2");

    const Result<std::string> failure = ParsePositive(0).map([](int value) { return std::to_string(value); });
    ASSERT_FALSE(failure.has_value());
    EXPECT_EQ(failure.error().code, ErrorCode::InvalidArgument);
}

TEST(Result, SupportsMoveOnlyValues) {
    Result<std::unique_ptr<int>> result = std::make_unique<int>(7);
    ASSERT_TRUE(result.has_value());

    std::unique_ptr<int> owned = std::move(*result);
    ASSERT_NE(owned, nullptr);
    EXPECT_EQ(*owned, 7);
}
