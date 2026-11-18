// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <daidalos/core/Result.hpp>
#include <daidalos/ecs/World.hpp>
#include <daidalos/input/ActionMap.hpp>
#include <daidalos/input/InputState.hpp>
#include <daidalos/scene/Scene.hpp>

#include <string>
#include <string_view>

#include "Factory.hpp"

namespace sandbox {

// Weapons, zombies and wounds: the fighting of the demo (docs/DEMO.md). Zombies go straight for the
// player they see, with no path finding.

/// Loads the sounds of the fight, and puts the weapons in the player's hands. Their models are found
/// in the level's directory. Missing models leave the weapons invisible, with a warning.
void SetUpCombat(daidalos::scene::Scene& scene, daidalos::ecs::Entity player, const std::string& directory);

/// Makes a zombie of a level node standing on the floor, with its model as a child. patrol is the
/// legacy list of points to walk between, "x:y:z|x:y:z" in legacy units from where it stands.
void AddZombie(daidalos::scene::Scene& scene, daidalos::ecs::Entity node, daidalos::ecs::Entity model,
               std::string_view patrol);

/// Gives the player the shotgun, loaded. A second one only brings its shells.
void GiveShotgun(Player& player);
/// Adds rounds to what the player carries for a weapon, up to what can be carried.
void AddAmmo(Player& player, WeaponKind weapon, int rounds);

/// Firing, reloading and switching, on every frame, so that no press is missed.
void UpdateWeapons(daidalos::scene::Scene& scene, daidalos::ecs::Entity player,
                   const daidalos::input::InputState& input, const daidalos::input::ActionMap& actions, float delta);

/// What the zombies see, decide and do, on every fixed step.
void UpdateZombies(daidalos::scene::Scene& scene, daidalos::ecs::Entity player, float step);

/// Draws the zombies between their last two fixed steps.
void PlaceZombies(daidalos::scene::Scene& scene);

/// How tense the moment is (ADR-0068), from 0 to 1, and how many zombies hunt the player: from the zombies
/// chasing the player, the nearer the tenser, and those waiting nearby.
struct Threat {
    float tension = 0.0f;
    int hunting = 0;
};
[[nodiscard]] Threat ZombieThreat(daidalos::scene::Scene& scene, const glm::vec3& feet);

} // namespace sandbox
