// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace editor {

/// A key with its modifiers, as "Ctrl+Shift+O" (ADR-0112). The key is named as Dear ImGui names it ("O", "F5",
/// "Delete", "LeftBracket"); the editor turns names into keys.
struct KeyChord {
    std::string key;
    bool ctrl = false;
    bool shift = false;
    bool alt = false;

    [[nodiscard]] bool Empty() const { return key.empty(); }
    [[nodiscard]] bool operator==(const KeyChord&) const = default;
};

/// "Ctrl+Shift+O" into a chord; nothing for text that names no key.
[[nodiscard]] std::optional<KeyChord> ParseChord(std::string_view text);
/// A chord as "Ctrl+Shift+O"; empty for none.
[[nodiscard]] std::string FormatChord(const KeyChord& chord);

/// Something the editor can do, by name, from a key, a menu or the command palette (ADR-0112).
struct Command {
    /// Stable, as the keymap file names it: "file.save".
    std::string id;
    /// What the palette and the keymap show: "Save".
    std::string name;
    /// The key it has unless the keymap gives it another, or none.
    KeyChord defaultChord;
    std::function<void()> run;
    /// Whether it can run now; always when not given.
    std::function<bool()> enabled;
    /// Pressing the chord held down runs it again (undo, moving a cut).
    bool repeats = false;
};

/// The keys given to commands other than their own, by command id: an empty chord takes a command's key away.
using Keymap = std::map<std::string, KeyChord>;

/// The keymap as text, a line "id = chord" each, and back. Unknown lines are kept out.
[[nodiscard]] std::string WriteKeymap(const Keymap& keymap);
[[nodiscard]] Keymap ParseKeymap(std::string_view text);

/// The chord a command answers to: the keymap's, or its own.
[[nodiscard]] KeyChord ChordOf(const Command& command, const Keymap& keymap);

/// The commands that share a chord with another, by chord text, for the keymap to warn about.
[[nodiscard]] std::map<std::string, std::vector<std::string>> Clashes(const std::vector<Command>& commands,
                                                                      const Keymap& keymap);

/// How well text matches a query typed fast: every character of the query in order, ignoring case, better when
/// they are together, at the start of words, or early. Nothing when they are not all there.
[[nodiscard]] std::optional<int> FuzzyScore(std::string_view text, std::string_view query);

} // namespace editor
