// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/renderer/CornellBox.hpp>
#include <daidalos/renderer/SceneView.hpp>

#include <glm/vec3.hpp>

#include <vector>

namespace daidalos::renderer {

/// The dark corridor (ADR-0045), the test scene of indirect light where a horror game spends most of its
/// time: a corridor 2 m wide, 3 m high and 20 m long, of mid-grey walls, with a doorway into a side room and
/// a few boxes, lit by nothing but a flashlight that follows the camera, as the player's does. Most of what
/// the camera sees is light the flashlight's spot sends back. Its interior spans x from -1 to 1, y from 0
/// to 3 and z from 0 to -20; the side room opens on the right, at x = 1. Made for EV100 3, the factory's.
struct DarkCorridor {
    /// Every part is a MakeCube.
    std::vector<CornellPart> parts;
    /// A camera near the start, looking down the corridor.
    glm::vec3 eye{0.0f};
    glm::vec3 target{0.0f};
    float verticalFov = 0.0f;
    float ev100 = 3.0f;
};

[[nodiscard]] DarkCorridor MakeDarkCorridor();

/// The flashlight of a camera at eye looking towards target: the player's, held in the left hand below the
/// eye and aimed at a point 8 m ahead.
[[nodiscard]] Light Flashlight(const glm::vec3& eye, const glm::vec3& target);

} // namespace daidalos::renderer
