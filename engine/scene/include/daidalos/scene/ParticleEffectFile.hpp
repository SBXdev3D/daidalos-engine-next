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
    /// Its texture and its sub-emitters' effects are not set: the file names them, relative to its own
    /// directory.
    renderer::ParticleEffectDesc desc;
    std::string texture;
    /// A mesh effect's model (renderer::ParticleEffectDesc::mesh), whose first part it draws, or empty.
    std::string mesh;
    /// The effect files of onDeath and onHit, or empty.
    std::string deathEffect;
    std::string hitEffect;
    /// The six-way light maps, or empty.
    std::string lightPositive;
    std::string lightNegative;
    ParticleEmission emission;
};

/// Reads an effect file, a JSON object (`.effect.json`, ADR-0053). Its keys are those of
/// renderer::ParticleEffectDesc, with these differences:
/// - "texture" is the image's path, relative to the file's directory, and is required unless "mesh", the
///   path of a glTF model whose first part a mesh effect draws, is given;
/// - "spread" is in degrees, and "rotationSpeed" in degrees per second;
/// - "lifetime", "size" and "speed" are a number, or the two numbers they are picked between;
/// - "color" is a list of [time, [r, g, b, a]] keys (a defaults to 1), and the curves over life lists of
///   [time, value] keys, or a number that holds over the whole life;
/// - "orientation" is "camera", "velocity", "axis" or "plane", "shape" "box", "cylinder", "cone" or
///   "line", and "collision" "none", "bounce", "stick" or "die";
/// - "onDeath" and "onHit" are objects with "effect", the path of another effect file relative to this
///   one's directory, and "count";
/// - "emitter" gives ParticleEmission: "rate", "ratePerMeter", and "bursts", a list of objects with
///   "time", "count", "interval" and "repeats";
/// - "lightMaps" is the paths of the two six-way light maps, positive then negative;
/// - "light" gives EmitterLight: "color", "temperature", "intensity", "range", "radius", "offset", "fade",
///   "flickerRate" and "flickerDepth";
/// - "comment" is free text.
/// Anything else, a value of the wrong kind, or a flipbook that does not fit in the grid is a ParseError that
/// names the key, so that a typo does not go unnoticed.
[[nodiscard]] core::Result<ParticleEffectFile> ParseParticleEffect(std::string_view json);

/// A ribbon as a file describes it (ADR-0053). Its texture is not set: the file names it, relative to its own
/// directory, or not at all, for plain color.
struct RibbonEffectFile {
    RibbonEffect effect;
    std::string texture;
};

/// Reads a ribbon file, a JSON object (`.ribbon.json`, ADR-0053). Its keys are those of RibbonEffect, with
/// "texture" the image's path, "shape" "line", "bolt" or "trail", "color" a list of [time, [r, g, b, a]] keys
/// and "width" a list of [time, meters] keys or a number, both along the ribbon, "light" as an effect file's,
/// and "comment" free text. Anything else, or a value of the wrong kind, is a ParseError that names the key.
[[nodiscard]] core::Result<RibbonEffectFile> ParseRibbonEffect(std::string_view json);

} // namespace daidalos::scene
