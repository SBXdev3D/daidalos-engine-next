// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/logging/Log.hpp>
#include <daidalos/scene/Text.hpp>

#include <simdjson.h>

#include <algorithm>
#include <format>
#include <fstream>
#include <sstream>

namespace daidalos::scene {

namespace {

constexpr std::string_view kModule = "scene";

std::optional<std::string> ReadFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }
    std::stringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

} // namespace

core::Result<TextTable> TextTable::Parse(std::string_view json, std::string_view name) {
    simdjson::dom::parser parser;
    simdjson::dom::element root;
    if (const simdjson::error_code error = parser.parse(json.data(), json.size()).get(root); error) {
        return core::MakeError(core::ErrorCode::ParseError, kModule,
                               std::format("Text '{}': {}", name, simdjson::error_message(error)));
    }
    simdjson::dom::object object;
    if (root.get_object().get(object) != simdjson::SUCCESS) {
        return core::MakeError(core::ErrorCode::ParseError, kModule,
                               std::format("Text '{}': must be an object of keys and strings", name));
    }
    TextTable table;
    for (const auto field : object) {
        std::string_view text;
        if (field.value.get_string().get(text) != simdjson::SUCCESS) {
            return core::MakeError(core::ErrorCode::ParseError, kModule,
                                   std::format("Text '{}': '{}' must be a string", name, field.key));
        }
        table.m_strings.insert_or_assign(std::string(field.key), std::string(text));
    }
    return table;
}

TextTable TextTable::Load(const std::filesystem::path& assetRoot, std::string_view language) {
    TextTable table;
    const auto read = [&](std::string_view code) {
        const std::filesystem::path path = assetRoot / "Text" / (std::string(code) + ".json");
        const std::optional<std::string> json = ReadFile(path);
        if (!json.has_value()) {
            DAI_LOG_WARNING(kModule, "No text for language '{}' at '{}'", code, path.generic_string());
            return;
        }
        core::Result<TextTable> parsed = Parse(*json, path.filename().generic_string());
        if (!parsed) {
            DAI_LOG_WARNING(kModule, "{}", core::Format(parsed.error()));
            return;
        }
        table.Overlay(*parsed);
    };
    read("en");
    if (!language.empty() && language != "en") {
        read(language);
    }
    return table;
}

std::vector<std::string> TextTable::Languages(const std::filesystem::path& assetRoot) {
    std::vector<std::string> languages;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(assetRoot / "Text", error)) {
        if (entry.path().extension() == ".json") {
            languages.push_back(entry.path().stem().generic_string());
        }
    }
    std::sort(languages.begin(), languages.end());
    return languages;
}

std::string_view TextTable::Get(std::string_view key) const {
    const auto found = m_strings.find(key);
    return found == m_strings.end() ? key : std::string_view(found->second);
}

void TextTable::Overlay(const TextTable& other) {
    for (const auto& [key, text] : other.m_strings) {
        m_strings.insert_or_assign(key, text);
    }
}

} // namespace daidalos::scene
