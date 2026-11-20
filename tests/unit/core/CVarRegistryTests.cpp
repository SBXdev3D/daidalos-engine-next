// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/core/Assert.hpp>
#include <daidalos/core/CVarRegistry.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace daidalos::core;

namespace {

int g_assertCount = 0;

void CountingAssertHandler(const AssertInfo&) {
    ++g_assertCount;
}

} // namespace

TEST(CVarRegistry, RegisteredVariableHasItsDefaultValue) {
    CVarRegistry registry;

    const CVar<std::int64_t> width = registry.Register<std::int64_t>("window.width", 1280, "Window width");

    EXPECT_EQ(width.Get(), 1280);
    EXPECT_EQ(width.Name(), "window.width");
    const CVarEntry* entry = registry.Find("window.width");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->description, "Window width");
    EXPECT_EQ(entry->defaultValue, CVarValue(std::int64_t{1280}));
}

TEST(CVarRegistry, SetParsesTextAccordingToType) {
    CVarRegistry registry;
    const CVar<bool> flag = registry.Register<bool>("test.flag", false, "");
    const CVar<std::int64_t> count = registry.Register<std::int64_t>("test.count", 0, "");
    const CVar<double> scale = registry.Register<double>("test.scale", 1.0, "");
    const CVar<std::string> name = registry.Register<std::string>("test.name", "default", "");

    ASSERT_TRUE(registry.Set("test.flag", "true").has_value());
    ASSERT_TRUE(registry.Set("test.count", "-42").has_value());
    ASSERT_TRUE(registry.Set("test.scale", "0.5").has_value());
    ASSERT_TRUE(registry.Set("test.name", "Main Hall").has_value());

    EXPECT_TRUE(flag.Get());
    EXPECT_EQ(count.Get(), -42);
    EXPECT_DOUBLE_EQ(scale.Get(), 0.5);
    EXPECT_EQ(name.Get(), "Main Hall");
}

TEST(CVarRegistry, BooleansAcceptOneAndZero) {
    CVarRegistry registry;
    const CVar<bool> flag = registry.Register<bool>("test.flag", false, "");

    ASSERT_TRUE(registry.Set("test.flag", "1").has_value());
    EXPECT_TRUE(flag.Get());
    ASSERT_TRUE(registry.Set("test.flag", "0").has_value());
    EXPECT_FALSE(flag.Get());
}

TEST(CVarRegistry, InvalidValueFailsAndKeepsCurrentValue) {
    CVarRegistry registry;
    const CVar<bool> flag = registry.Register<bool>("test.flag", true, "");
    const CVar<std::int64_t> count = registry.Register<std::int64_t>("test.count", 7, "");
    const CVar<double> scale = registry.Register<double>("test.scale", 1.0, "");

    for (const char* text : {"yes", "TRUE", ""}) {
        EXPECT_EQ(registry.Set("test.flag", text).error().code, ErrorCode::ParseError) << text;
    }
    for (const char* text : {"abc", "1.5", "12abc", "99999999999999999999", ""}) {
        EXPECT_EQ(registry.Set("test.count", text).error().code, ErrorCode::ParseError) << text;
    }
    for (const char* text : {"abc", "nan", "inf", "1e999"}) {
        EXPECT_EQ(registry.Set("test.scale", text).error().code, ErrorCode::ParseError) << text;
    }

    EXPECT_TRUE(flag.Get());
    EXPECT_EQ(count.Get(), 7);
    EXPECT_DOUBLE_EQ(scale.Get(), 1.0);
}

TEST(CVarRegistry, InvalidValueErrorNamesTheVariableAndExpectedType) {
    CVarRegistry registry;
    (void)registry.Register<std::int64_t>("window.width", 1280, "");

    const Result<void> result = registry.Set("window.width", "wide");

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().message, "Invalid value 'wide' for 'window.width': expected an integer");
}

TEST(CVarRegistry, InvalidNameFails) {
    CVarRegistry registry;

    for (const char* name : {"", "Window.Width", "window width", "window-width"}) {
        EXPECT_EQ(registry.Set(name, "1").error().code, ErrorCode::InvalidArgument) << name;
    }
}

