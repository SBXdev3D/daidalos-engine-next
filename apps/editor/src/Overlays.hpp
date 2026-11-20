// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/renderer/Renderer.hpp>
#include <daidalos/scene/Level.hpp>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Picking.hpp"

namespace editor {

/// What an entity's marker stands for, which the Show menu turns on and off (ADR-0106).
enum class Category : std::uint8_t { Lights, Effects, Volumes, Triggers, Rooms, Decals, Game, Models, Other };
inline constexpr std::size_t kCategoryCount = 9;

[[nodiscard]] Category CategoryOf(const daidalos::scene::LevelEntity& entity);
[[nodiscard]] std::string_view CategoryName(Category category);

/// What the viewport draws over the scene, and what the scene leaves out (ADR-0106).
struct ShowFlags {
    /// Markers and boxes, by category.
    std::array<bool, kCategoryCount> categories{true, true, true, true, true, true, true, true, true};
    bool grid = true;
    /// Every light's reach, not only the selected one's.
    bool lightReach = false;
    /// The physics boxes of entities with a body.
    bool collision = false;
    /// Where walkers can go, built from the level's triangles on request.
    bool navigation = false;
    /// The openings between rooms, where their boxes overlap.
    bool portals = false;
    /// The scene's own parts the editor can leave out of the view.
    bool decals = true;
    bool particles = true;
    bool fog = true;
    /// Only the game's image: no grid, markers, boxes or gizmo (G).
    bool gameView = false;

    [[nodiscard]] bool Shows(Category category) const {
        return !gameView && categories[static_cast<std::size_t>(category)];
    }
};

/// Turns on or off what a comma-separated list names (editor.show): light_reach, collision, navigation, portals,
/// game_view, no_grid, no_decals, no_particles, no_fog. Returns the names it did not know.
std::vector<std::string> ApplyShowFlags(ShowFlags& show, std::string_view names);

/// Whether points of the world are hidden by the scene, from a depth picture the renderer read back: overlays
/// draw what is hidden faint, as if drawn in the scene's depth (ADR-0106). Without a picture, all is visible.
class DepthTest {
public:
    /// How faint what lies behind a surface is drawn.
    static constexpr float kHidden = 0.22f;

    DepthTest() = default;
    explicit DepthTest(const daidalos::renderer::DepthPicture* picture) : m_picture(picture) {}

    /// 1 where the point shows, kHidden where the scene is in front of it.
    [[nodiscard]] float Visibility(const glm::vec3& point) const;

private:
    const daidalos::renderer::DepthPicture* m_picture = nullptr;
};

/// The openings between rooms (ADR-0089): where two room boxes overlap, as the renderer finds its portals.
[[nodiscard]] std::vector<Bounds> Openings(std::span<const Bounds> rooms);

/// The world box around a unit cube from -0.5 to 0.5 placed by a matrix.
[[nodiscard]] Bounds UnitBoxBounds(const glm::mat4& matrix);

} // namespace editor
