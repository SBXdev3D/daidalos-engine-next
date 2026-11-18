// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "JsonTree.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>

namespace daidalos::scene::detail {

namespace {

void Escape(std::string& out, std::string_view text) {
    out += '"';
    for (const char character : text) {
        switch (character) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\t':
            out += "\\t";
            break;
        case '\r':
            out += "\\r";
            break;
        default:
            if (static_cast<unsigned char>(character) < 0x20) {
                out += std::format("\\u{:04x}", static_cast<unsigned>(character));
            } else {
                out += character;
            }
        }
    }
    out += '"';
}

/// Arrays of plain values stay on one line: vectors read as vectors.
bool IsFlat(const JsonArray& array) {
    return std::ranges::none_of(array, [](const Json& item) {
        return std::holds_alternative<JsonArray>(item.value) || std::holds_alternative<JsonObject>(item.value);
    });
}

} // namespace

core::Result<Json> FromSimdjson(simdjson::dom::element element) {
    switch (element.type()) {
    case simdjson::dom::element_type::ARRAY: {
        JsonArray array;
        for (simdjson::dom::element item : element.get_array().value_unsafe()) {
            core::Result<Json> converted = FromSimdjson(item);
            if (!converted) {
                return tl::unexpected(converted.error());
            }
            array.push_back(std::move(*converted));
        }
        return Json{std::move(array)};
    }
    case simdjson::dom::element_type::OBJECT: {
        JsonObject object;
        for (const simdjson::dom::key_value_pair field : element.get_object().value_unsafe()) {
            core::Result<Json> converted = FromSimdjson(field.value);
            if (!converted) {
                return tl::unexpected(converted.error());
            }
            if (!object.emplace(std::string(field.key), std::move(*converted)).second) {
                return core::MakeError(core::ErrorCode::ParseError, "scene",
                                       std::format("Level: \"{}\" appears twice", field.key));
            }
        }
        return Json{std::move(object)};
    }
    case simdjson::dom::element_type::INT64:
        return Json{static_cast<double>(element.get_int64().value_unsafe())};
    case simdjson::dom::element_type::UINT64:
        return Json{static_cast<double>(element.get_uint64().value_unsafe())};
    case simdjson::dom::element_type::DOUBLE:
        return Json{element.get_double().value_unsafe()};
    case simdjson::dom::element_type::STRING:
        return Json{std::string(element.get_string().value_unsafe())};
    case simdjson::dom::element_type::BOOL:
        return Json{element.get_bool().value_unsafe()};
    case simdjson::dom::element_type::NULL_VALUE:
        return Json{nullptr};
    }
    return Json{nullptr};
}

core::Result<Json> ParseJson(std::string_view text, std::string_view what) {
    simdjson::dom::parser parser;
    simdjson::dom::element root;
    if (const simdjson::error_code error = parser.parse(text.data(), text.size()).get(root); error) {
        return core::MakeError(core::ErrorCode::ParseError, "scene",
                               std::format("{} is not valid JSON: {}", what, simdjson::error_message(error)));
    }
    return FromSimdjson(root);
}

void Dump(std::string& out, const Json& json, int depth) {
    const auto indent = [&out](int level) { out.append(static_cast<std::size_t>(level) * 2, ' '); };
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, std::nullptr_t>) {
                out += "null";
            } else if constexpr (std::is_same_v<T, bool>) {
                out += value ? "true" : "false";
            } else if constexpr (std::is_same_v<T, double>) {
                // Values are floats: their shortest form reads back as the same float.
                const auto single = static_cast<float>(value);
                if (static_cast<double>(single) == value || std::floor(value) != value) {
                    char buffer[32];
                    const auto written = std::to_chars(buffer, buffer + sizeof(buffer), single);
                    out.append(buffer, written.ptr);
                } else {
                    char buffer[32];
                    const auto written = std::to_chars(buffer, buffer + sizeof(buffer), value);
                    out.append(buffer, written.ptr);
                }
            } else if constexpr (std::is_same_v<T, std::string>) {
                Escape(out, value);
            } else if constexpr (std::is_same_v<T, JsonArray>) {
                if (value.empty()) {
                    out += "[]";
                } else if (IsFlat(value)) {
                    out += '[';
                    for (std::size_t index = 0; index < value.size(); ++index) {
                        out += index == 0 ? "" : ", ";
                        Dump(out, value[index], depth + 1);
                    }
                    out += ']';
                } else {
                    out += "[\n";
                    for (std::size_t index = 0; index < value.size(); ++index) {
                        indent(depth + 1);
                        Dump(out, value[index], depth + 1);
                        out += index + 1 < value.size() ? ",\n" : "\n";
                    }
                    indent(depth);
                    out += ']';
                }
            } else {
                if (value.empty()) {
                    out += "{}";
                    return;
                }
                out += "{\n";
                std::size_t index = 0;
                for (const auto& [key, item] : value) {
                    indent(depth + 1);
                    Escape(out, key);
                    out += ": ";
                    Dump(out, item, depth + 1);
                    out += ++index < value.size() ? ",\n" : "\n";
                }
                indent(depth);
                out += '}';
            }
        },
        json.value);
}

std::string WriteJson(const Json& json) {
    std::string out;
    Dump(out, json, 0);
    out += '\n';
    return out;
}

} // namespace daidalos::scene::detail
