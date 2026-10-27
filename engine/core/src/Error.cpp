// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/core/Error.hpp>

namespace daidalos::core {

std::string_view ToString(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::Unknown:
        return "Unknown";
    case ErrorCode::InvalidArgument:
        return "InvalidArgument";
    case ErrorCode::InvalidState:
        return "InvalidState";
    case ErrorCode::NotFound:
        return "NotFound";
    case ErrorCode::AlreadyExists:
        return "AlreadyExists";
    case ErrorCode::IoError:
        return "IoError";
    case ErrorCode::ParseError:
        return "ParseError";
    case ErrorCode::NotSupported:
        return "NotSupported";
    case ErrorCode::OutOfMemory:
        return "OutOfMemory";
    case ErrorCode::DeviceError:
        return "DeviceError";
    }
    return "Unknown";
}

std::string Format(const Error& error) {
    std::string text;
    text.reserve(error.module.size() + error.message.size() + 24);
    text += '[';
    text += error.module;
    text += "] ";
    text += ToString(error.code);
    text += ": ";
    text += error.message;
    return text;
}

} // namespace daidalos::core
