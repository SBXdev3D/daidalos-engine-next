// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include <daidalos/renderer/DisplayTransform.hpp>

#include <glm/common.hpp>
#include <glm/exponential.hpp>
#include <glm/mat3x3.hpp>

#include <algorithm>
#include <cmath>

namespace daidalos::renderer {

namespace {

// AgX as Blender and Filament make it, in the form three.js gives it (MIT): the matrices are written by
// columns, as glm and GLSL build them. Shaders/Output.slang holds the same numbers.
const glm::mat3 kRec709ToRec2020{glm::vec3(0.6274f, 0.0691f, 0.0164f), glm::vec3(0.3293f, 0.9195f, 0.0880f),
                                 glm::vec3(0.0433f, 0.0113f, 0.8956f)};
const glm::mat3 kRec2020ToRec709{glm::vec3(1.6605f, -0.1246f, -0.0182f), glm::vec3(-0.5876f, 1.1329f, -0.1006f),
                                 glm::vec3(-0.0728f, -0.0083f, 1.1187f)};
const glm::mat3 kAgxInset{glm::vec3(0.856627153315983f, 0.137318972929847f, 0.11189821299995f),
                          glm::vec3(0.0951212405381588f, 0.761241990602591f, 0.0767994186031903f),
                          glm::vec3(0.0482516061458583f, 0.101439036467562f, 0.811302368396859f)};
const glm::mat3 kAgxOutset{glm::vec3(1.1271005818144368f, -0.1413297634984383f, -0.14132976349843826f),
                           glm::vec3(-0.11060664309660323f, 1.157823702216272f, -0.11060664309660294f),
                           glm::vec3(-0.016493938717834573f, -0.016493938717834257f, 1.2519364065950405f)};
// Stops below and above middle grey (0.18) that the log encoding spans: log2(2^-10 * 0.18), log2(2^6.5 * 0.18).
constexpr float kAgxMinEv = -12.47393f;
constexpr float kAgxMaxEv = 4.026069f;

// AgX's sigmoid, fitted by a polynomial (Wrensch, "Minimal AgX implementation", 2023).
glm::vec3 AgxContrast(glm::vec3 x) {
    const glm::vec3 x2 = x * x;
    const glm::vec3 x4 = x2 * x2;
    return 15.5f * x4 * x2 - 40.14f * x4 * x + 31.96f * x4 - 6.868f * x2 * x + 0.4298f * x2 + 0.1191f * x - 0.00232f;
}

} // namespace

glm::vec3 AgX(glm::vec3 color) {
    color = kAgxInset * (kRec709ToRec2020 * glm::max(color, 0.0f));
    color = glm::log2(glm::max(color, 1e-10f));
    color = glm::clamp((color - kAgxMinEv) / (kAgxMaxEv - kAgxMinEv), 0.0f, 1.0f);
    color = kAgxOutset * AgxContrast(color);
    // The sigmoid's output is display-encoded with a 2.2 power.
    color = glm::pow(glm::max(color, 0.0f), glm::vec3(2.2f));
    return glm::clamp(kRec2020ToRec709 * color, 0.0f, 1.0f);
}

glm::vec3 PbrNeutral(glm::vec3 color) {
    constexpr float kStartCompression = 0.8f - 0.04f;
    constexpr float kDesaturation = 0.15f;
    color = glm::max(color, 0.0f);
    const float lowest = std::min({color.r, color.g, color.b});
    const float offset = lowest < 0.08f ? lowest - (6.25f * lowest * lowest) : 0.04f;
    color -= offset;
    const float peak = std::max({color.r, color.g, color.b});
    if (peak < kStartCompression) {
        return color;
    }
    constexpr float kD = 1.0f - kStartCompression;
    const float newPeak = 1.0f - (kD * kD / (peak + kD - kStartCompression));
    color *= newPeak / peak;
    const float g = 1.0f - (1.0f / ((kDesaturation * (peak - newPeak)) + 1.0f));
    return glm::mix(color, glm::vec3(newPeak), g);
}

glm::vec3 ExpandHighlights(glm::vec3 color, float headroom) {
    const float largest = std::max({color.r, color.g, color.b});
    if (headroom <= 1.0f || largest <= 0.0f) {
        return color;
    }
    const float expanded = largest / (1.0f - (std::min(largest, 1.0f) * (1.0f - (1.0f / headroom))));
    return color * (expanded / largest);
}

glm::vec3 DisplayColor(glm::vec3 color, ToneMapper toneMapper, float headroom) {
    const glm::vec3 mapped = toneMapper == ToneMapper::AgX ? AgX(color) : PbrNeutral(color);
    return ExpandHighlights(mapped, headroom);
}

glm::vec3 Rec709ToRec2020(glm::vec3 color) {
    return kRec709ToRec2020 * color;
}

float PqEncode(float nits) {
    constexpr float kM1 = 2610.0f / 16384.0f;
    constexpr float kM2 = 2523.0f / 4096.0f * 128.0f;
    constexpr float kC1 = 3424.0f / 4096.0f;
    constexpr float kC2 = 2413.0f / 4096.0f * 32.0f;
    constexpr float kC3 = 2392.0f / 4096.0f * 32.0f;
    const float y = std::pow(std::clamp(nits / 10000.0f, 0.0f, 1.0f), kM1);
    return std::pow((kC1 + (kC2 * y)) / (1.0f + (kC3 * y)), kM2);
}

} // namespace daidalos::renderer
