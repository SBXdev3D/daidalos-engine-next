// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <string>
#include <string_view>

namespace daidalos::core {

/// Broad category of a failure. The message of an Error carries the details.
enum class ErrorCode {
    Unknown,
    InvalidArgument,
    InvalidState,
    NotFound,
    AlreadyExists,
    IoError,
    ParseError,
    NotSupported,
    OutOfMemory,
    DeviceError,
};

/// Returns the name of an error code, for example "NotFound".
[[nodiscard]] std::string_view ToString(ErrorCode code) noexcept;

/// Describes a failure: its category, the module that reported it, and enough context
/// (path, entity, operation) to diagnose it.
struct Error {
    ErrorCode code = ErrorCode::Unknown;
    std::string module;
    std::string message;
};

/// Formats an error as "[module] Code: message".
[[nodiscard]] std::string Format(const Error& error);

} // namespace daidalos::core
