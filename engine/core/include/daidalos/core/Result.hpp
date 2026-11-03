// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <daidalos/core/Error.hpp>

#include <tl/expected.hpp>

#include <string>
#include <string_view>
#include <utility>

namespace daidalos::core {

/// Return type of every operation that can fail (ADR-0003).
/// Holds either a value of type T or an Error. Result<void> carries no value.
template <typename T>
using Result = tl::expected<T, Error>;

/// Builds the error side of a Result:
///   return MakeError(ErrorCode::NotFound, "resources", "Missing texture 'wall.ktx2'");
[[nodiscard]] inline tl::unexpected<Error> MakeError(ErrorCode code, std::string_view module, std::string message) {
    return tl::unexpected<Error>(Error{code, module, std::move(message)});
}

} // namespace daidalos::core

// Forwarding the error of a step is what most failures do, so it takes one line (ADR-0003):
//
//   DAI_TRY(device.UploadBuffer(buffer, bytes));              // Result<void>
//   DAI_TRY_ASSIGN(rhi::BufferHandle buffer, CreateBuffer()); // Result<T>: declares the value
//
// The enclosing function returns any Result. Steps that must undo something on failure keep an
// explicit if instead.
#define DAI_TRY(expression)                                                                                            \
    do {                                                                                                               \
        if (auto&& daiTryResult = (expression); !daiTryResult) {                                                       \
            return ::tl::unexpected(std::move(daiTryResult.error()));                                                  \
        }                                                                                                              \
    } while (false)

#define DAI_TRY_CONCAT_INNER(a, b) a##b
#define DAI_TRY_CONCAT(a, b) DAI_TRY_CONCAT_INNER(a, b)
#define DAI_TRY_ASSIGN(declaration, expression)                                                                        \
    DAI_TRY_ASSIGN_IMPL(declaration, expression, DAI_TRY_CONCAT(daiTryResult, __LINE__))
#define DAI_TRY_ASSIGN_IMPL(declaration, expression, result)                                                           \
    auto result = (expression);                                                                                        \
    if (!result) {                                                                                                     \
        return ::tl::unexpected(std::move(result.error()));                                                            \
    }                                                                                                                  \
    declaration = std::move(*result)
