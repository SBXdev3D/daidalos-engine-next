// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <daidalos/core/CVarRegistry.hpp>
#include <daidalos/logging/Log.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace daidalos::app {

/// What the console shows: the log's messages and the output of commands, the most recent ones only
/// (ADR-0030). A log sink, so messages come from any thread.
class ConsoleLog final : public logging::ISink {
public:
    struct Line {
        logging::Level level = logging::Level::Info;
        std::string text;
    };

    explicit ConsoleLog(std::size_t capacity = 2000);

    /// "[module] message", from the logging thread that wrote it.
    void Write(logging::Level level, std::string_view module, std::string_view message) override;
    void Print(logging::Level level, std::string text);
    void Clear();

    /// Oldest first.
    [[nodiscard]] std::vector<Line> Lines() const;
    /// Changes whenever a line is added or the log is cleared, so a reader copies the lines only then.
    [[nodiscard]] std::uint64_t Revision() const;

private:
    mutable std::mutex m_mutex;
    std::size_t m_capacity;
    std::deque<Line> m_lines;
    std::uint64_t m_revision = 0;
};

/// The developer console's language (ADR-0030): a line is a command and its arguments, or the name of
/// a cvar to show it, or a cvar's name and a value to set it for this run. Values with spaces go in
/// double quotes. Built in: help, cvars [filter], clear.
class Console {
public:
    using Command = std::function<void(Console& console, std::span<const std::string> arguments)>;

    /// The registry and the log must outlive the console.
    Console(core::CVarRegistry& cvars, ConsoleLog& log);

    /// A command named like a cvar hides it. Names follow cvar names: lowercase, digits, '_' and '.'.
    void AddCommand(std::string name, std::string description, Command run);

    /// Runs a line and keeps it in the history. Output and errors go to the log.
    void Execute(std::string_view line);

    /// Completes the line's first word with the commands and cvars it starts: up to their longest
    /// common prefix, with a space after a single match. Several matches are also printed.
    [[nodiscard]] std::string Complete(std::string_view line);

    /// Lines run, oldest first, without repeats of the previous one.
    [[nodiscard]] std::span<const std::string> History() const { return m_history; }

    void Print(std::string text, logging::Level level = logging::Level::Info);
    [[nodiscard]] ConsoleLog& Log() { return *m_log; }
    [[nodiscard]] core::CVarRegistry& CVars() { return *m_cvars; }

private:
    struct Entry {
        std::string description;
        Command run;
    };

    void ShowCVar(const core::CVarEntry& entry);

    core::CVarRegistry* m_cvars;
    ConsoleLog* m_log;
    std::map<std::string, Entry, std::less<>> m_commands;
    std::vector<std::string> m_history;
};

/// Splits a console line into words, at spaces and tabs; double quotes keep spaces in a word.
[[nodiscard]] std::vector<std::string> SplitConsoleLine(std::string_view line);

} // namespace daidalos::app