TEST(CVarRegistry, ValueSetBeforeRegistrationIsAppliedOnRegister) {
    CVarRegistry registry;
    ASSERT_TRUE(registry.Set("window.width", "800").has_value());
    EXPECT_EQ(registry.UnregisteredNames(), std::vector<std::string>{"window.width"});

    const CVar<std::int64_t> width = registry.Register<std::int64_t>("window.width", 1280, "");

    EXPECT_EQ(width.Get(), 800);
    EXPECT_TRUE(registry.UnregisteredNames().empty());
    EXPECT_TRUE(registry.TakeErrors().empty());
}

TEST(CVarRegistry, InvalidEarlierValueKeepsDefaultAndIsReported) {
    CVarRegistry registry;
    ASSERT_TRUE(registry.Set("window.width", "wide").has_value());

    const CVar<std::int64_t> width = registry.Register<std::int64_t>("window.width", 1280, "");

    EXPECT_EQ(width.Get(), 1280);
    const std::vector<Error> errors = registry.TakeErrors();
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_EQ(errors[0].code, ErrorCode::ParseError);
    EXPECT_TRUE(registry.TakeErrors().empty()) << "TakeErrors clears the list";
}

TEST(CVarRegistry, LastValueSetBeforeRegistrationWins) {
    CVarRegistry registry;
    ASSERT_TRUE(registry.Set("test.count", "1").has_value());
    ASSERT_TRUE(registry.Set("test.count", "2").has_value());

    EXPECT_EQ(registry.Register<std::int64_t>("test.count", 0, "").Get(), 2);
}

TEST(CVarRegistry, ForEachVisitsVariablesInNameOrder) {
    CVarRegistry registry;
    (void)registry.Register<bool>("b.second", false, "");
    (void)registry.Register<bool>("a.first", false, "");
    (void)registry.Register<bool>("c.third", false, "");

    std::vector<std::string> names;
    registry.ForEach([&](const CVarEntry& entry) { names.push_back(entry.name); });

    EXPECT_EQ(names, (std::vector<std::string>{"a.first", "b.second", "c.third"}));
}

TEST(CVarRegistry, FindReturnsNullForUnknownName) {
    CVarRegistry registry;
    EXPECT_EQ(registry.Find("missing"), nullptr);
}

TEST(CVarRegistry, HandlesStayValidWhenManyVariablesAreAdded) {
    CVarRegistry registry;
    const CVar<std::int64_t> first = registry.Register<std::int64_t>("first", 1, "");

    for (int i = 0; i < 1000; ++i) {
        (void)registry.Register<std::int64_t>("var." + std::to_string(i), i, "");
    }

    EXPECT_EQ(first.Get(), 1);
}

TEST(CVarRegistry, RegisteringTwiceAsserts) {
    if (!DAI_ASSERTS_ENABLED) {
        GTEST_SKIP() << "Assertions are disabled in this configuration";
    }
    CVarRegistry registry;
    (void)registry.Register<bool>("test.flag", true, "");
    g_assertCount = 0;
    const AssertHandler previous = SetAssertHandler(&CountingAssertHandler);

    const CVar<bool> second = registry.Register<bool>("test.flag", false, "");

    SetAssertHandler(previous);
    EXPECT_EQ(g_assertCount, 1);
    EXPECT_TRUE(second.Get()) << "The first registration is kept";
}

TEST(CVarValueToString, FormatsLikeConfigFiles) {
    EXPECT_EQ(ToString(CVarValue(true)), "true");
    EXPECT_EQ(ToString(CVarValue(false)), "false");
    EXPECT_EQ(ToString(CVarValue(std::int64_t{-3})), "-3");
    EXPECT_EQ(ToString(CVarValue(0.25)), "0.25");
    EXPECT_EQ(ToString(CVarValue(std::string("text"))), "text");
}

TEST(CVarConfig, AppliesNameValueLines) {
    CVarRegistry registry;
    const CVar<std::int64_t> width = registry.Register<std::int64_t>("window.width", 0, "");
    const CVar<std::string> title = registry.Register<std::string>("window.title", "", "");
    const CVar<bool> resizable = registry.Register<bool>("window.resizable", false, "");

    const Result<void> result = registry.ParseConfig("# Window settings\r\n"
                                                     "\r\n"
                                                     "window.width   800   # pixels\r\n"
                                                     "  window.title \"Main # Hall\"  # quoted\r\n"
                                                     "window.resizable\ttrue",
                                                     "test.cfg");

    ASSERT_TRUE(result.has_value()) << Format(result.error());
    EXPECT_EQ(width.Get(), 800);
    EXPECT_EQ(title.Get(), "Main # Hall");
    EXPECT_TRUE(resizable.Get());
}

