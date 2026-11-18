// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>

#include <glm/vec3.hpp>

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace daidalos::scene {

/// The text a game shows, by key, in one language (ADR-0075): menus, subtitles and captions. A file
/// Text/<language>.json under the asset root holds one, a flat object of keys and strings, "language.name"
/// naming the language in itself.
class TextTable {
public:
    /// Parses a table. What is not an object of strings is an error that names it.
    [[nodiscard]] static core::Result<TextTable> Parse(std::string_view json, std::string_view name = "text");
    /// English, then the language over it, so that a key the translation lacks shows in English. A file
    /// missing or wrong is logged, and what could be read is kept.
    [[nodiscard]] static TextTable Load(const std::filesystem::path& assetRoot, std::string_view language);
    /// The languages there are files for, by their code (the file's name).
    [[nodiscard]] static std::vector<std::string> Languages(const std::filesystem::path& assetRoot);

    /// The string of a key, or the key itself, so that a missing string shows where it is missing.
    [[nodiscard]] std::string_view Get(std::string_view key) const;
    [[nodiscard]] bool Has(std::string_view key) const { return m_strings.contains(key); }
    [[nodiscard]] std::size_t Size() const { return m_strings.size(); }
    /// Puts other's strings over this table's.
    void Overlay(const TextTable& other);

private:
    std::map<std::string, std::string, std::less<>> m_strings;
};

/// A line of text over the game (ADR-0075): a subtitle, what someone says, or a caption, what a sound is.
/// Its text is a key of the text table.
struct Caption {
    std::string text;
    /// Who speaks, a key of the text table; empty for a sound.
    std::string speaker;
    float seconds = 3.0f;
    /// A sound's caption: where it comes from, and how far it is heard. Shown to a listener within reach,
    /// with the side it comes from.
    std::optional<glm::vec3> position;
    float reach = 0.0f;
    /// Seconds shown so far.
    float age = 0.0f;
    [[nodiscard]] bool IsSound() const { return speaker.empty(); }
};

} // namespace daidalos::scene
