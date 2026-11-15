// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>

#include <concepts>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace daidalos::core {

/// Value of a configuration variable: integers are 64-bit, reals are double.
using CVarValue = std::variant<bool, std::int64_t, double, std::string>;

template <typename T>
concept CVarType =
    std::same_as<T, bool> || std::same_as<T, std::int64_t> || std::same_as<T, double> || std::same_as<T, std::string>;

/// Formats a value the way it is written in config files: true/false, 42, 0.5, text.
[[nodiscard]] std::string ToString(const CVarValue& value);

/// One "name value" line of config text.
struct ConfigLine {
    std::string name;
    /// Without its quotes.
    std::string value;
    /// From 1, for messages.
    std::size_t number = 0;
};

/// Reads config text: one "name value" per line, '#' starts a comment, and a value in double quotes
/// keeps its spaces (there are no escape sequences). sourceName appears in error messages. Fails at the
/// first invalid line. Names are not checked here.
[[nodiscard]] Result<std::vector<ConfigLine>> ParseConfigLines(std::string_view text, std::string_view sourceName);

/// A line that ParseConfigLines reads back as this name and value: in double quotes when the value is
/// empty or holds a space, a tab or '#'. Values cannot hold double quotes or line breaks.
[[nodiscard]] std::string FormatConfigLine(std::string_view name, std::string_view value);

/// A registered variable.
struct CVarEntry {
    std::string name;
    std::string description;
    CVarValue value;
    CVarValue defaultValue;
    /// Whether a config file, the command line or a Set gave it its value, rather than its default.
    bool set = false;
};

/// Typed, read-only view of a registered variable. It stays valid as long as its registry.
template <CVarType T>
class CVar {
public:
    [[nodiscard]] const T& Get() const { return std::get<T>(m_entry->value); }
    /// Whether something set it, rather than its default: a setting that a quality preset decides
    /// follows the preset until it is set.
    [[nodiscard]] bool IsSet() const noexcept { return m_entry->set; }
    [[nodiscard]] const std::string& Name() const noexcept { return m_entry->name; }

private:
    friend class CVarRegistry;
    explicit CVar(const CVarEntry& entry) : m_entry(&entry) {}

    const CVarEntry* m_entry;
};

/// Configuration variables ("cvars"), set from config files and the command line.
///
/// Values can be set before their variable is registered: the config file is read at startup, before the
/// modules that own the variables exist. Such values are kept and applied when the variable is registered.
///
/// Names use lowercase letters, digits, '_' and '.', for example "window.width".
class CVarRegistry {
public:
    CVarRegistry() = default;
    CVarRegistry(const CVarRegistry&) = delete;
    CVarRegistry& operator=(const CVarRegistry&) = delete;

    /// Registers a variable. The type is explicit: Register<std::int64_t>("window.width", 1280, "...").
    /// Registering a name twice, or an invalid name, is a programming error.
    /// A value set earlier for this name is applied; if it does not parse as T, the default is kept
    /// and the problem is reported by TakeErrors.
    template <CVarType T>
    CVar<T> Register(std::string_view name, std::type_identity_t<T> defaultValue, std::string_view description) {
        return CVar<T>(Add(name, CVarValue(std::move(defaultValue)), description));
    }

    /// Sets a variable from text, parsed according to its type. A value for an unknown name is kept
    /// until that name is registered.
    Result<void> Set(std::string_view name, std::string_view text);

    /// Puts a variable back to its default, as if nothing had set it, and forgets a value kept for it
    /// before it was registered.
    void Reset(std::string_view name);

    /// Applies config text, as read by ParseConfigLines. Stops at the first invalid line, after applying
    /// none of them if the text itself is invalid.
    Result<void> ParseConfig(std::string_view text, std::string_view sourceName);

    /// Reads a file and applies it with ParseConfig. Fails with NotFound if the file does not exist.
    Result<void> LoadConfigFile(const std::filesystem::path& path);

    /// Applies "+name value" argument pairs. Other arguments are ignored.
    Result<void> ApplyCommandLine(std::span<const std::string> arguments);

    /// Returns the variable with this name, or nullptr if it is not registered.
    [[nodiscard]] const CVarEntry* Find(std::string_view name) const;

    /// Calls visitor for each registered variable, in name order.
    void ForEach(const std::function<void(const CVarEntry&)>& visitor) const;

    /// Names that were given a value but never registered, in name order. Usually a typo in a config file.
    [[nodiscard]] std::vector<std::string> UnregisteredNames() const;

    /// Returns and clears the errors found while applying earlier values at registration.
    [[nodiscard]] std::vector<Error> TakeErrors();

private:
    const CVarEntry& Add(std::string_view name, CVarValue defaultValue, std::string_view description);

    std::deque<CVarEntry> m_entries; // a deque keeps entry addresses stable for CVar handles
    std::map<std::string, CVarEntry*, std::less<>> m_byName;
    std::map<std::string, std::string, std::less<>> m_pending;
    std::vector<Error> m_errors;
};

} // namespace daidalos::core
