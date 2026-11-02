# ADR-0018: Physics with Jolt and virtual characters

- **Status:** Accepted, amended 2026-11-02
- **Date:** 2026-11-01

## Context

Phase 6 needs a player who walks through the level, doors that block the way, and raycasts for weapons and zombies. The game has no rigid props or ragdolls planned. The feel of movement matters more than the simulation: a first-person shooter should stop at once and climb steps cleanly, which a rigid capsule pushed by forces does not do.

## Decision

- **Library:** Jolt, version 5.6 from vcpkg. It is hidden in the `physics` module, whose public headers show no Jolt type.
- **Ownership:** each `scene::Scene` owns a `physics::PhysicsWorld`, so restarting a level also resets its physics.
- **Characters:** `JPH::CharacterVirtual`, moved directly by the game once per fixed step. Rays hit them through an inner body (amended 2026-11-02, see below). Gravity belongs to the game, because a character on a ladder has none.
- **Bodies:** static meshes for the level and kinematic boxes for doors. Dynamic bodies wait for a feature that needs them.
- **Fixed step:** physics runs at the 60 Hz of `FrameTimer`. The fixed systems run first, then `SyncKinematicBodies` moves bodies to their entities, then the world steps. Frame systems draw the player between two steps with `Scene::FixedAlpha`.
- **Threads:** Jolt runs on the main thread, with its single-threaded job system (ADR-0011). A few characters and doors do not need more.
- **Geometry:** `renderer::Model` keeps a CPU copy of its triangles, because the level's collision is its visible geometry.

## Consequences

- Stairs, slopes and sliding along walls come from Jolt, and game code stays small.
- Character velocity is measured from the distance really moved, because Jolt only keeps the requested velocity. Without that, a jump would keep pushing against a ceiling.
- Models use more memory, about the size of their positions and indices. Level geometry is small, and a later model format could keep this only for what collides.
- Moving to multi-threaded physics means passing a job system adapter to `PhysicsWorld`, with no change to its users.

## Amendment (2026-11-02): rays hit characters

Weapons must hit zombies, and zombies must see the player. Each character now has a Jolt inner body, the same capsule, which follows it and changes with its height when it crouches. Inner bodies live in a layer of their own that collides with nothing, so characters still push each other only through `CharacterVsCharacterCollisionSimple`, and never through each other's bodies. `PhysicsWorld::CharacterBody` returns a character's body, and `CastRay` takes a body to ignore, so a character looking or shooting does not hit itself. `BodyCount` does not count inner bodies.

Testing rays against capsules in game code was the alternative: it needs no physics change, but each query would have to merge two answers, the world's and the characters', and walls would not hide characters unless both were compared.
