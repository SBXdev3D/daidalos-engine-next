// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/core/Assert.hpp>
#include <daidalos/core/CVarRegistry.hpp>

#include <charconv>
#include <cmath>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <utility>

namespace daidalos::core {

namespace {

constexpr std::string_view kModule = "core";

bool IsValidName(std::string_view name) {
    if (name.empty()) {
        return false;
    }
    for (const char c : name) {
        const bool valid = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.';
        if (!valid) {
            return false;
        }
    }
    return true;
}

std::string_view Trim(std::string_view text) {
    const std::size_t first = text.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) {
        return {};
    }
    const std::size_t last = text.find_last_not_of(" \t\r");
    return text.substr(first, last - first + 1);
}

template <typename Number>
std::optional<Number> ParseNumber(std::string_view text) {
    Number number{};
    const char* end = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(text.data(), end, number);
    if (ec != std::errc{} || ptr != end) {
        return std::nullopt;
    }
    return number;
}

// Parses text into the same alternative as current.
std::optional<CVarValue> ParseAs(const CVarValue& current, std::string_view text) {
    return std::visit(
        [text]<typename T>(const T&) -> std::optional<CVarValue> {
            if constexpr (std::is_same_v<T, bool>) {
                if (text == "true" || text == "1") {
                    return CVarValue(true);
                }
                if (text == "false" || text == "0") {
                    return CVarValue(false);
                }
                return std::nullopt;
            } else if constexpr (std::is_same_v<T, std::int64_t>) {
                if (const std::optional<std::int64_t> number = ParseNumber<std::int64_t>(text)) {
                    return CVarValue(*number);
                }
                return std::nullopt;
            } else if constexpr (std::is_same_v<T, double>) {
                if (const std::optional<double> number = ParseNumber<double>(text); number && std::isfinite(*number)) {
                    return CVarValue(*number);
                }
                return std::nullopt;
            } else {
                return CVarValue(std::string(text));
            }
        },
        current);
}

std::string_view TypeDescription(const CVarValue& value) {
    switch (value.index()) {
    case 0:
        return "a boolean (true, false, 1 or 0)";
    case 1:
        return "an integer";
    case 2:
        return "a finite number";
    default:
        return "a string";
    }
}

Error InvalidValue(const CVarEntry& entry, std::string_view text) {
    return Error{
        ErrorCode::ParseError, std::string(kModule),
        std::format("Invalid value '{}' for '{}': expected {}", text, entry.name, TypeDescription(entry.value))};
}

} // namespace

std::string ToString(const CVarValue& value) {
    return std::visit(
        []<typename T>(const T& v) -> std::string {
            if constexpr (std::is_same_v<T, bool>) {
                return v ? "true" : "false";
            } else if constexpr (std::is_same_v<T, std::string>) {
                return v;
            } else {
                return std::format("{}", v);
            }
        },
        value);
}

const CVarEntry& CVarRegistry::Add(std::string_view name, CVarValue defaultValue, std::string_view description) {
    DAI_ASSERT(IsValidName(name), "CVar names use lowercase letters, digits, '_' and '.'");
    if (const auto existing = m_byName.find(name); existing != m_byName.end()) {
        DAI_ASSERT(false, "CVar registered twice");
        return *existing->second;
    }

    CVarEntry& entry = m_entries.emplace_back(
        CVarEntry{std::string(name), std::string(description), defaultValue, std::move(defaultValue)});
    m_byName.emplace(entry.name, &entry);

    if (const auto pending = m_pending.find(name); pending != m_pending.end()) {
        if (std::optional<CVarValue> parsed = ParseAs(entry.value, pending->second)) {
            entry.value = std::move(*parsed);
        } else {
            m_errors.push_back(InvalidValue(entry, pending->second));
        }
        m_pending.erase(pending);
    }
    return entry;
}

