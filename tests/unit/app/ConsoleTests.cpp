// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/app/Console.hpp>

#include <gtest/gtest.h>

#include <string>
#include <thread>
#include <vector>

using namespace daidalos;
using app::Console;
using app::ConsoleLog;

namespace {

class ConsoleTest : public ::testing::Test {
protected:
    ConsoleTest()
        : m_fov(m_cvars.Register<double>("view.fov", 90.0, "Degrees across")),
          m_title(m_cvars.Register<std::string>("window.title", "Daidalos", "Title")),
          m_vsync(m_cvars.Register<bool>("renderer.vsync", true, "Vertical sync")), m_console(m_cvars, m_log) {}

    /// The text of the last line printed.
    [[nodiscard]] std::string Last() const {
        const std::vector<ConsoleLog::Line> lines = m_log.Lines();
        return lines.empty() ? std::string() : lines.back().text;
    }

    core::CVarRegistry m_cvars;
    core::CVar<double> m_fov;
    core::CVar<std::string> m_title;
    core::CVar<bool> m_vsync;
    ConsoleLog m_log;
    Console m_console;
};

} // namespace

TEST(ConsoleLine, SplitsAtSpacesAndKeepsQuotedOnesTogether) {
    EXPECT_EQ(app::SplitConsoleLine("  view.fov   75 "), (std::vector<std::string>{"view.fov", "75"}));
    EXPECT_EQ(app::SplitConsoleLine("window.title \"Main hall\""),
              (std::vector<std::string>{"window.title", "Main hall"}));
    EXPECT_EQ(app::SplitConsoleLine("set \"\""), (std::vector<std::string>{"set", ""}));
    EXPECT_TRUE(app::SplitConsoleLine("   ").empty());
}

TEST_F(ConsoleTest, NameAndValueSetTheCVarAndShowIt) {
    m_console.Execute("view.fov 75");
    EXPECT_DOUBLE_EQ(m_fov.Get(), 75.0);
    EXPECT_EQ(Last(), "  view.fov = 75, default 90  (Degrees across)");

    m_console.Execute("window.title \"Main hall\"");
    EXPECT_EQ(m_title.Get(), "Main hall");
}

TEST_F(ConsoleTest, NameAloneShowsTheCVar) {
    m_console.Execute("renderer.vsync");
    EXPECT_EQ(Last(), "  renderer.vsync = true  (Vertical sync)");
}

TEST_F(ConsoleTest, MistakesAreReportedAndChangeNothing) {
    m_console.Execute("view.fov wide");
    EXPECT_DOUBLE_EQ(m_fov.Get(), 90.0);
    EXPECT_EQ(m_log.Lines().back().level, logging::Level::Warning);

    m_console.Execute("view.fov 70 80");
    EXPECT_DOUBLE_EQ(m_fov.Get(), 90.0);

    m_console.Execute("nothing.here");
    EXPECT_EQ(Last(), "Unknown command or setting 'nothing.here'; type help");
}

TEST_F(ConsoleTest, CommandsGetTheirArguments) {
    std::vector<std::string> received;
    m_console.AddCommand("give", "Gives things", [&received](Console&, std::span<const std::string> arguments) {
        received.assign(arguments.begin(), arguments.end());
    });
    m_console.Execute("give shotgun \"7 shells\"");
    EXPECT_EQ(received, (std::vector<std::string>{"shotgun", "7 shells"}));
}

TEST_F(ConsoleTest, CVarsListsTheMatchingSettings) {
    m_console.Execute("cvars view");
    EXPECT_EQ(Last(), "1 settings");
}

TEST_F(ConsoleTest, ClearEmptiesTheLog) {
    m_console.Execute("view.fov");
    m_console.Execute("clear");
    EXPECT_TRUE(m_log.Lines().empty());
}

TEST_F(ConsoleTest, HistoryKeepsLinesWithoutRepeats) {
    m_console.Execute("view.fov 70");
    m_console.Execute("view.fov 70");
    m_console.Execute("  renderer.vsync");
    m_console.Execute("");
    ASSERT_EQ(m_console.History().size(), 2u);
    EXPECT_EQ(m_console.History()[0], "view.fov 70");
    EXPECT_EQ(m_console.History()[1], "renderer.vsync");
}

TEST_F(ConsoleTest, CompletionGoesAsFarAsTheMatchesAgree) {
    EXPECT_EQ(m_console.Complete("view"), "view.fov ");
    EXPECT_EQ(m_console.Complete("re"), "renderer.vsync ");
    m_console.AddCommand("restart", "Starts over", [](Console&, std::span<const std::string>) {});
    EXPECT_EQ(m_console.Complete("re"), "re");
    EXPECT_EQ(Last(), "renderer.vsync   restart");
    EXPECT_EQ(m_console.Complete("res"), "restart ");
    EXPECT_EQ(m_console.Complete("zzz"), "zzz");
    EXPECT_EQ(m_console.Complete("view.fov 7"), "view.fov 7") << "Only the first word completes";
}

TEST(ConsoleLogTest, KeepsTheMostRecentLinesFromAnyThread) {
    ConsoleLog log(100);
    std::vector<std::thread> threads;
    for (int thread = 0; thread < 4; ++thread) {
        threads.emplace_back([&log] {
            for (int line = 0; line < 100; ++line) {
                log.Write(logging::Level::Info, "test", "line");
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    const std::vector<ConsoleLog::Line> lines = log.Lines();
    ASSERT_EQ(lines.size(), 100u);
    EXPECT_EQ(lines.front().text, "[test] line");
    EXPECT_EQ(log.Revision(), 400u);
}
