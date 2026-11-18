// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>

#include <simdjson.h>

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// The JSON tree the scene's files are read into and written from: level files (ADR-0054), sequences and glTF
// materials written back by the editor (ADR-0079). Internal to the scene module.
namespace daidalos::scene::detail {

// A JSON value, read from a file or about to be written. Objects keep their keys sorted, which is the
// order they are written in.
struct Json;
using JsonArray = std::vector<Json>;
using JsonObject = std::map<std::string, Json, std::less<>>;
struct Json {
    std::variant<std::nullptr_t, bool, double, std::string, JsonArray, JsonObject> value;
};

/// The tree of a parsed document. Keys that appear twice in an object are errors.
[[nodiscard]] core::Result<Json> FromSimdjson(simdjson::dom::element element);
/// Parses a document into a tree; what is wrong is an error that names what.
[[nodiscard]] core::Result<Json> ParseJson(std::string_view text, std::string_view what);
/// Writes a value: keys sorted, two-space indents, arrays of plain values on one line, and numbers in the
/// shortest form that reads back as the same float, or as the same double for integers no float holds.
void Dump(std::string& out, const Json& json, int depth);
/// Dump from the top, with a final newline.
[[nodiscard]] std::string WriteJson(const Json& json);

} // namespace daidalos::scene::detail
