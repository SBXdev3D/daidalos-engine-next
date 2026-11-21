// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/audio/Audio.hpp>
#include <daidalos/core/Result.hpp>

#include <glm/vec3.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace daidalos::scene {

/// A key of a cinematic camera (ADR-0071): where it is, what it looks at, and the lens's focus, at a time
/// from the cue's start. Between keys, the camera moves smoothly.
struct CameraKey {
    float at = 0.0f;
    glm::vec3 position{0.0f};
    glm::vec3 target{0.0f, 0.0f, -1.0f};
    /// f-number, 0 for everything sharp, and meters in focus: a focus pull moves them between keys.
    float aperture = 0.0f;
    float focus = 3.0f;
    [[nodiscard]] bool operator==(const CameraKey&) const = default;
};

/// How a camera cue goes from key to key (ADR-0120).
enum class CameraPath : std::uint8_t {
    /// Straight between keys, easing in and out of each: a few keys, each a shot (ADR-0071).
    Eased,
    /// One smooth curve through every key, without stopping at them, turning the shorter way: a long flight
    /// of many keys. It starts and ends at rest.
    Smooth,
};

/// One thing a sequence does at a time (ADR-0071). Entities are named as in the level (NodeName); paths are
/// relative to the asset root.
struct Cue {
    enum class Kind : std::uint8_t {
        Power,     // Cut or restore a power circuit: circuit, on.
        Light,     // Turn a light on or off: entity, on.
        Sound,     // Play a clip: clip, at the entity if any, volume, ducking.
        Particles, // Spawn an effect: effect, at the entity, count.
        Move,      // Move an entity to a position over seconds.
        Visible,   // Show or hide an entity from every ray: entity, on.
        Shake,     // Shake the camera: strength, seconds.
        Camera,    // Take the camera along keys until the last.
        Mix,       // Fade the mix to a snapshot over seconds.
        Event,     // A name the game's code takes (Scene::TakeEvents).
        Subtitle,  // A line someone says (ADR-0075): text and speaker, keys of the text table, for seconds.
    };
    float at = 0.0f;
    Kind kind = Kind::Event;
    std::string name; // The circuit, the entity, or the event.
    std::string path; // The clip or the effect.
    /// A subtitle's text, or the caption of a sound; a key of the text table (ADR-0075).
    std::string caption;
    bool on = true;
    float volume = 1.0f;
    float ducking = 0.0f;
    std::uint32_t count = 1;
    glm::vec3 position{0.0f};
    float seconds = 0.0f;
    float strength = 0.0f;
    std::vector<CameraKey> keys;
    CameraPath cameraPath = CameraPath::Eased;
    audio::MixSnapshot mix;
    [[nodiscard]] bool operator==(const Cue&) const = default;
};

/// The name of a kind of cue as its file writes it ("power", "sound"...).
[[nodiscard]] std::string_view CueKindName(Cue::Kind kind);

/// A timeline of cues, from a .sequence.json file: {"cues": [{"at": 0.5, "sound": {...}}, ...]}. Each cue is
/// its time and one kind, named by its key: power, light, sound, particles, move, visible, shake, camera,
/// mix, event or subtitle.
struct Sequence {
    std::vector<Cue> cues;
    /// What the sequence is for, for people: the file's "comment".
    std::string comment;
    /// Seconds to its last cue, and to the end of its last camera move.
    [[nodiscard]] float Length() const;
    [[nodiscard]] bool operator==(const Sequence&) const = default;
};

/// Parses a sequence. Unknown keys and kinds are errors that name them.
[[nodiscard]] core::Result<Sequence> ParseSequence(std::string_view json, std::string_view name = "sequence");

/// Writes a sequence as its file (ADR-0079): cues in time order, each with the fields its kind uses, keys sorted.
[[nodiscard]] std::string WriteSequence(const Sequence& sequence);

/// Where a cinematic camera is at a time, between its keys. On a smooth path the target is a meter ahead,
/// in the direction the camera looks, which never comes closer than a degree to straight up or down.
[[nodiscard]] CameraKey CameraAt(const std::vector<CameraKey>& keys, float time, CameraPath path = CameraPath::Eased);

} // namespace daidalos::scene
