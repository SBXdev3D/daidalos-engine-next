// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/renderer/Particles.hpp>
#include <daidalos/scene/Components.hpp>

#include <string>
#include <string_view>

namespace daidalos::scene {

/// A particle effect as a file describes it (ADR-0053): what its particles look like and how they move,
/// the image they use, and how its emitters emit unless they say otherwise.
struct ParticleEffectFile {
    /// Its texture is not set: the file names it, relative to the file's own directory.
    renderer::ParticleEffectDesc desc;
    std::string texture;
    ParticleEmission emission;
};

/// Reads an effect file, a JSON object (`.effect.json`, ADR-0053). Its keys are those of
/// renderer::ParticleEffectDesc, with these differences:
/// - "texture" is the image's path, relative to the file's directory, and is required;
/// - "spread" is in degrees, and "rotationSpeed" in degrees per second;
/// - "lifetime", "size" and "speed" are a number, or the two numbers they are picked between;
/// - "color" is a list of [time, [r, g, b, a]] keys (a defaults to 1), and the curves over life lists of
///   [time, value] keys, or a number that holds over the whole life;
/// - "orientation" is "camera", "velocity", "axis" or "plane", and "shape" "box", "cylinder", "cone" or
///   "line";
/// - "emitter" gives ParticleEmission: "rate", "ratePerMeter", and "bursts", a list of objects with
///   "time", "count", "interval" and "repeats";
/// - "comment" is free text.
/// Anything else, a value of the wrong kind, or a flipbook that does not fit in the grid is a ParseError that
/// names the key, so that a typo does not go unnoticed.
[[nodiscard]] core::Result<ParticleEffectFile> ParseParticleEffect(std::string_view json);

} // namespace daidalos::scene
