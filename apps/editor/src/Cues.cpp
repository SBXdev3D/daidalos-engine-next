// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "Cues.hpp"

#include <filesystem>
#include <format>

using namespace daidalos;

namespace editor {

namespace {

constexpr unsigned Rgba(unsigned r, unsigned g, unsigned b) {
    return r | (g << 8U) | (b << 16U) | (255U << 24U);
}

std::string FileName(const std::string& path) {
    return std::filesystem::path(path).filename().generic_string();
}

std::string At(const std::string& entity) {
    return entity.empty() ? std::string{} : std::format(" at {}", entity);
}

} // namespace

scene::Cue MakeCue(scene::Cue::Kind kind, float at) {
    scene::Cue cue{.at = at, .kind = kind};
    switch (kind) {
    case scene::Cue::Kind::Power:
    case scene::Cue::Kind::Light:
    case scene::Cue::Kind::Visible:
        // Cutting, putting out and hiding are what scares do first.
        cue.on = false;
        break;
    case scene::Cue::Kind::Particles:
        cue.count = 20;
        break;
    case scene::Cue::Kind::Move:
        cue.seconds = 1.0f;
        break;
    case scene::Cue::Kind::Shake:
        cue.strength = 0.5f;
        cue.seconds = 0.5f;
        break;
    case scene::Cue::Kind::Mix:
        cue.mix.lowpass = 2000.0f;
        cue.seconds = 0.5f;
        break;
    case scene::Cue::Kind::Subtitle:
        cue.seconds = 3.0f;
        cue.name = "speaker.you";
        break;
    case scene::Cue::Kind::Camera:
        cue.keys = {scene::CameraKey{.at = 0.0f, .position = {0.0f, 1.7f, 3.0f}, .target = {0.0f, 1.5f, 0.0f}},
                    scene::CameraKey{.at = 1.0f, .position = {0.0f, 1.7f, 2.0f}, .target = {0.0f, 1.5f, 0.0f}}};
        break;
    case scene::Cue::Kind::Fade:
        cue.strength = 1.0f;
        cue.seconds = 1.0f;
        break;
    case scene::Cue::Kind::Event:
        cue.name = "event";
        break;
    case scene::Cue::Kind::Sound:
        break;
    }
    return cue;
}

std::string CueSummary(const scene::Cue& cue) {
    switch (cue.kind) {
    case scene::Cue::Kind::Power:
        return std::format("power {} {}", cue.name, cue.on ? "on" : "off");
    case scene::Cue::Kind::Light:
        return std::format("light {} {}", cue.name, cue.on ? "on" : "off");
    case scene::Cue::Kind::Visible:
        return std::format("{} {}", cue.on ? "show" : "hide", cue.name);
    case scene::Cue::Kind::Sound:
        return std::format("sound {}{}", FileName(cue.path), At(cue.name));
    case scene::Cue::Kind::Subtitle:
        return std::format("subtitle {}: {}", cue.name, cue.caption);
    case scene::Cue::Kind::Particles:
        return std::format("{} x {}{}", FileName(cue.path), cue.count, At(cue.name));
    case scene::Cue::Kind::Move:
        return std::format("move {} over {:.2g} s", cue.name, cue.seconds);
    case scene::Cue::Kind::Shake:
        return std::format("shake {:.2g} for {:.2g} s", cue.strength, cue.seconds);
    case scene::Cue::Kind::Mix:
        return std::format("mix, low-pass {:.0f} Hz, over {:.2g} s", cue.mix.lowpass, cue.seconds);
    case scene::Cue::Kind::Camera:
        return std::format("camera, {} keys over {:.2g} s", cue.keys.size(),
                           cue.keys.empty() ? 0.0f : cue.keys.back().at);
    case scene::Cue::Kind::Fade:
        return std::format("fade to {:.2g} over {:.2g} s", cue.strength, cue.seconds);
    case scene::Cue::Kind::Event:
        return std::format("event \"{}\"", cue.name);
    }
    return {};
}

unsigned CueColor(scene::Cue::Kind kind) {
    switch (kind) {
    case scene::Cue::Kind::Sound:
        return Rgba(110, 190, 255);
    case scene::Cue::Kind::Subtitle:
        return Rgba(235, 235, 235);
    case scene::Cue::Kind::Light:
    case scene::Cue::Kind::Power:
        return Rgba(255, 220, 90);
    case scene::Cue::Kind::Particles:
        return Rgba(255, 140, 60);
    case scene::Cue::Kind::Shake:
        return Rgba(230, 90, 90);
    case scene::Cue::Kind::Mix:
        return Rgba(150, 120, 255);
    case scene::Cue::Kind::Move:
    case scene::Cue::Kind::Visible:
        return Rgba(120, 220, 140);
    case scene::Cue::Kind::Camera:
        return Rgba(90, 220, 220);
    case scene::Cue::Kind::Fade:
        return Rgba(130, 130, 130);
    case scene::Cue::Kind::Event:
        return Rgba(200, 120, 200);
    }
    return Rgba(200, 200, 200);
}

} // namespace editor
