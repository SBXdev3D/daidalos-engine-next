// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/scene/Level.hpp>
#include <daidalos/scene/Sequence.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace editor {

/// Something wrong with a level, which the map check lists (ADR-0111).
struct Problem {
    /// Notes are worth knowing but often meant, such as what lies outside the rooms.
    enum class Severity : std::uint8_t { Error, Warning, Note };
    Severity severity = Severity::Warning;
    /// The entity it is about, which a click selects; 0 for the level as a whole.
    std::uint64_t entity = 0;
    std::string message;
};

/// What the check reads beside the level, given so that it can be tested without files.
struct CheckSources {
    /// Whether a path under the asset root names a file.
    std::function<bool(const std::string& path)> exists;
    /// The sequence a path under the asset root holds, if it can be read.
    std::function<std::optional<daidalos::scene::Sequence>(const std::string& path)> sequence;
};

/// The problems of a level read from levelPath, a path under the asset root (ADR-0111), errors first:
/// - ids used twice, parents missing or holding each other;
/// - files named that are not there: models, prefabs, effects, decal images, light profiles and cookies, the sky,
///   sequences;
/// - a trigger without a sequence, a sequence naming entities the level has not;
/// - a scale of zero, a light that gives no light;
/// - two copies of a model in the same place;
/// - no player start, or more than one;
/// - with rooms (ADR-0089), what lies outside all of them, as notes.
[[nodiscard]] std::vector<Problem> CheckLevel(const daidalos::scene::Level& level, const std::string& levelPath,
                                              const CheckSources& sources);

/// The path under the asset root of a path a level names, relative to the level's directory.
[[nodiscard]] std::string UnderRoot(const std::string& levelPath, const std::string& path);

} // namespace editor
