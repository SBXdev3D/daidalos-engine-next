// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <string>

#include "MapCheck.hpp"

using namespace daidalos;
using namespace editor;

namespace {

/// Files that exist, and one sequence naming an entity.
CheckSources Files(std::set<std::string> files) {
    return CheckSources{.exists = [files](const std::string& path) { return files.contains(path); },
                        .sequence = [](const std::string& /*path*/) -> std::optional<scene::Sequence> {
                            scene::Sequence sequence;
                            sequence.cues.push_back(scene::Cue{.kind = scene::Cue::Kind::Light, .name = "Ghost lamp"});
                            return sequence;
                        }};
}

bool Says(const std::vector<Problem>& problems, std::uint64_t entity, std::string_view words) {
    return std::ranges::any_of(problems, [&](const Problem& problem) {
        return problem.entity == entity && problem.message.find(words) != std::string::npos;
    });
}

scene::LevelEntity Player(std::uint64_t id) {
    scene::LevelEntity player{.id = id, .name = "Start"};
    player.game = scene::Tags{.kind = "player"};
    return player;
}

} // namespace

TEST(MapCheckTest, AGoodLevelHasNoProblems) {
    scene::Level level;
    level.entities.push_back(Player(1));
    level.entities.push_back(scene::LevelEntity{.id = 2, .name = "Crate", .model = "Props/crate.gltf"});
    EXPECT_TRUE(CheckLevel(level, "Factory/Factory.level.json", Files({"Factory/Props/crate.gltf"})).empty());
}

TEST(MapCheckTest, MissingFilesAreErrorsNamedUnderTheRoot) {
    scene::Level level;
    level.entities.push_back(Player(1));
    level.entities.push_back(scene::LevelEntity{.id = 2, .name = "Crate", .model = "../Props/crate.gltf"});
    const std::vector<Problem> problems = CheckLevel(level, "Factory/Factory.level.json", Files({}));
    ASSERT_FALSE(problems.empty());
    EXPECT_EQ(problems.front().severity, Problem::Severity::Error);
    EXPECT_TRUE(Says(problems, 2, "Props/crate.gltf"));
    EXPECT_EQ(UnderRoot("Factory/Factory.level.json", "../Props/crate.gltf"), "Props/crate.gltf");
}

TEST(MapCheckTest, BrokenLinksAreFound) {
    scene::Level level;
    level.entities.push_back(Player(1));
    level.entities.push_back(scene::LevelEntity{.id = 2, .name = "Lost", .parent = 99});
    level.entities.push_back(scene::LevelEntity{.id = 3, .name = "Twin"});
    level.entities.push_back(scene::LevelEntity{.id = 3, .name = "Twin again"});
    const std::vector<Problem> problems = CheckLevel(level, "Level.level.json", Files({}));
    EXPECT_TRUE(Says(problems, 2, "not in the level"));
    EXPECT_TRUE(Says(problems, 3, "more than one entity"));
}

TEST(MapCheckTest, TriggersAndTheirSequencesAreChecked) {
    scene::Level level;
    level.entities.push_back(Player(1));
    scene::LevelEntity silent{.id = 2, .name = "Silent"};
    silent.trigger = scene::Trigger{};
    level.entities.push_back(silent);
    scene::LevelEntity scare{.id = 3, .name = "Scare"};
    scare.trigger = scene::Trigger{.sequence = "Sequences/scare.sequence.json"};
    level.entities.push_back(scare);
    const std::vector<Problem> problems = CheckLevel(level, "L.level.json", Files({"Sequences/scare.sequence.json"}));
    EXPECT_TRUE(Says(problems, 2, "plays no sequence"));
    EXPECT_TRUE(Says(problems, 3, "\"Ghost lamp\""));
}

TEST(MapCheckTest, PlacementMistakesAreWarned) {
    scene::Level level;
    level.entities.push_back(Player(1));
    level.entities.push_back(Player(2));
    level.entities.push_back(scene::LevelEntity{.id = 3, .name = "Crate", .model = "crate.gltf"});
    level.entities.push_back(scene::LevelEntity{.id = 4, .name = "Crate copy", .model = "crate.gltf"});
    scene::LevelEntity flat{.id = 5, .name = "Flat"};
    flat.transform.scale.y = 0.0f;
    level.entities.push_back(flat);
    const std::vector<Problem> problems = CheckLevel(level, "L.level.json", Files({"crate.gltf"}));
    EXPECT_TRUE(Says(problems, 2, "More than one player start"));
    EXPECT_TRUE(Says(problems, 4, "lies exactly on"));
    EXPECT_TRUE(Says(problems, 5, "scale of 0"));
    EXPECT_TRUE(Says(CheckLevel(scene::Level{}, "L.level.json", Files({})), 0, "No player start"));
}

TEST(MapCheckTest, WithRoomsWhatLiesOutsideIsWarned) {
    scene::Level level;
    level.entities.push_back(Player(1));
    scene::LevelEntity room{.id = 2, .name = "Hall"};
    room.cell = scene::VisibilityCell{};
    room.transform.scale = glm::vec3(10.0f);
    level.entities.push_back(room);
    scene::LevelEntity inside{.id = 3, .name = "Inside", .model = "crate.gltf"};
    scene::LevelEntity outside{.id = 4, .name = "Outside", .model = "crate.gltf"};
    outside.transform.translation = glm::vec3(20.0f, 0.0f, 0.0f);
    level.entities.push_back(inside);
    level.entities.push_back(outside);
    const std::vector<Problem> problems = CheckLevel(level, "L.level.json", Files({"crate.gltf"}));
    EXPECT_FALSE(Says(problems, 3, "outside every room"));
    EXPECT_TRUE(Says(problems, 4, "outside every room"));
    EXPECT_EQ(problems.back().severity, Problem::Severity::Note);
}
