// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/animation/Animation.hpp>
#include <daidalos/ecs/Transform.hpp>

#include <glm/gtc/quaternion.hpp>
#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <optional>
#include <vector>

using namespace daidalos;
using animation::Animator;
using resources::AnimationChannel;
using resources::AnimationData;
using resources::AnimationInterpolation;
using resources::AnimationPath;

namespace {

AnimationChannel Translation(std::uint32_t node, std::vector<float> times, std::vector<float> xs,
                             AnimationInterpolation interpolation = AnimationInterpolation::Linear) {
    AnimationChannel channel;
    channel.node = node;
    channel.path = AnimationPath::Translation;
    channel.interpolation = interpolation;
    channel.times = std::move(times);
    for (const float x : xs) {
        channel.values.emplace_back(x, 0.0f, 0.0f, 0.0f);
    }
    return channel;
}

AnimationChannel Turn(std::uint32_t node, float degrees, float seconds) {
    AnimationChannel channel;
    channel.node = node;
    channel.path = AnimationPath::Rotation;
    channel.times = {0.0f, seconds};
    const glm::quat end = glm::angleAxis(glm::radians(degrees), glm::vec3(0.0f, 1.0f, 0.0f));
    channel.values = {{0.0f, 0.0f, 0.0f, 1.0f}, {end.x, end.y, end.z, end.w}};
    return channel;
}

// Two nodes: "slide" moves node 0 from x = 0 to 10 in one second, "rise" moves node 1 from x = 0 to 4
// in two seconds. Node 1 rests at x = 7.
std::shared_ptr<const animation::ClipSet> TestClips() {
    std::vector<AnimationData> animations(2);
    animations[0].name = "slide";
    animations[0].channels.push_back(Translation(0, {0.0f, 1.0f}, {0.0f, 10.0f}));
    animations[1].name = "rise";
    animations[1].channels.push_back(Translation(1, {0.0f, 2.0f}, {0.0f, 4.0f}));
    std::vector<math::Transform> rest(2);
    rest[1].translation.x = 7.0f;
    return std::make_shared<const animation::ClipSet>(animation::MakeClipSet(animations, rest));
}

class AnimatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        for (ecs::Entity& node : m_nodes) {
            node = m_world.Create();
            m_world.Add<ecs::LocalTransform>(node);
        }
        m_animator.emplace(TestClips(), std::vector<ecs::Entity>(m_nodes.begin(), m_nodes.end()));
    }

    float X(std::size_t node) { return m_world.Get<ecs::LocalTransform>(m_nodes[node]).translation.x; }

    void Step(float seconds) {
        m_animator->Advance(seconds);
        m_animator->Apply(m_world);
    }

    ecs::World m_world;
    std::array<ecs::Entity, 2> m_nodes;
    std::optional<Animator> m_animator;
};

} // namespace

TEST(SampleChannel, ClampsToTheFirstAndLastKeys) {
    const AnimationChannel channel = Translation(0, {1.0f, 2.0f}, {3.0f, 5.0f});
    EXPECT_FLOAT_EQ(animation::SampleChannel(channel, 0.0f).x, 3.0f);
    EXPECT_FLOAT_EQ(animation::SampleChannel(channel, 9.0f).x, 5.0f);
}

TEST(SampleChannel, LinearKeysAreInterpolated) {
    const AnimationChannel channel = Translation(0, {0.0f, 1.0f, 3.0f}, {0.0f, 10.0f, 30.0f});
    EXPECT_FLOAT_EQ(animation::SampleChannel(channel, 0.5f).x, 5.0f);
    EXPECT_FLOAT_EQ(animation::SampleChannel(channel, 2.0f).x, 20.0f);
}

TEST(SampleChannel, StepKeysHoldUntilTheNextKey) {
    const AnimationChannel channel = Translation(0, {0.0f, 1.0f}, {0.0f, 10.0f}, AnimationInterpolation::Step);
    EXPECT_FLOAT_EQ(animation::SampleChannel(channel, 0.99f).x, 0.0f);
    EXPECT_FLOAT_EQ(animation::SampleChannel(channel, 1.0f).x, 10.0f);
}

