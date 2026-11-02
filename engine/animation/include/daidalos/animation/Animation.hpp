// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <daidalos/ecs/World.hpp>
#include <daidalos/math/Transform.hpp>
#include <daidalos/resources/ModelData.hpp>

#include <glm/vec4.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace daidalos::animation {

/// A named piece of motion, such as walking, for the nodes of one model (ADR-0020).
struct Clip {
    std::string name;
    /// Seconds: the time of the last key of any channel.
    float duration = 0.0f;
    /// Each one moves a node of the model, by its index in the model.
    std::vector<resources::AnimationChannel> channels;
    /// The nodes that the channels move, each once.
    std::vector<std::uint32_t> targets;
};

/// The clips of a model, and the pose its nodes rest in when no clip moves them. Shared by every
/// instance of the model.
struct ClipSet {
    std::vector<Clip> clips;
    /// For each node of the model, its local transform.
    std::vector<math::Transform> restPose;

    [[nodiscard]] std::optional<std::size_t> Find(std::string_view name) const;
};

/// Builds the clips of a model. Channels that point outside the rest pose are dropped.
[[nodiscard]] ClipSet MakeClipSet(std::span<const resources::AnimationData> animations,
                                  std::vector<math::Transform> restPose);

/// The value of a channel at a time, which is clamped to its keys. A rotation comes back normalized.
[[nodiscard]] glm::vec4 SampleChannel(const resources::AnimationChannel& channel, float time);

/// Overwrites what the clip moves in the pose, which holds one transform per node of the model.
void SamplePose(const Clip& clip, float time, std::span<math::Transform> pose);

/// Blends two transforms: weight 0 gives a, 1 gives b. Rotations take the short way.
[[nodiscard]] math::Transform Blend(const math::Transform& a, const math::Transform& b, float weight);

struct PlayOptions {
    bool loop = true;
    /// Multiplies time. 0 holds the current pose.
    float speed = 1.0f;
    /// Seconds over which the previous clip fades out. 0 cuts at once.
    float fade = 0.2f;
    /// Seconds into the clip.
    float startTime = 0.0f;
    /// Plays the clip from the start even if it is already playing.
    bool restart = false;
};

/// ECS component on the root of a model instance: plays its clips on the entities of its nodes, by
/// writing their LocalTransform. One clip plays at a time, and the previous one fades out under it.
class Animator {
public:
    /// targets holds the entity of each node of the model, in the order of the rest pose.
    Animator(std::shared_ptr<const ClipSet> clips, std::vector<ecs::Entity> targets);

    /// Starts a clip. Does nothing if the clip is already playing, unless options.restart is set.
    /// Returns false for an unknown clip, which leaves the animator as it was.
    bool Play(std::string_view clip, const PlayOptions& options = {});
    void SetSpeed(float speed);

    [[nodiscard]] const ClipSet& Clips() const { return *m_clips; }
    /// Null before the first Play.
    [[nodiscard]] const Clip* Current() const;
    /// Seconds into the current clip.
    [[nodiscard]] float Time() const { return m_current.time; }
    /// Whether a clip that does not loop has reached its end. Its last pose stays.
    [[nodiscard]] bool Finished() const;

    /// Moves time forward, in seconds.
    void Advance(float delta);
    /// Writes the pose into the targets that the clips move. Targets that are gone are skipped.
    void Apply(ecs::World& world);

private:
    struct Layer {
        std::optional<std::size_t> clip;
        float time = 0.0f;
        float speed = 1.0f;
        bool loop = true;
    };

    void AdvanceLayer(Layer& layer, float delta) const;

    std::shared_ptr<const ClipSet> m_clips;
    std::vector<ecs::Entity> m_targets;
    Layer m_current;
    Layer m_previous;
    float m_fade = 0.0f;
    float m_fadeElapsed = 0.0f;
    // Reused from frame to frame.
    std::vector<math::Transform> m_pose;
    std::vector<math::Transform> m_fadingPose;
};

/// Advances every Animator and applies its pose. Run it before the world transforms are computed.
void UpdateAnimators(ecs::World& world, float delta);

} // namespace daidalos::animation
