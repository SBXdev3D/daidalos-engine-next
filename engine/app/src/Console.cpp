// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/app/Console.hpp>

#include <algorithm>
#include <format>
#include <utility>

namespace daidalos::app {

ConsoleLog::ConsoleLog(std::size_t capacity) : m_capacity(std::max<std::size_t>(capacity, 1)) {}

void ConsoleLog::Write(logging::Level level, std::string_view module, std::string_view message) {
    Print(level, std::format("[{}] {}", module, message));
}

void ConsoleLog::Print(logging::Level level, std::string text) {
    const std::scoped_lock lock(m_mutex);
    if (m_lines.size() == m_capacity) {
        m_lines.pop_front();
    }
    m_lines.push_back({level, std::move(text)});
    ++m_revision;
}

void ConsoleLog::Clear() {
    const std::scoped_lock lock(m_mutex);
    m_lines.clear();
    ++m_revision;
}

std::vector<ConsoleLog::Line> ConsoleLog::Lines() const {
    const std::scoped_lock lock(m_mutex);
    return {m_lines.begin(), m_lines.end()};
}

std::uint64_t ConsoleLog::Revision() const {
    const std::scoped_lock lock(m_mutex);
    return m_revision;
}

std::vector<std::string> SplitConsoleLine(std::string_view line) {
    std::vector<std::string> words;
    std::string word;
    bool inWord = false;
    bool quoted = false;
    for (const char c : line) {
        if (c == '"') {
            quoted = !quoted;
            inWord = true;
        } else if (!quoted && (c == ' ' || c == '\t' || c == '\r' || c == '\n')) {
            if (inWord) {
                words.push_back(std::move(word));
                word.clear();
                inWord = false;
            }
        } else {
            word += c;
            inWord = true;
        }
    }
    if (inWord) {
        words.push_back(std::move(word));
    }
    return words;
}

Console::Console(core::CVarRegistry& cvars, ConsoleLog& log) : m_cvars(&cvars), m_log(&log) {
    AddCommand("help", "Lists the commands; cvars lists the settings",
               [](Console& console, std::span<const std::string> /*arguments*/) {
                   console.Print("Type a setting's name to see it, or its name and a value to change it until "
                                 "the game quits. Commands:");
                   for (const auto& [name, entry] : console.m_commands) {
                       console.Print(std::format("  {} - {}", name, entry.description));
                   }
               });
    AddCommand("cvars", "Lists the settings, or those whose name holds the filter: cvars [filter]",
               [](Console& console, std::span<const std::string> arguments) {
                   const std::string_view filter = arguments.empty() ? std::string_view{} : arguments[0];
                   std::size_t shown = 0;
                   console.CVars().ForEach([&](const core::CVarEntry& entry) {
                       if (entry.name.find(filter) != std::string::npos) {
                           console.ShowCVar(entry);
                           ++shown;
                       }
                   });
                   console.Print(std::format("{} settings", shown));
               });
    AddCommand("clear", "Empties the console",
               [](Console& console, std::span<const std::string> /*arguments*/) { console.Log().Clear(); });
}

void Console::AddCommand(std::string name, std::string description, Command run) {
    m_commands.insert_or_assign(std::move(name), Entry{std::move(description), std::move(run)});
}

void Console::Print(std::string text, logging::Level level) {
    m_log->Print(level, std::move(text));
}

void Console::ShowCVar(const core::CVarEntry& entry) {
    const std::string value = core::ToString(entry.value);
    const std::string fallback = core::ToString(entry.defaultValue);
    Print(value == fallback
              ? std::format("  {} = {}  ({})", entry.name, value, entry.description)
              : std::format("  {} = {}, default {}  ({})", entry.name, value, fallback, entry.description));
}

void Console::Execute(std::string_view line) {
    std::vector<std::string> words = SplitConsoleLine(line);
    if (words.empty()) {
        return;
    }
    const std::string trimmed(line.substr(line.find_first_not_of(" \t")));
    if (m_history.empty() || m_history.back() != trimmed) {
        m_history.push_back(trimmed);
    }
    Print(std::format("> {}", trimmed), logging::Level::Debug);

    const std::string name = std::move(words.front());
    words.erase(words.begin());
    if (const auto command = m_commands.find(name); command != m_commands.end()) {
        // Copied: a command may add or replace commands.
        const Command run = command->second.run;
        run(*this, words);
        return;
    }
    const core::CVarEntry* entry = m_cvars->Find(name);
    if (entry == nullptr) {
        Print(std::format("Unknown command or setting '{}'; type help", name), logging::Level::Warning);
        return;
    }
    if (words.empty()) {
        ShowCVar(*entry);
        return;
    }
    if (words.size() > 1) {
        Print(std::format("{} takes one value: put it in double quotes if it has spaces", name),
              logging::Level::Warning);
        return;
    }
    if (core::Result<void> set = m_cvars->Set(name, words.front()); !set) {
        Print(set.error().message, logging::Level::Warning);
        return;
    }
    ShowCVar(*entry);
}

std::string Console::Complete(std::string_view line) {
    const std::size_t start = line.find_first_not_of(" \t");
    if (start == std::string_view::npos || line.find_first_of(" \t", start) != std::string_view::npos) {
        return std::string(line); // Only the first word completes.
    }
    const std::string_view prefix = line.substr(start);
    std::vector<std::string> matches;
    for (const auto& [name, entry] : m_commands) {
        if (name.starts_with(prefix)) {
            matches.push_back(name);
        }
    }
    m_cvars->ForEach([&](const core::CVarEntry& entry) {
        if (entry.name.starts_with(prefix) && !m_commands.contains(entry.name)) {
            matches.push_back(entry.name);
        }
    });
    if (matches.empty()) {
        return std::string(line);
    }
    if (matches.size() == 1) {
        return matches.front() + ' ';
    }
    std::ranges::sort(matches);
    std::size_t common = matches.front().size();
    for (const std::string& match : matches) {
        common = std::min(common, static_cast<std::size_t>(std::ranges::mismatch(matches.front(), match).in1 -
                                                           matches.front().begin()));
    }
    std::string list;
    for (const std::string& match : matches) {
        list += list.empty() ? match : "   " + match;
    }
    Print(list);
    return matches.front().substr(0, common);
}

} // namespace daidalos::app