TEST(SampleChannel, RotationsAreSphericallyInterpolated) {
    const glm::vec4 value = animation::SampleChannel(Turn(0, 90.0f, 1.0f), 0.5f);
    const glm::quat half = glm::angleAxis(glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    EXPECT_NEAR(value.y, half.y, 1e-5f);
    EXPECT_NEAR(value.w, half.w, 1e-5f);
    EXPECT_NEAR(glm::length(value), 1.0f, 1e-5f);
}

TEST(MakeClipSet, DurationIsTheLastKeyAndTargetsAreListedOnce) {
    std::vector<AnimationData> animations(1);
    animations[0].name = "both";
    animations[0].channels = {Translation(0, {0.0f, 1.5f}, {0.0f, 1.0f}), Turn(0, 90.0f, 3.0f),
                              Translation(5, {0.0f, 9.0f}, {0.0f, 1.0f})};
    const animation::ClipSet clips = animation::MakeClipSet(animations, std::vector<math::Transform>(2));
    ASSERT_EQ(clips.clips.size(), 1U);
    // The channel of node 5 points outside the two nodes, and is dropped.
    EXPECT_EQ(clips.clips[0].channels.size(), 2U);
    EXPECT_FLOAT_EQ(clips.clips[0].duration, 3.0f);
    EXPECT_EQ(clips.clips[0].targets, (std::vector<std::uint32_t>{0}));
    EXPECT_EQ(clips.Find("both"), 0U);
    EXPECT_FALSE(clips.Find("none").has_value());
}

TEST(Blend, MixesEachPart) {
    math::Transform a;
    math::Transform b;
    b.translation = {2.0f, 0.0f, 0.0f};
    b.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    b.scale = glm::vec3(3.0f);
    const math::Transform half = animation::Blend(a, b, 0.5f);
    EXPECT_FLOAT_EQ(half.translation.x, 1.0f);
    EXPECT_FLOAT_EQ(half.scale.x, 2.0f);
    EXPECT_NEAR(glm::degrees(glm::angle(half.rotation)), 45.0f, 1e-3f);
}

TEST_F(AnimatorTest, UnknownClipIsRefused) {
    EXPECT_FALSE(m_animator->Play("fly"));
    EXPECT_EQ(m_animator->Current(), nullptr);
}

TEST_F(AnimatorTest, PlayingClipMovesItsNodes) {
    ASSERT_TRUE(m_animator->Play("slide"));
    Step(0.25f);
    EXPECT_FLOAT_EQ(X(0), 2.5f);
    // The clip does not move node 1, which keeps what it had.
    EXPECT_FLOAT_EQ(X(1), 0.0f);
}

TEST_F(AnimatorTest, LoopingClipWrapsAround) {
    m_animator->Play("slide");
    Step(1.25f);
    EXPECT_FLOAT_EQ(m_animator->Time(), 0.25f);
    EXPECT_FALSE(m_animator->Finished());
}

TEST_F(AnimatorTest, ClipThatDoesNotLoopStopsOnItsLastPose) {
    m_animator->Play("slide", {.loop = false});
    Step(0.5f);
    EXPECT_FALSE(m_animator->Finished());
    Step(5.0f);
    EXPECT_TRUE(m_animator->Finished());
    EXPECT_FLOAT_EQ(X(0), 10.0f);
}

TEST_F(AnimatorTest, SpeedMultipliesTimeAndZeroHolds) {
    m_animator->Play("slide", {.speed = 2.0f});
    Step(0.25f);
    EXPECT_FLOAT_EQ(X(0), 5.0f);
    m_animator->SetSpeed(0.0f);
    Step(0.25f);
    EXPECT_FLOAT_EQ(X(0), 5.0f);
}

TEST_F(AnimatorTest, PlayingTheSameClipAgainDoesNotRestartIt) {
    m_animator->Play("slide");
    Step(0.5f);
    m_animator->Play("slide");
    EXPECT_FLOAT_EQ(m_animator->Time(), 0.5f);
    m_animator->Play("slide", {.restart = true});
    EXPECT_FLOAT_EQ(m_animator->Time(), 0.0f);
}

TEST_F(AnimatorTest, StartTimeIsInsideTheClip) {
    m_animator->Play("slide", {.startTime = 1.5f});
    EXPECT_FLOAT_EQ(m_animator->Time(), 0.5f);
}

TEST_F(AnimatorTest, PreviousClipFadesOut) {
    m_animator->Play("slide", {.speed = 0.0f, .startTime = 0.5f});
    Step(0.0f);
    EXPECT_FLOAT_EQ(X(0), 5.0f);

    m_animator->Play("rise", {.speed = 0.0f, .fade = 1.0f, .startTime = 1.0f});
    Step(0.5f);
    // Node 0 goes back halfway to its rest at 0. Node 1 is halfway from its rest at 7 to 2.
    EXPECT_FLOAT_EQ(X(0), 2.5f);
    EXPECT_FLOAT_EQ(X(1), 4.5f);

    Step(0.5f);
    EXPECT_FLOAT_EQ(X(0), 0.0f);
    EXPECT_FLOAT_EQ(X(1), 2.0f);
}

TEST_F(AnimatorTest, NoFadeCutsAtOnce) {
    m_animator->Play("slide", {.startTime = 0.5f});
    m_animator->Play("rise", {.fade = 0.0f, .startTime = 1.0f});
    Step(0.0f);
    EXPECT_FLOAT_EQ(X(1), 2.0f);
    // Node 0 is no longer animated, so it keeps whatever it had.
    EXPECT_FLOAT_EQ(X(0), 0.0f);
}

TEST_F(AnimatorTest, DestroyedNodeIsSkipped) {
    m_world.Destroy(m_nodes[0]);
    m_animator->Play("slide");
    Step(0.5f);
    SUCCEED();
}

TEST_F(AnimatorTest, UpdateAnimatorsAdvancesEveryAnimator) {
    const ecs::Entity root = m_world.Create();
    Animator& animator =
        m_world.Add<Animator>(root, TestClips(), std::vector<ecs::Entity>(m_nodes.begin(), m_nodes.end()));
    animator.Play("rise");
    animation::UpdateAnimators(m_world, 1.0f);
    EXPECT_FLOAT_EQ(X(1), 2.0f);
}
