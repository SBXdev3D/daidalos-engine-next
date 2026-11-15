// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/renderer/Particles.hpp>

#include <glm/common.hpp>

#include <algorithm>
#include <cstddef>
#include <span>

namespace daidalos::renderer {

namespace {

// The value of keys at a time: blended between the two around it, held before the first and after the last.
// Keys are taken in the order of their times. Without keys, the curve is fallback.
template <typename Key, typename Value, typename Get>
Value Evaluate(std::span<const Key> keys, float time, Value fallback, Get get) {
    if (keys.empty()) {
        return fallback;
    }
    if (time <= keys.front().time) {
        return get(keys.front());
    }
    for (std::size_t i = 1; i < keys.size(); ++i) {
        if (time <= keys[i].time) {
            const float span = keys[i].time - keys[i - 1].time;
            const float t = span > 0.0f ? (time - keys[i - 1].time) / span : 1.0f;
            return glm::mix(get(keys[i - 1]), get(keys[i]), t);
        }
    }
    return get(keys.back());
}

template <typename Key>
std::vector<Key> Sorted(const std::vector<Key>& keys) {
    std::vector<Key> sorted = keys;
    std::stable_sort(sorted.begin(), sorted.end(), [](const Key& a, const Key& b) { return a.time < b.time; });
    return sorted;
}

} // namespace

std::vector<glm::vec4> BakeParticleCurves(const ParticleEffectDesc& desc) {
    const std::vector<ParticleColorKey> color = Sorted(desc.color);
    const std::vector<ParticleCurveKey> size = Sorted(desc.sizeOverLife);
    const std::vector<ParticleCurveKey> emission = Sorted(desc.emissionOverLife);
    const std::vector<ParticleCurveKey> speed = Sorted(desc.speedOverLife);
    const auto value = [](const ParticleCurveKey& key) { return key.value; };
    std::vector<glm::vec4> baked(std::size_t{2} * kParticleCurveSamples);
    for (std::uint32_t i = 0; i < kParticleCurveSamples; ++i) {
        const float time = static_cast<float>(i) / static_cast<float>(kParticleCurveSamples - 1);
        baked[i] = Evaluate<ParticleColorKey>(color, time, glm::vec4(1.0f),
                                              [](const ParticleColorKey& key) { return key.color; });
        baked[kParticleCurveSamples + i] = {Evaluate<ParticleCurveKey>(size, time, 1.0f, value),
                                            Evaluate<ParticleCurveKey>(emission, time, 1.0f, value),
                                            Evaluate<ParticleCurveKey>(speed, time, 1.0f, value), 0.0f};
    }
    return baked;
}

} // namespace daidalos::renderer
