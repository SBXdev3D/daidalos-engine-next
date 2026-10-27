// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/app/StartupSequence.hpp>

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace daidalos;
using app::StartupSequence;

namespace {

// Fake subsystems that record their start and stop calls in a shared journal.
class Journal {
public:
    void AddStep(StartupSequence& sequence, const std::string& name, bool fails = false) {
        sequence.Add(
            name,
            [this, name, fails]() -> core::Result<void> {
                entries.push_back("start " + name);
                if (fails) {
                    return core::MakeError(core::ErrorCode::DeviceError, "test", name + " is broken");
                }
                return {};
            },
            [this, name] { entries.push_back("stop " + name); });
    }

    std::vector<std::string> entries;
};

using Entries = std::vector<std::string>;

} // namespace

TEST(StartupSequence, StartsInOrderAndStopsInReverseOrder) {
    Journal journal;
    StartupSequence sequence;
    journal.AddStep(sequence, "a");
    journal.AddStep(sequence, "b");
    journal.AddStep(sequence, "c");

    ASSERT_TRUE(sequence.Start().has_value());
    EXPECT_EQ(sequence.StartedCount(), 3u);
    sequence.Stop();

    EXPECT_EQ(journal.entries, (Entries{"start a", "start b", "start c", "stop c", "stop b", "stop a"}));
    EXPECT_EQ(sequence.StartedCount(), 0u);
}

TEST(StartupSequence, FailureStopsOnlyTheStepsAlreadyStarted) {
    Journal journal;
    StartupSequence sequence;
    journal.AddStep(sequence, "a");
    journal.AddStep(sequence, "b");
    journal.AddStep(sequence, "c", /*fails=*/true);
    journal.AddStep(sequence, "d");

    const core::Result<void> result = sequence.Start();

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(journal.entries, (Entries{"start a", "start b", "start c", "stop b", "stop a"}));
    EXPECT_EQ(sequence.StartedCount(), 0u);
}

TEST(StartupSequence, FailureOfFirstStepStopsNothing) {
    Journal journal;
    StartupSequence sequence;
    journal.AddStep(sequence, "a", /*fails=*/true);
    journal.AddStep(sequence, "b");

    ASSERT_FALSE(sequence.Start().has_value());

    EXPECT_EQ(journal.entries, (Entries{"start a"}));
}

TEST(StartupSequence, ErrorKeepsCodeAndModuleAndNamesTheStep) {
    Journal journal;
    StartupSequence sequence;
    journal.AddStep(sequence, "renderer", /*fails=*/true);

    const core::Result<void> result = sequence.Start();

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, core::ErrorCode::DeviceError);
    EXPECT_EQ(result.error().module, "test");
    EXPECT_EQ(result.error().message, "Starting renderer failed: renderer is broken");
}

TEST(StartupSequence, StopIsSafeToCallTwice) {
    Journal journal;
    StartupSequence sequence;
    journal.AddStep(sequence, "a");
    ASSERT_TRUE(sequence.Start().has_value());

    sequence.Stop();
    sequence.Stop();

    EXPECT_EQ(journal.entries, (Entries{"start a", "stop a"}));
}

TEST(StartupSequence, DestructorStopsStartedSteps) {
    Journal journal;
    {
        StartupSequence sequence;
        journal.AddStep(sequence, "a");
        journal.AddStep(sequence, "b");
        ASSERT_TRUE(sequence.Start().has_value());
    }

    EXPECT_EQ(journal.entries, (Entries{"start a", "start b", "stop b", "stop a"}));
}

TEST(StartupSequence, StepWithoutStopFunctionIsSkippedWhenStopping) {
    Journal journal;
    StartupSequence sequence;
    journal.AddStep(sequence, "a");
    sequence.Add("no stop", [&]() -> core::Result<void> {
        journal.entries.emplace_back("start no stop");
        return {};
    });

    ASSERT_TRUE(sequence.Start().has_value());
    sequence.Stop();

    EXPECT_EQ(journal.entries, (Entries{"start a", "start no stop", "stop a"}));
}

TEST(StartupSequence, CanStartAgainAfterStop) {
    Journal journal;
    StartupSequence sequence;
    journal.AddStep(sequence, "a");

    ASSERT_TRUE(sequence.Start().has_value());
    sequence.Stop();
    ASSERT_TRUE(sequence.Start().has_value());
    sequence.Stop();

    EXPECT_EQ(journal.entries, (Entries{"start a", "stop a", "start a", "stop a"}));
}

TEST(StartupSequence, EmptySequenceStarts) {
    StartupSequence sequence;
    EXPECT_TRUE(sequence.Start().has_value());
}