TEST(CVarConfig, QuotedEmptyStringIsAValue) {
    CVarRegistry registry;
    const CVar<std::string> title = registry.Register<std::string>("window.title", "default", "");

    ASSERT_TRUE(registry.ParseConfig("window.title \"\"", "test.cfg").has_value());

    EXPECT_EQ(title.Get(), "");
}

TEST(CVarConfig, UnknownNamesAreKeptForLaterRegistration) {
    CVarRegistry registry;

    ASSERT_TRUE(registry.ParseConfig("game.difficulty 3\n", "test.cfg").has_value());

    EXPECT_EQ(registry.UnregisteredNames(), std::vector<std::string>{"game.difficulty"});
    EXPECT_EQ(registry.Register<std::int64_t>("game.difficulty", 1, "").Get(), 3);
}

TEST(CVarConfig, ErrorsReportSourceAndLineNumber) {
    struct Case {
        const char* text;
        const char* message;
    };
    const Case cases[] = {
        {"window.width 800\nwindow.height\n", "test.cfg:2: missing value for 'window.height'"},
        {"\n\nwindow.title \"Hall\n", "test.cfg:3: missing closing quote"},
        {"window.title \"Hall\" extra\n", "test.cfg:1: unexpected text after the closing quote"},
        {"window.width wide\n", "test.cfg:1: Invalid value 'wide' for 'window.width': expected an integer"},
        {"Window.Width 1\n",
         "test.cfg:1: Invalid cvar name 'Window.Width': use lowercase letters, digits, '_' and '.'"},
    };

    for (const Case& c : cases) {
        CVarRegistry registry;
        (void)registry.Register<std::int64_t>("window.width", 0, "");

        const Result<void> result = registry.ParseConfig(c.text, "test.cfg");

        ASSERT_FALSE(result.has_value()) << c.text;
        EXPECT_EQ(result.error().code, ErrorCode::ParseError) << c.text;
        EXPECT_EQ(result.error().message, c.message);
    }
}

TEST(CVarConfig, MissingFileFailsWithNotFound) {
    CVarRegistry registry;

    const Result<void> result =
        registry.LoadConfigFile(std::filesystem::temp_directory_path() / "daidalos_missing.cfg");

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::NotFound);
}

TEST(CVarConfig, LoadsFile) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "daidalos_cvar_test.cfg";
    {
        std::ofstream file(path, std::ios::binary);
        file << "window.width 640\n";
    }
    CVarRegistry registry;
    const CVar<std::int64_t> width = registry.Register<std::int64_t>("window.width", 0, "");

    const Result<void> result = registry.LoadConfigFile(path);
    std::filesystem::remove(path);

    ASSERT_TRUE(result.has_value()) << Format(result.error());
    EXPECT_EQ(width.Get(), 640);
}

TEST(CVarCommandLine, AppliesPlusNameValuePairsAndIgnoresOtherArguments) {
    CVarRegistry registry;
    const CVar<std::int64_t> width = registry.Register<std::int64_t>("window.width", 0, "");
    const CVar<std::string> title = registry.Register<std::string>("window.title", "", "");
    const std::vector<std::string> arguments = {"--verbose",  "+window.width", "1024",
                                                "level.gltf", "+window.title", "Two words"};

    const Result<void> result = registry.ApplyCommandLine(arguments);

    ASSERT_TRUE(result.has_value()) << Format(result.error());
    EXPECT_EQ(width.Get(), 1024);
    EXPECT_EQ(title.Get(), "Two words");
}

TEST(CVarCommandLine, MissingValueFails) {
    CVarRegistry registry;
    const std::vector<std::string> arguments = {"+window.width"};

    const Result<void> result = registry.ApplyCommandLine(arguments);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::InvalidArgument);
}

TEST(CVarCommandLine, InvalidValueFails) {
    CVarRegistry registry;
    (void)registry.Register<std::int64_t>("window.width", 0, "");
    const std::vector<std::string> arguments = {"+window.width", "wide"};

    EXPECT_EQ(registry.ApplyCommandLine(arguments).error().code, ErrorCode::ParseError);
}

