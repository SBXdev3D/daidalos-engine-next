// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/logging/Log.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace daidalos;
using logging::Level;

namespace {

class CaptureSink final : public logging::ISink {
public:
    struct Entry {
        Level level;
        std::string module;
        std::string message;
    };

    void Write(Level level, std::string_view module, std::string_view message) override {
        entries.push_back({level, std::string(module), std::string(message)});
    }

    std::vector<Entry> entries;
};

logging::LogConfig SinkOnlyConfig(CaptureSink& sink, Level minimumLevel = Level::Trace) {
    logging::LogConfig config;
    config.minimumLevel = minimumLevel;
    config.console = false;
    config.sinks = {&sink};
    return config;
}

class LogTest : public testing::Test {
protected:
    void TearDown() override { logging::Shutdown(); }

    CaptureSink m_sink;
};

} // namespace

TEST(LogLevel, NamesMatchEnumerators) {
    EXPECT_EQ(logging::ToString(Level::Trace), "Trace");
    EXPECT_EQ(logging::ToString(Level::Warning), "Warning");
    EXPECT_EQ(logging::ToString(Level::Off), "Off");
}

TEST_F(LogTest, NothingIsEnabledBeforeInitialize) {
    EXPECT_FALSE(logging::IsEnabled(Level::Critical));
    logging::Write(Level::Critical, "test", "Dropped"); // Must not crash.
}

TEST_F(LogTest, MessageReachesSinkWithLevelAndModule) {
    ASSERT_TRUE(logging::Initialize(SinkOnlyConfig(m_sink)).has_value());

    logging::Write(Level::Info, "renderer", "Device created");

    ASSERT_EQ(m_sink.entries.size(), 1u);
    EXPECT_EQ(m_sink.entries[0].level, Level::Info);
    EXPECT_EQ(m_sink.entries[0].module, "renderer");
    EXPECT_EQ(m_sink.entries[0].message, "Device created");
}

TEST_F(LogTest, LogFormatsArguments) {
    ASSERT_TRUE(logging::Initialize(SinkOnlyConfig(m_sink)).has_value());

    logging::Log(Level::Debug, "test", "{} x {} = {}", 6, 7, 42);

    ASSERT_EQ(m_sink.entries.size(), 1u);
    EXPECT_EQ(m_sink.entries[0].message, "6 x 7 = 42");
}

TEST_F(LogTest, MacrosUseTheirLevel) {
    ASSERT_TRUE(logging::Initialize(SinkOnlyConfig(m_sink)).has_value());

    DAI_LOG_TRACE("test", "trace");
    DAI_LOG_DEBUG("test", "debug");
    DAI_LOG_INFO("test", "info");
    DAI_LOG_WARNING("test", "warning {}", 1);
    DAI_LOG_ERROR("test", "error");
    DAI_LOG_CRITICAL("test", "critical");

    const std::vector<Level> expected = {Level::Trace,   Level::Debug, Level::Info,
                                         Level::Warning, Level::Error, Level::Critical};
    ASSERT_EQ(m_sink.entries.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(m_sink.entries[i].level, expected[i]) << "Entry " << i;
    }
    EXPECT_EQ(m_sink.entries[3].message, "warning 1");
}

TEST_F(LogTest, MessagesBelowMinimumLevelAreFiltered) {
    ASSERT_TRUE(logging::Initialize(SinkOnlyConfig(m_sink, Level::Warning)).has_value());

    EXPECT_FALSE(logging::IsEnabled(Level::Info));
    EXPECT_TRUE(logging::IsEnabled(Level::Warning));
    EXPECT_TRUE(logging::IsEnabled(Level::Error));

    DAI_LOG_INFO("test", "filtered");
    DAI_LOG_ERROR("test", "kept");

    ASSERT_EQ(m_sink.entries.size(), 1u);
    EXPECT_EQ(m_sink.entries[0].message, "kept");
}

TEST_F(LogTest, LevelOffDisablesEverything) {
    ASSERT_TRUE(logging::Initialize(SinkOnlyConfig(m_sink, Level::Off)).has_value());

    DAI_LOG_CRITICAL("test", "filtered");

    EXPECT_TRUE(m_sink.entries.empty());
}

TEST_F(LogTest, SecondInitializeFailsWithInvalidState) {
    ASSERT_TRUE(logging::Initialize(SinkOnlyConfig(m_sink)).has_value());

    const core::Result<void> second = logging::Initialize(SinkOnlyConfig(m_sink));

    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error().code, core::ErrorCode::InvalidState);
    EXPECT_EQ(second.error().module, "logging");
}

TEST_F(LogTest, NothingIsWrittenAfterShutdown) {
    ASSERT_TRUE(logging::Initialize(SinkOnlyConfig(m_sink)).has_value());
    logging::Shutdown();

    EXPECT_FALSE(logging::IsEnabled(Level::Critical));
    DAI_LOG_CRITICAL("test", "dropped");

    EXPECT_TRUE(m_sink.entries.empty());
}

TEST_F(LogTest, CanInitializeAgainAfterShutdown) {
    ASSERT_TRUE(logging::Initialize(SinkOnlyConfig(m_sink)).has_value());
    logging::Shutdown();
    ASSERT_TRUE(logging::Initialize(SinkOnlyConfig(m_sink)).has_value());

    DAI_LOG_INFO("test", "second session");

    ASSERT_EQ(m_sink.entries.size(), 1u);
}

TEST_F(LogTest, ShutdownWithoutInitializeIsSafe) {
    logging::Shutdown();
    logging::Shutdown();
}

TEST_F(LogTest, FileOutputContainsLevelModuleAndMessage) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "daidalos_log_test.log";
    std::filesystem::remove(path);

    logging::LogConfig config;
    config.console = false;
    config.filePath = path;
    ASSERT_TRUE(logging::Initialize(config).has_value());
    DAI_LOG_INFO("test", "hello {}", 42);
    logging::Shutdown();

    std::ifstream file(path);
    ASSERT_TRUE(file.is_open());
    const std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    std::filesystem::remove(path);

    EXPECT_NE(contents.find("[info]"), std::string::npos) << contents;
    EXPECT_NE(contents.find("[test] hello 42"), std::string::npos) << contents;
}

TEST_F(LogTest, UnopenableFileFailsWithIoErrorAndLeavesLoggingStopped) {
    logging::LogConfig config;
    config.console = false;
    config.filePath = std::filesystem::temp_directory_path(); // A directory cannot be opened as a file.
    config.sinks = {&m_sink};

    const core::Result<void> result = logging::Initialize(config);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, core::ErrorCode::IoError);
    EXPECT_FALSE(logging::IsEnabled(Level::Critical));
    ASSERT_TRUE(logging::Initialize(SinkOnlyConfig(m_sink)).has_value())
        << "A failed Initialize must not block a retry";
}
