// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>

#include <filesystem>
#include <format>
#include <string_view>
#include <utility>
#include <vector>

namespace daidalos::logging {

enum class Level {
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Critical,
    Off,
};

/// Returns the name of a level, for example "Warning".
[[nodiscard]] std::string_view ToString(Level level) noexcept;

/// Receives every message that passes the level filter, in addition to the console and file outputs.
/// Used by tests and, later, by the in-engine log console. Write may be called from several threads.
class ISink {
public:
    virtual ~ISink() = default;
    virtual void Write(Level level, std::string_view module, std::string_view message) = 0;
};

struct LogConfig {
    Level minimumLevel = Level::Info;
    bool console = true;
    std::filesystem::path filePath; ///< Empty: no log file. The file is overwritten at startup.
    std::vector<ISink*> sinks;      ///< Not owned. Each sink must stay alive until Shutdown().
};

/// Starts logging.
///
/// Logging is the one process-wide service in the engine: every module must be able to report
/// problems without a logger being passed through every constructor. It is still started and
/// stopped explicitly: call Initialize() first at startup and Shutdown() last.
/// Messages written before Initialize() or after Shutdown() are dropped.
///
/// Fails with InvalidState if logging is already initialized, or IoError if the log file cannot be opened.
[[nodiscard]] core::Result<void> Initialize(const LogConfig& config);

/// Flushes and stops logging. Safe to call when logging is not initialized.
void Shutdown() noexcept;

/// Writes what is buffered to the sinks now: for messages that must outlive a process killed from outside.
void Flush() noexcept;

/// True if a message of this level would be written.
[[nodiscard]] bool IsEnabled(Level level) noexcept;

/// Writes a message that is already formatted.
void Write(Level level, std::string_view module, std::string_view message);

/// Formats with std::format syntax and writes the message. Formatting is skipped when the level is disabled.
template <typename... Args>
void Log(Level level, std::string_view module, std::format_string<Args...> format, Args&&... args) {
    if (IsEnabled(level)) {
        Write(level, module, std::format(format, std::forward<Args>(args)...));
    }
}

} // namespace daidalos::logging

// Usage: DAI_LOG_INFO("renderer", "Swapchain created: {}x{}", width, height);
#define DAI_LOG_TRACE(module, ...) ::daidalos::logging::Log(::daidalos::logging::Level::Trace, module, __VA_ARGS__)
#define DAI_LOG_DEBUG(module, ...) ::daidalos::logging::Log(::daidalos::logging::Level::Debug, module, __VA_ARGS__)
#define DAI_LOG_INFO(module, ...) ::daidalos::logging::Log(::daidalos::logging::Level::Info, module, __VA_ARGS__)
#define DAI_LOG_WARNING(module, ...) ::daidalos::logging::Log(::daidalos::logging::Level::Warning, module, __VA_ARGS__)
#define DAI_LOG_ERROR(module, ...) ::daidalos::logging::Log(::daidalos::logging::Level::Error, module, __VA_ARGS__)
#define DAI_LOG_CRITICAL(module, ...)                                                                                  \
    ::daidalos::logging::Log(::daidalos::logging::Level::Critical, module, __VA_ARGS__)
