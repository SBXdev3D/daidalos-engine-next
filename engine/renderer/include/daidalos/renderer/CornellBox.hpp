// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

#pragma once

#include <daidalos/renderer/Material.hpp>
#include <daidalos/renderer/SceneView.hpp>
#include <daidalos/resources/ModelData.hpp>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <string>
#include <vector>

namespace daidalos::renderer {

/// One box of the Cornell box: a cube mesh scaled and turned into place.
struct CornellPart {
    std::string name;
    /// Its center, its size, and its turn around the vertical, in radians.
    glm::vec3 position{0.0f};
    glm::vec3 size{1.0f};
    float yaw = 0.0f;
    /// The same placement as a matrix: translation, then the turn, then the size.
    glm::mat4 world{1.0f};
    MaterialDesc material;
};

/// The Cornell box, the classic test scene of rendering (ADR-0034): a room 5.5 m wide, open at the front,
/// with a red wall on the left, a green one on the right and two white blocks, all matte. The original
/// is lit by a panel in the ceiling; the engine has no area lights, so a point light hangs under it. Its
/// interior spans x and z from -2.75 to 2.75, and y from 0 to 5.5.
struct CornellBox {
    /// Every part is a MakeCube.
    std::vector<CornellPart> parts;
    Light light;
    /// A camera in front of the opening, which sees the whole room.
    glm::vec3 eye{0.0f};
    glm::vec3 target{0.0f};
    float verticalFov = 0.0f;
};

[[nodiscard]] CornellBox MakeCornellBox();

} // namespace daidalos::renderer