TEST(CVarCommandLine, LonePlusIsIgnored) {
    CVarRegistry registry;
    const std::vector<std::string> arguments = {"+"};

    EXPECT_TRUE(registry.ApplyCommandLine(arguments).has_value());
    EXPECT_TRUE(registry.UnregisteredNames().empty());
}

TEST(CVarConfig, LinesKeepNamesValuesAndNumbers) {
    const Result<std::vector<ConfigLine>> lines =
        ParseConfigLines("# settings\nview.fov 75\n\n  window.title \"A # B\" # quoted\n", "user.cfg");
    ASSERT_TRUE(lines.has_value()) << Format(lines.error());
    ASSERT_EQ(lines->size(), 2u);
    EXPECT_EQ((*lines)[0].name, "view.fov");
    EXPECT_EQ((*lines)[0].value, "75");
    EXPECT_EQ((*lines)[0].number, 2u);
    EXPECT_EQ((*lines)[1].name, "window.title");
    EXPECT_EQ((*lines)[1].value, "A # B");
    EXPECT_EQ((*lines)[1].number, 4u);
}

TEST(CVarConfig, InvalidTextAppliesNothing) {
    CVarRegistry registry;
    const CVar<std::int64_t> width = registry.Register<std::int64_t>("window.width", 1280, "");
    EXPECT_FALSE(registry.ParseConfig("window.width 800\nwindow.title \"open\n", "test.cfg").has_value());
    EXPECT_EQ(width.Get(), 1280);
}

TEST(CVarConfig, FormattedLinesReadBack) {
    for (const std::string_view value : {"75", "", "two words", "a#b", "msaa4", "0.0025"}) {
        const std::string line = FormatConfigLine("some.name", value);
        const Result<std::vector<ConfigLine>> lines = ParseConfigLines(line, "test");
        ASSERT_TRUE(lines.has_value()) << line;
        ASSERT_EQ(lines->size(), 1u) << line;
        EXPECT_EQ((*lines)[0].name, "some.name");
        EXPECT_EQ((*lines)[0].value, value) << line;
    }
    EXPECT_EQ(FormatConfigLine("view.fov", "90"), "view.fov 90");
    EXPECT_EQ(FormatConfigLine("a.b", "x y"), "a.b \"x y\"");
}

TEST(CVarRegistry, KnowsWhetherSomethingSetAVariable) {
    CVarRegistry registry;
    ASSERT_TRUE(registry.Set("test.early", "3").has_value());
    const CVar<std::int64_t> early = registry.Register<std::int64_t>("test.early", 1, "");
    const CVar<std::int64_t> late = registry.Register<std::int64_t>("test.late", 1, "");
    const CVar<std::int64_t> invalid = registry.Register<std::int64_t>("test.invalid", 1, "");

    EXPECT_TRUE(early.IsSet()) << "A value kept until registration counts";
    EXPECT_FALSE(late.IsSet());
    EXPECT_FALSE(registry.Set("test.invalid", "many").has_value());
    EXPECT_FALSE(invalid.IsSet()) << "A value that does not parse sets nothing";
    ASSERT_TRUE(registry.Set("test.late", "1").has_value());
    EXPECT_TRUE(late.IsSet()) << "Even to its default value";

    registry.Reset("test.early");
    EXPECT_FALSE(early.IsSet());
    EXPECT_EQ(early.Get(), 1);
}

TEST(CVarRegistry, ResetForgetsAValueKeptForAnUnregisteredName) {
    CVarRegistry registry;
    ASSERT_TRUE(registry.Set("test.pending", "3").has_value());
    registry.Reset("test.pending");
    const CVar<std::int64_t> pending = registry.Register<std::int64_t>("test.pending", 1, "");
    EXPECT_FALSE(pending.IsSet());
    EXPECT_EQ(pending.Get(), 1);
    EXPECT_TRUE(registry.UnregisteredNames().empty());
}

TEST(CVarRegistry, ObtainGivesBackWhatIsRegistered) {
    daidalos::core::CVarRegistry registry;
    const auto first = registry.Obtain<std::int64_t>("game.lives", 3, "Lives");
    ASSERT_TRUE(registry.Set("game.lives", "5").has_value());
    // A second start of the same code finds the variable and its value, without registering it twice.
    const auto second = registry.Obtain<std::int64_t>("game.lives", 3, "Lives");
    EXPECT_EQ(second.Get(), 5);
    EXPECT_EQ(first.Get(), 5);
}
