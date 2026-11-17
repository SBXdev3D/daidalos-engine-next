# ADR-0058: The flashlight

- **Status:** Accepted
- **Date:** 2026-11-17

## Context

Phase 11 step 3 (ADR-0041) asks for the player's flashlight to read as a real torch: a shaped beam (IES or cookie), sway tied to the camera's motion, a small offset from the eye so its shadows read, and hooks for battery, flicker and failure. The sandbox's flashlight was a spot light of 13 to 22 degrees in the left hand, aimed at a point 8 m ahead, switched by F.

## Decision

- **The beam is a profile and a lens.** `flashlight.ies`, written by `scripts/make_light_profiles.py`, holds a hot spot within 5 to 9 degrees, a bright ring at 10.5 degrees from the reflector's edge, and a spill to 28 degrees; `flashlight_cookie.png` lays faint rings and two smudges over it (ADR-0057). The spot's cone (26 to 30 degrees) only bounds the beam; the profile shapes it. Its candela at the center stay what they were.
- **It stays in the left hand,** 30 cm left of and 15 cm below the eye, aimed at a point 8 m ahead, so the shadows it casts fall beside what it lights and read as shadows.
- **It is held, not bolted to the camera** (`SwayFlashlight`): a turn of the camera leaves the beam behind by as much (at most 0.14 rad), and a critically damped spring brings it back in about a third of a second, without overshoot; walking adds a sway of a step's rhythm, which eases in and out with the speed so the beam rests where it aims when the player stops.
- **A battery** (`sandbox.flashlight_battery`, 900 s by default, 0 for none) drains while the light is on. Below 15% the beam dims and buzzes, more as it runs down; empty, it is dark. Picking up a second flashlight is a fresh battery. The HUD shows the charge under the flashlight's icon, red when low.
- **A failure hook,** `FailFlashlight(player, seconds)`: the beam stutters and dies at random for that long, on the failing pattern of ADR-0057, for scripted scares (Phase 13).
- **The logic is a module of its own,** `apps/sandbox/src/Flashlight.cpp`, with unit tests that need no window (`daidalos_sandbox_tests`).

## Consequences

- The beam lags the view: aiming with the crosshair at the edge of the lit spot after a fast turn takes a third of a second. That is the point of it; the crosshair, not the beam, aims the weapons.
- The battery adds a resource the factory does not place yet: batteries as pickups, and how long a level should take, are Phase 16's to tune.

## Alternatives considered

- **A cookie alone for the beam:** a texture can hold the ring and the spill too, but its edge is fixed to the cone's angle and the spill then needs a wide cone of mostly black texels; the profile carries the falloff with angle exactly, and the cookie only the lens.
- **Sway from the camera's angular velocity alone** (no spring): simpler, but the beam snaps back the moment the turn stops.
