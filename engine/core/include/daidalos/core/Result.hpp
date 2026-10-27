// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <daidalos/core/Error.hpp>

#include <tl/expected.hpp>

#include <string>
#include <utility>

namespace daidalos::core {

/// Return type of every operation that can fail (ADR-0003).
/// Holds either a value of type T or an Error. Result<void> carries no value.
template <typename T>
using Result = tl::expected<T, Error>;

/// Builds the error side of a Result:
///   return MakeError(ErrorCode::NotFound, "resources", "Missing texture 'wall.ktx2'");
[[nodiscard]] inline tl::unexpected<Error> MakeError(ErrorCode code, std::string module, std::string message) {
    return tl::unexpected<Error>(Error{code, std::move(module), std::move(message)});
}

} // namespace daidalos::core
