// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#include <daidalos/animation/Animation.hpp>
#include <daidalos/core/Assert.hpp>
#include <daidalos/core/Profile.hpp>
#include <daidalos/ecs/Transform.hpp>

#include <glm/common.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace daidalos::animation {

namespace {

glm::quat ToQuaternion(const glm::vec4& value) {
    return glm::quat(value.w, value.x, value.y, value.z);
}

void Store(const resources::AnimationChannel& channel, const glm::vec4& value, math::Transform& transform) {
    switch (channel.path) {
    case resources::AnimationPath::Translation:
        transform.translation = glm::vec3(value);
        break;
    case resources::AnimationPath::Rotation:
        transform.rotation = ToQuaternion(value);
        break;
    case resources::AnimationPath::Scale:
        transform.scale = glm::vec3(value);
        break;
    }
}

} // namespace

std::optional<std::size_t> ClipSet::Find(std::string_view name) const {
    for (std::size_t index = 0; index < clips.size(); ++index) {
        if (clips[index].name == name) {
            return index;
        }
    }
    return std::nullopt;
}

ClipSet MakeClipSet(std::span<const resources::AnimationData> animations, std::vector<math::Transform> restPose) {
    ClipSet set;
    set.restPose = std::move(restPose);
    set.clips.reserve(animations.size());
    for (const resources::AnimationData& animation : animations) {
        Clip& clip = set.clips.emplace_back();
        clip.name = animation.name;
        for (const resources::AnimationChannel& channel : animation.channels) {
            if (channel.node >= set.restPose.size() || channel.times.empty() ||
                channel.times.size() != channel.values.size()) {
                continue;
            }
            clip.duration = std::max(clip.duration, channel.times.back());
            clip.channels.push_back(channel);
            if (std::ranges::find(clip.targets, channel.node) == clip.targets.end()) {
                clip.targets.push_back(channel.node);
            }
        }
    }
    return set;
}

glm::vec4 SampleChannel(const resources::AnimationChannel& channel, float time) {
    DAI_ASSERT(!channel.times.empty() && channel.times.size() == channel.values.size(),
               "A channel needs one value per key");
    const std::vector<float>& times = channel.times;
    const bool rotation = channel.path == resources::AnimationPath::Rotation;
    const auto finish = [rotation](const glm::vec4& value) {
        if (!rotation) {
            return value;
        }
        const glm::quat normalized = glm::normalize(ToQuaternion(value));
        return glm::vec4(normalized.x, normalized.y, normalized.z, normalized.w);
    };
    if (time <= times.front()) {
        return finish(channel.values.front());
    }
    if (time >= times.back()) {
        return finish(channel.values.back());
    }

    // The first key after the time, and the one before it.
    const auto next = static_cast<std::size_t>(std::ranges::upper_bound(times, time) - times.begin());
    const std::size_t previous = next - 1;
    const glm::vec4& a = channel.values[previous];
    const glm::vec4& b = channel.values[next];
    if (channel.interpolation == resources::AnimationInterpolation::Step) {
        return finish(a);
    }
    const float span = times[next] - times[previous];
    const float weight = span > 0.0f ? (time - times[previous]) / span : 0.0f;
    if (rotation) {
        const glm::quat mixed = glm::normalize(glm::slerp(ToQuaternion(a), ToQuaternion(b), weight));
        return {mixed.x, mixed.y, mixed.z, mixed.w};
    }
    return glm::mix(a, b, weight);
}

void SamplePose(const Clip& clip, float time, std::span<math::Transform> pose) {
    for (const resources::AnimationChannel& channel : clip.channels) {
        if (channel.node < pose.size()) {
            Store(channel, SampleChannel(channel, time), pose[channel.node]);
        }
    }
}

math::Transform Blend(const math::Transform& a, const math::Transform& b, float weight) {
    math::Transform result;
    result.translation = glm::mix(a.translation, b.translation, weight);
    // glm::slerp goes the short way.
    result.rotation = glm::normalize(glm::slerp(a.rotation, b.rotation, weight));
    result.scale = glm::mix(a.scale, b.scale, weight);
    return result;
}

