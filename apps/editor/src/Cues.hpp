// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/scene/Sequence.hpp>

#include <array>
#include <string>

// What the sequence panel needs to know of cues, apart from drawing them (ADR-0079).
namespace editor {

/// Every kind of cue, in the order the panel offers them.
inline constexpr std::array<daidalos::scene::Cue::Kind, 12> kCueKinds{
    daidalos::scene::Cue::Kind::Sound,  daidalos::scene::Cue::Kind::Subtitle,  daidalos::scene::Cue::Kind::Light,
    daidalos::scene::Cue::Kind::Power,  daidalos::scene::Cue::Kind::Particles, daidalos::scene::Cue::Kind::Shake,
    daidalos::scene::Cue::Kind::Mix,    daidalos::scene::Cue::Kind::Move,      daidalos::scene::Cue::Kind::Visible,
    daidalos::scene::Cue::Kind::Camera, daidalos::scene::Cue::Kind::Fade,      daidalos::scene::Cue::Kind::Event};

/// A new cue of a kind at a time, with values that do something when played as they are: a full volume, a
/// short shake, a mix that fades in half a second, a camera of two keys a second apart.
[[nodiscard]] daidalos::scene::Cue MakeCue(daidalos::scene::Cue::Kind kind, float at);

/// One line saying what a cue does: "sound zombie_moan.wav at Particles drip 113".
[[nodiscard]] std::string CueSummary(const daidalos::scene::Cue& cue);

/// The cue's color on the timeline, as ImGui's packed RGBA.
[[nodiscard]] unsigned CueColor(daidalos::scene::Cue::Kind kind);

} // namespace editor
