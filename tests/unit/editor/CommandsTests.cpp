// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <gtest/gtest.h>

#include "Commands.hpp"

using namespace editor;

TEST(CommandsTest, ChordsReadAndWriteAsTheyAreShown) {
    const std::optional<KeyChord> chord = ParseChord(" Ctrl + Shift+O ");
    ASSERT_TRUE(chord.has_value());
    EXPECT_TRUE(chord->ctrl);
    EXPECT_TRUE(chord->shift);
    EXPECT_FALSE(chord->alt);
    EXPECT_EQ(chord->key, "O");
    EXPECT_EQ(FormatChord(*chord), "Ctrl+Shift+O");
    EXPECT_EQ(FormatChord(*ParseChord("Alt+F5")), "Alt+F5");
    EXPECT_TRUE(ParseChord("")->Empty());
    EXPECT_FALSE(ParseChord("Super+K").has_value());
    EXPECT_FALSE(ParseChord("Ctrl+").has_value());
}

TEST(CommandsTest, TheKeymapKeepsWhatDiffers) {
    Keymap keymap;
    keymap["file.save"] = *ParseChord("Ctrl+Alt+S");
    keymap["view.grid"] = KeyChord{};
    const Keymap read = ParseKeymap(WriteKeymap(keymap) + "not a line\n# a comment\nbroken = Hyper+X\n");
    EXPECT_EQ(read, keymap);
    const Command save{.id = "file.save", .name = "Save", .defaultChord = *ParseChord("Ctrl+S")};
    const Command open{.id = "file.open", .name = "Open", .defaultChord = *ParseChord("Ctrl+O")};
    EXPECT_EQ(FormatChord(ChordOf(save, read)), "Ctrl+Alt+S");
    EXPECT_EQ(FormatChord(ChordOf(open, read)), "Ctrl+O");
}

TEST(CommandsTest, TwoCommandsOnOneChordClash) {
    const std::vector<Command> commands{
        Command{.id = "a", .defaultChord = *ParseChord("Ctrl+K")},
        Command{.id = "b", .defaultChord = *ParseChord("Ctrl+L")},
        Command{.id = "c", .defaultChord = KeyChord{}},
    };
    EXPECT_TRUE(Clashes(commands, {}).empty());
    Keymap keymap;
    keymap["b"] = *ParseChord("Ctrl+K");
    const auto clashes = Clashes(commands, keymap);
    ASSERT_EQ(clashes.size(), 1U);
    EXPECT_EQ(clashes.at("Ctrl+K"), (std::vector<std::string>{"a", "b"}));
}

TEST(CommandsTest, FuzzyMatchingPrefersTogetherAndWordStarts) {
    EXPECT_FALSE(FuzzyScore("Save", "sx").has_value());
    EXPECT_TRUE(FuzzyScore("Save as", "sva").has_value());
    // "op" is together at a word's start in "Open", scattered in "Drop to the floor".
    EXPECT_GT(*FuzzyScore("Open", "op"), *FuzzyScore("Drop to the floor", "op"));
    EXPECT_GT(*FuzzyScore("Factory/Props/crate.gltf", "crate"), *FuzzyScore("Factory/Props/c_r_a_t_e.gltf", "crate"));
    EXPECT_EQ(*FuzzyScore("anything", ""), 0);
}