Animator::Animator(std::shared_ptr<const ClipSet> clips, std::vector<ecs::Entity> targets)
    : m_clips(std::move(clips)), m_targets(std::move(targets)) {
    DAI_ASSERT(m_clips != nullptr, "An animator needs clips");
    DAI_ASSERT(m_targets.size() == m_clips->restPose.size(), "An animator needs one target per node");
}

bool Animator::Play(std::string_view clip, const PlayOptions& options) {
    const std::optional<std::size_t> found = m_clips->Find(clip);
    if (!found.has_value()) {
        return false;
    }
    Layer next{found, std::max(options.startTime, 0.0f), std::max(options.speed, 0.0f), options.loop};
    if (m_current.clip == found && !options.restart) {
        m_current.speed = next.speed;
        m_current.loop = next.loop;
        return true;
    }
    if (m_current.clip.has_value() && options.fade > 0.0f) {
        m_previous = m_current;
        m_fade = options.fade;
        m_fadeElapsed = 0.0f;
    } else {
        m_previous = {};
        m_fade = 0.0f;
    }
    m_current = next;
    AdvanceLayer(m_current, 0.0f); // Brings the start time inside the clip.
    return true;
}

void Animator::SetSpeed(float speed) {
    m_current.speed = std::max(speed, 0.0f);
}

const Clip* Animator::Current() const {
    return m_current.clip.has_value() ? &m_clips->clips[*m_current.clip] : nullptr;
}

bool Animator::Finished() const {
    const Clip* clip = Current();
    return clip != nullptr && !m_current.loop && m_current.time >= clip->duration;
}

void Animator::AdvanceLayer(Layer& layer, float delta) const {
    if (!layer.clip.has_value()) {
        return;
    }
    const float duration = m_clips->clips[*layer.clip].duration;
    layer.time += delta * layer.speed;
    if (duration <= 0.0f) {
        layer.time = 0.0f;
    } else if (layer.loop) {
        layer.time = std::fmod(layer.time, duration);
    } else {
        layer.time = std::min(layer.time, duration);
    }
}

void Animator::Advance(float delta) {
    AdvanceLayer(m_current, delta);
    if (m_previous.clip.has_value()) {
        AdvanceLayer(m_previous, delta);
        m_fadeElapsed += delta;
    }
}

void Animator::Apply(ecs::World& world) {
    const Clip* current = Current();
    if (current == nullptr) {
        return;
    }
    m_pose.assign(m_clips->restPose.begin(), m_clips->restPose.end());
    SamplePose(*current, m_current.time, m_pose);

    const Clip* previous = m_previous.clip.has_value() ? &m_clips->clips[*m_previous.clip] : nullptr;
    if (previous != nullptr) {
        m_fadingPose.assign(m_clips->restPose.begin(), m_clips->restPose.end());
        SamplePose(*previous, m_previous.time, m_fadingPose);
        const float weight = std::clamp(m_fadeElapsed / m_fade, 0.0f, 1.0f);
        for (const std::uint32_t target : previous->targets) {
            m_pose[target] = Blend(m_fadingPose[target], m_pose[target], weight);
        }
        // Nodes that only the current clip moves fade in from where they rested.
        for (const std::uint32_t target : current->targets) {
            if (std::ranges::find(previous->targets, target) == previous->targets.end()) {
                m_pose[target] = Blend(m_fadingPose[target], m_pose[target], weight);
            }
        }
    }

    const auto write = [&](std::uint32_t target) {
        if (auto* transform = world.TryGet<ecs::LocalTransform>(m_targets[target])) {
            static_cast<math::Transform&>(*transform) = m_pose[target];
        }
    };
    for (const std::uint32_t target : current->targets) {
        write(target);
    }
    if (previous != nullptr) {
        for (const std::uint32_t target : previous->targets) {
            if (std::ranges::find(current->targets, target) == current->targets.end()) {
                write(target);
            }
        }
        // Once the previous clip is written at no weight, its nodes are where the current one wants them.
        if (m_fadeElapsed >= m_fade) {
            m_previous = {};
        }
    }
}

void UpdateAnimators(ecs::World& world, float delta) {
    DAI_PROFILE_SCOPE("animation::UpdateAnimators");
    world.Each<Animator>([&world, delta](ecs::Entity /*entity*/, Animator& animator) {
        animator.Advance(delta);
        animator.Apply(world);
    });
}

} // namespace daidalos::animation
