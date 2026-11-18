// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/scene/Sequence.hpp>
#include <daidalos/scene/Text.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>

using namespace daidalos;
using scene::TextTable;

namespace {

std::set<std::string> Keys(const std::filesystem::path& file) {
    std::ifstream stream(file);
    std::set<std::string> keys;
    const std::regex key(R"re(^\s*"([^"]+)"\s*:)re");
    std::string line;
    while (std::getline(stream, line)) {
        std::smatch match;
        if (std::regex_search(line, match, key)) {
            keys.insert(match[1].str());
        }
    }
    return keys;
}

} // namespace

TEST(TextTableTest, KeysGiveTheirStringsAndMissingOnesThemselves) {
    const core::Result<TextTable> table = TextTable::Parse(R"({"menu.play": "Play", "menu.quit": "Quit"})");
    ASSERT_TRUE(table.has_value()) << core::Format(table.error());
    EXPECT_EQ(table->Get("menu.play"), "Play");
    EXPECT_TRUE(table->Has("menu.quit"));
    EXPECT_EQ(table->Get("menu.missing"), "menu.missing");
    EXPECT_EQ(table->Size(), 2U);
}

TEST(TextTableTest, ATranslationGoesOverEnglishKeyByKey) {
    TextTable english = TextTable::Parse(R"({"a": "Yes", "b": "No"})").value();
    english.Overlay(TextTable::Parse(R"({"a": "Oui"})").value());
    EXPECT_EQ(english.Get("a"), "Oui");
    EXPECT_EQ(english.Get("b"), "No");
}

TEST(TextTableTest, WhatIsNotAnObjectOfStringsIsAnError) {
    EXPECT_FALSE(TextTable::Parse("[1, 2]").has_value());
    const core::Result<TextTable> number = TextTable::Parse(R"({"a": 3})", "en.json");
    ASSERT_FALSE(number.has_value());
    EXPECT_NE(number.error().message.find("'a'"), std::string::npos);
    EXPECT_FALSE(TextTable::Parse("{").has_value());
}

TEST(TextTableTest, EveryLanguageHasEveryKeyOfEnglishAndNoOther) {
    // ADR-0075: a translation that lacks a key shows English, which a player notices; this catches it first.
    const std::filesystem::path text = std::filesystem::path(DAI_PROJECT_ASSET_DIR) / "Text";
    const std::set<std::string> english = Keys(text / "en.json");
    ASSERT_GT(english.size(), 50U);
    const std::vector<std::string> languages = TextTable::Languages(DAI_PROJECT_ASSET_DIR);
    EXPECT_NE(std::find(languages.begin(), languages.end(), "fr"), languages.end());
    for (const std::string& language : languages) {
        const std::set<std::string> keys = Keys(text / (language + ".json"));
        for (const std::string& key : english) {
            EXPECT_TRUE(keys.contains(key)) << language << " lacks " << key;
        }
        for (const std::string& key : keys) {
            EXPECT_TRUE(english.contains(key)) << language << " has " << key << ", which English does not";
        }
        const TextTable table = TextTable::Load(DAI_PROJECT_ASSET_DIR, language);
        EXPECT_NE(table.Get("language.name"), "language.name") << language;
    }
}

TEST(TextTableTest, SubtitlesAndCaptionsParseFromSequences) {
    const core::Result<scene::Sequence> sequence = scene::ParseSequence(R"({"cues": [
        {"at": 1.0, "subtitle": {"speaker": "speaker.you", "text": "subtitle.a", "seconds": 2.0}},
        {"at": 0.5, "sound": {"clip": "a.wav", "caption": "caption.b"}}]})");
    ASSERT_TRUE(sequence.has_value()) << core::Format(sequence.error());
    ASSERT_EQ(sequence->cues.size(), 2U);
    const auto subtitle = std::find_if(sequence->cues.begin(), sequence->cues.end(),
                                       [](const scene::Cue& cue) { return cue.kind == scene::Cue::Kind::Subtitle; });
    ASSERT_NE(subtitle, sequence->cues.end());
    EXPECT_EQ(subtitle->caption, "subtitle.a");
    EXPECT_EQ(subtitle->name, "speaker.you");
    EXPECT_FLOAT_EQ(subtitle->seconds, 2.0f);
    const auto sound = std::find_if(sequence->cues.begin(), sequence->cues.end(),
                                    [](const scene::Cue& cue) { return cue.kind == scene::Cue::Kind::Sound; });
    ASSERT_NE(sound, sequence->cues.end());
    EXPECT_EQ(sound->caption, "caption.b");
    EXPECT_FALSE(scene::ParseSequence(R"({"cues": [{"at": 0, "subtitle": {"speaker": "x"}}]})").has_value());
}
