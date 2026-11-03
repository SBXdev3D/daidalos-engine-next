// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/logging/Log.hpp>

#include <spdlog/logger.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <atomic>
#include <exception>
#include <memory>
#include <mutex>
#include <string>

namespace daidalos::logging {

namespace {

constexpr std::string_view kModule = "logging";

struct LogState {
    std::shared_ptr<spdlog::logger> logger; // Console and file outputs; null when both are disabled.
    std::vector<ISink*> sinks;
};

// g_mutex guards g_state. g_minimumLevel is read without locking so that disabled levels cost one atomic load.
std::mutex g_mutex;
std::unique_ptr<LogState> g_state;
std::atomic<Level> g_minimumLevel{Level::Off};

spdlog::level::level_enum ToSpdlog(Level level) noexcept {
    switch (level) {
    case Level::Trace:
        return spdlog::level::trace;
    case Level::Debug:
        return spdlog::level::debug;
    case Level::Info:
        return spdlog::level::info;
    case Level::Warning:
        return spdlog::level::warn;
    case Level::Error:
        return spdlog::level::err;
    case Level::Critical:
        return spdlog::level::critical;
    case Level::Off:
        return spdlog::level::off;
    }
    return spdlog::level::off;
}

} // namespace

std::string_view ToString(Level level) noexcept {
    switch (level) {
    case Level::Trace:
        return "Trace";
    case Level::Debug:
        return "Debug";
    case Level::Info:
        return "Info";
    case Level::Warning:
        return "Warning";
    case Level::Error:
        return "Error";
    case Level::Critical:
        return "Critical";
    case Level::Off:
        return "Off";
    }
    return "Off";
}

core::Result<void> Initialize(const LogConfig& config) {
    const std::scoped_lock lock(g_mutex);
    if (g_state) {
        return core::MakeError(core::ErrorCode::InvalidState, kModule, "Logging is already initialized");
    }

    auto state = std::make_unique<LogState>();
    state->sinks = config.sinks;

    // spdlog reports failures with exceptions; they must not leave this module (ADR-0003).
    try {
        std::vector<spdlog::sink_ptr> outputs;
        if (config.console) {
            outputs.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
        }
        if (!config.filePath.empty()) {
            outputs.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(config.filePath.string(), true));
        }
        if (!outputs.empty()) {
            // The logger is not registered in spdlog's global registry: this module owns it.
            state->logger = std::make_shared<spdlog::logger>("daidalos", outputs.begin(), outputs.end());
            state->logger->set_pattern("%Y-%m-%d %H:%M:%S.%e [%^%l%$] %v");
            state->logger->set_level(spdlog::level::trace); // Filtering is done by IsEnabled().
            state->logger->flush_on(spdlog::level::warn);
        }
    } catch (const std::exception& exception) {
        return core::MakeError(core::ErrorCode::IoError, kModule,
                               "Cannot open log output '" + config.filePath.string() + "': " + exception.what());
    }

    g_state = std::move(state);
    g_minimumLevel.store(config.minimumLevel);
    return {};
}

void Shutdown() noexcept {
    const std::scoped_lock lock(g_mutex);
    g_minimumLevel.store(Level::Off);
    if (g_state && g_state->logger) {
        try {
            g_state->logger->flush();
        } catch (...) {
            // Nothing can be reported: the logger itself failed.
        }
    }
    g_state.reset();
}

bool IsEnabled(Level level) noexcept {
    const Level minimum = g_minimumLevel.load(std::memory_order_relaxed);
    return level != Level::Off && minimum != Level::Off && level >= minimum;
}

void Write(Level level, std::string_view module, std::string_view message) {
    if (!IsEnabled(level)) {
        return;
    }

    const std::scoped_lock lock(g_mutex);
    if (!g_state) {
        return;
    }
    if (g_state->logger) {
        g_state->logger->log(ToSpdlog(level), "[{}] {}", module, message);
    }
    for (ISink* sink : g_state->sinks) {
        sink->Write(level, module, message);
    }
}

} // namespace daidalos::logging
