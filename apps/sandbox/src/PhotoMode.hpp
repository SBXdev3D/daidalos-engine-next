// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/app/Application.hpp>
#include <daidalos/ecs/World.hpp>
#include <daidalos/input/ActionMap.hpp>
#include <daidalos/scene/Scene.hpp>
#include <daidalos/ui/Canvas.hpp>
#include <daidalos/ui/Context.hpp>

#include <glm/vec3.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Hud.hpp"

namespace sandbox {

/// The game's photo mode (ADR-0077): the game stops, a free camera leaves the player's eyes, within reach of
/// them and not through walls, and a panel sets the lens, the exposure and the film, lets the path tracer
/// converge, and saves the photo, as a PNG and an EXR of its light.
class PhotoMode {
public:
    /// Meters the camera may go from the player's eyes: the game's rule, so that a photo shows no more of
    /// the level than the player has seen nearby.
    static constexpr float kReach = 12.0f;

    [[nodiscard]] bool Active() const { return m_active; }

    /// Starts at the player's eyes, looking where they look. The scene must be paused already.
    void Enter(daidalos::app::Application& app, daidalos::scene::Scene& scene, daidalos::ecs::Entity player);
    /// Gives the view back to the player, and the settings photo mode changed back to what they were.
    void Leave(daidalos::app::Application& app, daidalos::scene::Scene& scene);

    /// Moves and turns the camera. The mouse turns it while captured; H hides the panel.
    void Update(daidalos::app::Application& app, daidalos::scene::Scene& scene,
                const daidalos::input::ActionMap& actions, bool mouseCaptured, float seconds);
    /// The panel, unless hidden. True when the player asks to go back to the game.
    [[nodiscard]] bool Draw(daidalos::ui::Context& ui, daidalos::ui::Canvas& canvas, const HudAssets& hud,
                            daidalos::app::Application& app, daidalos::scene::Scene& scene);

private:
    /// Sets a cvar for the photo, keeping what it was to put back.
    void Override(daidalos::app::Application& app, std::string_view name, std::string_view value);
    void PlaceCamera(daidalos::scene::Scene& scene);
    /// Focuses at what the middle of the view shows.
    void FocusAtCentre(daidalos::scene::Scene& scene);

    bool m_active = false;
    daidalos::ecs::Entity m_camera;
    daidalos::ecs::Entity m_playerCamera;
    daidalos::physics::BodyId m_playerBody;
    glm::vec3 m_origin{0.0f};
    glm::vec3 m_position{0.0f};
    float m_yaw = 0.0f;
    float m_pitch = 0.0f;
    float m_roll = 0.0f;
    float m_focalLength = 28.0f;
    std::size_t m_aperture = 0;
    float m_focus = 3.0f;
    float m_exposure = 0.0f;
    float m_grain = 0.0f;
    float m_vignette = 0.0f;
    bool m_pathTraced = false;
    bool m_panelHidden = false;
    /// Cvars changed for the photo: their values before, or nothing when they had their default.
    std::vector<std::pair<std::string, std::optional<std::string>>> m_saved;
    std::string m_status;
    float m_statusSeconds = 0.0f;
};

} // namespace sandbox
