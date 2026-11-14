// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/renderer/CornellBox.hpp>
#include <daidalos/renderer/SceneView.hpp>

#include <vector>

namespace daidalos::renderer {

/// The test scene of reflections and glass (ADR-0042): a room 6 m wide and 3 m high, open at the front, lit
/// by two lamps. A mirror covers most of its back wall; its floor is polished stone, and a block of brushed
/// gold stands on it. In front of a white block stand a window pane, thin glass, and a block of thick
/// blue-green glass. Its interior spans x and z from -3 to 3, and y from 0 to 3. Its lights are made for
/// EV100 3 (ADR-0043).
struct MirrorRoom {
    /// Every part is a MakeCube.
    std::vector<CornellPart> parts;
    std::vector<Light> lights;
    /// A camera in front of the opening, which sees the whole room and the mirror.
    glm::vec3 eye{0.0f};
    glm::vec3 target{0.0f};
    float verticalFov = 0.0f;
};

[[nodiscard]] MirrorRoom MakeMirrorRoom();

} // namespace daidalos::renderer