Result<void> CVarRegistry::Set(std::string_view name, std::string_view text) {
    if (!IsValidName(name)) {
        return MakeError(ErrorCode::InvalidArgument, std::string(kModule),
                         std::format("Invalid cvar name '{}': use lowercase letters, digits, '_' and '.'", name));
    }

    const auto found = m_byName.find(name);
    if (found == m_byName.end()) {
        m_pending.insert_or_assign(std::string(name), std::string(text));
        return {};
    }

    CVarEntry& entry = *found->second;
    std::optional<CVarValue> parsed = ParseAs(entry.value, text);
    if (!parsed) {
        return tl::unexpected(InvalidValue(entry, text));
    }
    entry.value = std::move(*parsed);
    return {};
}

Result<void> CVarRegistry::ParseConfig(std::string_view text, std::string_view sourceName) {
    std::size_t lineNumber = 0;
    while (!text.empty()) {
        ++lineNumber;
        const std::size_t lineEnd = text.find('\n');
        std::string_view line = text.substr(0, lineEnd);
        text = (lineEnd == std::string_view::npos) ? std::string_view{} : text.substr(lineEnd + 1);

        const auto lineError = [&](std::string_view message) {
            return MakeError(ErrorCode::ParseError, std::string(kModule),
                             std::format("{}:{}: {}", sourceName, lineNumber, message));
        };

        line = Trim(line);
        if (line.empty() || line.front() == '#') {
            continue;
        }

        const std::size_t nameEnd = line.find_first_of(" \t");
        const std::string_view name = line.substr(0, nameEnd);
        const std::string_view rest =
            (nameEnd == std::string_view::npos) ? std::string_view{} : Trim(line.substr(nameEnd));

        std::string_view value;
        if (!rest.empty() && rest.front() == '"') {
            const std::size_t closing = rest.find('"', 1);
            if (closing == std::string_view::npos) {
                return lineError("missing closing quote");
            }
            value = rest.substr(1, closing - 1);
            const std::string_view after = Trim(rest.substr(closing + 1));
            if (!after.empty() && after.front() != '#') {
                return lineError("unexpected text after the closing quote");
            }
        } else {
            value = Trim(rest.substr(0, rest.find('#')));
            if (value.empty()) {
                return lineError(std::format("missing value for '{}'", name));
            }
        }

        if (Result<void> result = Set(name, value); !result) {
            return lineError(result.error().message);
        }
    }
    return {};
}

Result<void> CVarRegistry::LoadConfigFile(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) {
        return MakeError(ErrorCode::NotFound, std::string(kModule),
                         std::format("Config file '{}' not found", path.generic_string()));
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return MakeError(ErrorCode::IoError, std::string(kModule),
                         std::format("Cannot open config file '{}'", path.generic_string()));
    }
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (file.bad()) {
        return MakeError(ErrorCode::IoError, std::string(kModule),
                         std::format("Cannot read config file '{}'", path.generic_string()));
    }
    return ParseConfig(text, path.generic_string());
}

Result<void> CVarRegistry::ApplyCommandLine(std::span<const std::string> arguments) {
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        const std::string& argument = arguments[i];
        if (argument.size() < 2 || argument.front() != '+') {
            continue;
        }
        if (i + 1 == arguments.size()) {
            return MakeError(ErrorCode::InvalidArgument, std::string(kModule),
                             std::format("Missing value after '{}' on the command line", argument));
        }
        if (Result<void> result = Set(std::string_view(argument).substr(1), arguments[i + 1]); !result) {
            return result;
        }
        ++i;
    }
    return {};
}

const CVarEntry* CVarRegistry::Find(std::string_view name) const {
    const auto found = m_byName.find(name);
    return (found == m_byName.end()) ? nullptr : found->second;
}

void CVarRegistry::ForEach(const std::function<void(const CVarEntry&)>& visitor) const {
    for (const auto& [name, entry] : m_byName) {
        visitor(*entry);
    }
}

std::vector<std::string> CVarRegistry::UnregisteredNames() const {
    std::vector<std::string> names;
    names.reserve(m_pending.size());
    for (const auto& [name, value] : m_pending) {
        names.push_back(name);
    }
    return names;
}

std::vector<Error> CVarRegistry::TakeErrors() {
    return std::exchange(m_errors, {});
}

} // namespace daidalos::core
