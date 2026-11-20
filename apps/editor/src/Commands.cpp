// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "Commands.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace editor {

namespace {

std::string_view Trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    return text;
}

char Lower(char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

} // namespace

std::optional<KeyChord> ParseChord(std::string_view text) {
    text = Trim(text);
    if (text.empty()) {
        return KeyChord{};
    }
    KeyChord chord;
    // The key is the last part: "Ctrl++" is not a chord of this editor, so a '+' always separates.
    while (true) {
        const std::size_t plus = text.find('+');
        const std::string_view part = Trim(text.substr(0, plus));
        if (plus == std::string_view::npos) {
            if (part.empty()) {
                return std::nullopt;
            }
            chord.key = std::string(part);
            return chord;
        }
        if (part == "Ctrl") {
            chord.ctrl = true;
        } else if (part == "Shift") {
            chord.shift = true;
        } else if (part == "Alt") {
            chord.alt = true;
        } else {
            return std::nullopt;
        }
        text = text.substr(plus + 1);
    }
}

std::string FormatChord(const KeyChord& chord) {
    if (chord.Empty()) {
        return {};
    }
    std::string text;
    text += chord.ctrl ? "Ctrl+" : "";
    text += chord.shift ? "Shift+" : "";
    text += chord.alt ? "Alt+" : "";
    return text + chord.key;
}

std::string WriteKeymap(const Keymap& keymap) {
    std::string text = "# The editor's keys that differ from its own (ADR-0112): a command id, '=', and its keys,\n"
                       "# or nothing to take its key away. The Shortcuts window (F1) writes this file.\n";
    for (const auto& [id, chord] : keymap) {
        text += id + " = " + FormatChord(chord) + "\n";
    }
    return text;
}

Keymap ParseKeymap(std::string_view text) {
    Keymap keymap;
    std::istringstream lines{std::string(text)};
    std::string line;
    while (std::getline(lines, line)) {
        const std::string_view trimmed = Trim(line);
        const std::size_t equals = trimmed.find('=');
        if (trimmed.empty() || trimmed.front() == '#' || equals == std::string_view::npos) {
            continue;
        }
        const std::string_view id = Trim(trimmed.substr(0, equals));
        if (std::optional<KeyChord> chord = ParseChord(trimmed.substr(equals + 1)); chord && !id.empty()) {
            keymap[std::string(id)] = *chord;
        }
    }
    return keymap;
}

KeyChord ChordOf(const Command& command, const Keymap& keymap) {
    const auto found = keymap.find(command.id);
    return found != keymap.end() ? found->second : command.defaultChord;
}

std::map<std::string, std::vector<std::string>> Clashes(const std::vector<Command>& commands, const Keymap& keymap) {
    std::map<std::string, std::vector<std::string>> byChord;
    for (const Command& command : commands) {
        const KeyChord chord = ChordOf(command, keymap);
        if (!chord.Empty()) {
            byChord[FormatChord(chord)].push_back(command.id);
        }
    }
    std::erase_if(byChord, [](const auto& entry) { return entry.second.size() < 2; });
    return byChord;
}

std::optional<int> FuzzyScore(std::string_view text, std::string_view query) {
    if (query.empty()) {
        return 0;
    }
    // The query as one piece of the text counts most, more at a word's start: "crate" in "Props/crate.gltf".
    const auto lowered = [](std::string_view from) {
        std::string to(from);
        std::ranges::transform(to, to.begin(), Lower);
        return to;
    };
    const std::string lowerText = lowered(text);
    if (const std::size_t found = lowerText.find(lowered(query)); found != std::string::npos) {
        const bool wordStart = found == 0 || text[found - 1] == ' ' || text[found - 1] == '/' ||
                               text[found - 1] == '_' || text[found - 1] == '.';
        return 100 + (wordStart ? 20 : 0) - static_cast<int>(std::min<std::size_t>(found, 30)) / 10 -
               static_cast<int>(std::min<std::size_t>(text.size(), 60)) / 20;
    }
    int score = 0;
    std::size_t at = 0;
    std::optional<std::size_t> previous;
    for (const char wanted : query) {
        if (wanted == ' ') {
            continue;
        }
        while (at < text.size() && Lower(text[at]) != Lower(wanted)) {
            ++at;
        }
        if (at == text.size()) {
            return std::nullopt;
        }
        // Together counts most, then the start of a word, then early in the text.
        if (previous && *previous + 1 == at) {
            score += 8;
        }
        if (at == 0 || text[at - 1] == ' ' || text[at - 1] == '/' || text[at - 1] == '_' || text[at - 1] == '.') {
            score += 6;
        }
        score -= static_cast<int>(std::min<std::size_t>(at, 30)) / 10;
        previous = at;
        ++at;
    }
    // Shorter texts that match as well are nearer what was meant.
    return score - static_cast<int>(std::min<std::size_t>(text.size(), 60)) / 20;
}

} // namespace editor
