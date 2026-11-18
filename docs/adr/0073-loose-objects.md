# ADR-0073: Loose objects, and what Phase 13 leaves for later

- **Status:** Accepted
- **Date:** 2026-11-18

## Context

Phase 13 (ADR-0041) lists physics (picking up, dragging and throwing objects, leaning, ragdolls blended with animation, cloth through Jolt's soft bodies), animation (layered state machines, additive layers, foot and hand IK, look-at, morph targets, the player's body in first person) and portal visibility and profiling "if measurements justify them", as things the phase may include. Its last completion criterion: added complexity provides measurable or functional value. The physics world had static meshes, kinematic doors and characters, and no gravity: nothing could be knocked over.

## Decision

- **Dynamic bodies:** `PhysicsWorld::AddDynamicBox` (a box of a mass, friction 0.6, a little bounce), with its rotation, velocity, mass, impulses and a teleport for saved games. The world now has the Earth's gravity, which only dynamic bodies feel: characters are virtual and the game moves them.
- **Loose objects in levels:** a level entity's `body` component (a box's size and center, and a mass) makes a dynamic body where the level puts it; the scene puts the entity where physics moved it after each step, and the body goes with the entity. The editor edits it ("Loose object"). A saved game puts bodies back where they were (ADR-0072).
- **The player's hands:** interact picks up the loose object in front, within 2.2 m and up to 40 kg, which then follows a point 1.3 m in front of the eyes, pulled there by its velocity, so that walls stop it and it drops when it catches; interact again drops it, firing throws it at 9 m/s, with a noise zombies hear (ADR-0070), and the gun waits for the trigger to come up.
- **The factory** has six loose crates, near the start and in the corridor to the rain room, a 60 cm box that `scripts/make_crate.py` writes with the factory's crate textures.
- **Left for later, with why:**
  - *Ragdolls blended with animation:* the zombies' skeleton has no collision shapes or joint limits to make a Jolt ragdoll from, and a death that falls on the spot reads well in the factory's corridors. When a model ships with them, a ragdoll is Jolt's `Ragdoll` driven by the animation's pose.
  - *Cloth:* nothing in the factory is cloth.
  - *Leaning:* the factory's corners are open enough to peek by stepping; the keys are taken.
  - *Animation layers, IK, look-at, morph targets, the player's body:* the zombies' clips play whole (ADR-0024), the player has no body model, and no asset has morph targets. Each comes with the first asset that needs it.
  - *Portal visibility:* the factory draws 84 to 90 draws a frame; culling them is not where the frame's time goes (ADR-0052's measurements: lighting is).

Tested: a box falls and comes to rest on a floor, with its mass; a thrown box flies the way it was thrown, and a teleport puts it where asked (`PhysicsWorld`); a loose object's level entry survives a round trip; a level entity with a body falls, and its body goes when the entity does (`ALooseObjectFallsAndGoesWithItsEntity`).

## Consequences

- Phase 13 is done: navigation (ADR-0069), perception and utility AI (ADR-0070), triggers and sequences (ADR-0071), saves (ADR-0072), loose objects (ADR-0073).
- Loose objects are boxes: a barrel rolls like a box. Other shapes (spheres, capsules, convex hulls from the mesh) come when a level needs them.
- Zombies do not push crates out of their way: the navigation mesh does not know them, and a zombie walks into one until it slides off.

## Alternatives considered

- **Carrying by a constraint** (a spring joint to an invisible kinematic body): truer collisions while carried, but a joint to tune; setting the velocity towards the hands does the same at these masses.
