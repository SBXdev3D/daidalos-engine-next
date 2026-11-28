# Proposal: explosions that throw loose objects

*2026-11-23. Superseded by `physics.md` (Phase 21), whose step 21.3 does it. Asked for after the effects gallery (ADR-0122): the explosion room's blasts move nothing. Estimates are from reading the code.*

## What is there

- **Loose objects** (ADR-0073): a level entity's `body` component (a box's half extents, its center, a mass) makes a dynamic Jolt box, which falls, slides and tumbles; the scene puts the entity where physics moved it after each fixed step. The editor edits it ("Loose object"). The factory has six.
- **`PhysicsWorld::AddImpulse(body, impulse)`**, through the center of mass. Nothing calls it: the player's hands set velocities (`apps/sandbox/src/Factory.cpp`).
- **Sequences** (ADR-0071) time the gallery's blasts: flare, fireball, sparks, debris, shock, smoke, sound and shake cues on a marker entity per blast.

## What is missing

1. **No blast force.** Nothing pushes the bodies around a point, less with distance and not through walls.
2. **No cue for it.** A sequence cannot ask for one.
3. **Only boxes.** Barrels and gas bottles would fly as boxes and not roll.
4. **The gallery's props are fixed.** None of the 17 in the explosion room (nine propane tanks, three barrels, two crates, two cement bags, a storage cart) has a `body`.

Shots do not push bodies either; that is a separate change, which the blast's impulse at a point would serve.

## Proposed

### Physics

- **`PhysicsWorld::AddImpulseAt(body, impulse, point)`**: Jolt's `AddImpulse(id, impulse, point)`, so that a body pushed off its center spins.
- **Cylinders**: `AddDynamicCylinder(halfHeight, radius, mass, transform, center)` (Jolt's `CylinderShape`, along the body's Y); the level `body` component gets `"shape": "box" | "cylinder"`, box by default so that current levels load unchanged, and the editor a choice of shape.
- **Bodies within a sphere**: a broad-phase query (Jolt's `CollideShape` with a sphere, or the broad phase's `CollideSphere`) for the dynamic bodies around a point, instead of looping over every body.

### Scene

- **`Scene::Blast(center, radius, strength)`**: for each dynamic body within the radius,
  - the direction from the center to the body, lifted a little (a share of up, say 0.3), so that things leave the floor;
  - an impulse of `strength * (1 - distance / radius)^2` kilogram-meters per second, capped by mass so that a light thing does not reach absurd speeds (a top speed, say 25 m/s);
  - applied at the point of the body nearest the blast, plus a small random offset from a fixed seed, so that it spins and a recording repeats;
  - shielded: a ray from the center to the body's center that hits a static body first scales the impulse down (to 0.2 rather than 0, as a blast wraps around corners).
- Woken bodies: Jolt's impulses wake them.
- Saved games already keep bodies where they lie (ADR-0072); nothing to add.

### Sequence cue

- `{"blast": {"entity": "Explosion: blast A", "radius": 6, "strength": 400}}`, at the entity's position; parsed, written and validated as the other cues (radius > 0, strength >= 0); the editor's sequence panel offers it with a radius and a strength.

### Gallery

- The explosion room's props get bodies of real masses: propane tank 15 kg (cylinder), barrel 20 kg empty (cylinder), crates 10 to 20 kg, cement bag 25 kg, storage cart 30 kg, sized from their models' bounds by `scripts/make_effects_gallery.py`.
- Each blast gets a `blast` cue with its flare. Props must not start inside a blast's radius core, or they fly through the camera: placed by the script, checked by `--check`, which learns to keep the shots clear of where things land (the landing spots of a recorded run, or a margin around the props' reach).
- Three blasts in the same room: the props pile up differently after each, which is what a viewer expects.

### Later, not in this step

- Explosive barrels in the factory game: a body that, shot or burnt, blasts (fire, damage to characters within the radius, which `Combat` would take).
- Shots pushing loose objects: the hit's direction times the gun's impulse, at the hit point.

## Tests

- `PhysicsWorld`: an impulse at an off-center point turns the body; a cylinder rolls down a slope where a box slides.
- `Scene::Blast`: a nearer box flies faster than a farther one of the same mass; a box behind a wall barely moves; a box beyond the radius does not move; two runs from the same state end the same.
- `Sequence`: a blast cue round-trips, and a negative radius is an error.
- A level `body` with `"shape": "cylinder"` round-trips; one without a shape is a box.

## Size

Moderate: physics (impulse at a point, cylinders, a sphere query), scene (the blast, the cue), the level file and the editor (the shape, the cue's fields), the gallery script, an ADR, the docs. No renderer change.
