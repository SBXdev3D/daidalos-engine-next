// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/core/Error.hpp>

#include <gtest/gtest.h>

#include <set>
#include <string>

using namespace daidalos::core;

TEST(ErrorCode, EveryCodeHasADistinctName) {
    const ErrorCode codes[] = {
        ErrorCode::Unknown,       ErrorCode::InvalidArgument, ErrorCode::InvalidState, ErrorCode::NotFound,
        ErrorCode::AlreadyExists, ErrorCode::IoError,         ErrorCode::ParseError,   ErrorCode::NotSupported,
        ErrorCode::OutOfMemory,   ErrorCode::DeviceError,
    };

    std::set<std::string> names;
    for (const ErrorCode code : codes) {
        const std::string name(ToString(code));
        EXPECT_FALSE(name.empty());
        EXPECT_TRUE(names.insert(name).second) << "Duplicate name: " << name;
    }
}

TEST(ErrorCode, NameMatchesEnumerator) {
    EXPECT_EQ(ToString(ErrorCode::NotFound), "NotFound");
    EXPECT_EQ(ToString(ErrorCode::DeviceError), "DeviceError");
}

TEST(Error, DefaultErrorIsUnknownAndEmpty) {
    const Error error;
    EXPECT_EQ(error.code, ErrorCode::Unknown);
    EXPECT_TRUE(error.module.empty());
    EXPECT_TRUE(error.message.empty());
}

TEST(Error, FormatIncludesModuleCodeAndMessage) {
    const Error error{ErrorCode::NotFound, "resources", "Missing texture 'wall.ktx2'"};
    EXPECT_EQ(Format(error), "[resources] NotFound: Missing texture 'wall.ktx2'");
}

TEST(Error, FormatWithEmptyFieldsKeepsStructure) {
    EXPECT_EQ(Format(Error{}), "[] Unknown: ");
}
