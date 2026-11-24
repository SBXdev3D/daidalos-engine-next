# ADR-0127: Blasts, physics cues, held bodies and conveyors

- **Status:** Accepted
- **Date:** 2026-11-24
- **Amends:** ADR-0071 (sequences get five physics cues), ADR-0122 (the gallery's explosion room throws its props)
- **Replaces:** `docs/proposals/explosion-forces.md`

## Context

Step 21.3 of Phase 21 (`docs/proposals/physics.md`). The effects gallery's blasts moved nothing. The physics gallery (21.4) needs blasts, crates held at the ceiling until their cue, a sign whose chain is cut, a wheel and a piston switched on, a conveyor, and things pushed on cue. In the game, shots did not move loose objects.

## Decision

- **`Scene::Blast(center, radius, strength)`.** It finds the dynamic bodies within the radius (`DynamicBodiesInSphere`) and pushes each away from the center, lifted by 0.3 up.
  - **Strength:** `strength × (1 − distance / radius)²` kilogram-meters per second, at most what gives the body 25 m/s.
  - **Spin:** the push lands up to 15 cm off the body's center, from a seed of the scene's, so things spin and a recording repeats.
  - **Walls:** a static body between the center and the body leaves a fifth of the push, since a blast wraps around corners.
  - **Joints:** they break when the push pulls them past their force (ADR-0126).
- **Five cues** in sequences, with the entity named as the other cues name it. The four that act on one body look for it among loose objects, since a model's node can have the same name.
  - `blast`: entity, radius, strength;
  - `impulse`: entity, impulse in the world, and a point in the entity's frame. It also lets a held body go;
  - `release`: entity;
  - `break`: entity, the joint's place in its list;
  - `motor`: entity, joint, mode (speed, position or off), target, strength.

  The editor's sequence panel offers them with their fields, and the map check knows they name entities.
- **Held bodies** (`body.held`) wait as kinematic bodies that can turn dynamic (Jolt's `mAllowDynamicOrKinematic`). What falls on them rests on them. `release` or `impulse` lets them fall, and a saved game remembers that they were let go. They can have joints.
- **Conveyors** (`body.conveyor`, meters per second in the entity's frame, on a static box): a contact with the box gets that surface velocity (Jolt's `mRelativeLinearSurfaceVelocity`), so what lies on it is carried while the box stays put. When the belt stops, what it carried slides to a stop on the belt's friction. `PhysicsWorld::SetSurfaceVelocity` sets it on any body and wakes what lies near it.
- **Shots push loose objects** at the point they hit, by 2 kg·m/s per point of damage, split between pellets. The handgun (10) sends a 10 kg crate off at 2 m/s, the rifle (50) at 10 m/s. That is far more than a real round's momentum, so that a shot crate is seen to move.
- **The effects gallery's explosion room** (`scripts/make_effects_gallery.py`):
  - Its props are loose objects of their real weight: bottles 15 kg and drums 20 kg as cylinders, crates 15 and 25 kg, sacks 25 kg, and the cart as a 40 kg hull.
  - Each blast has a `blast` cue of 6 m and 300 × its strength.
  - A recording of the room shows the bottles knocked over, rolled and scattered, a different layout after each blast, and nothing thrown at the camera or through a wall.

## Consequences

- A blast reads the distance to a body's origin, not its nearest point: a long body is pushed as if it were at its origin.
- The exploding bottles are thrown, not destroyed: there are no broken pieces (fracture is out of Phase 21).
- `Named` still finds the first entity of a name for the other cues.
- Tested:
  - `PhysicsWorld`: a held body waits until released, holds what falls on it, and has its mass when let go; a conveyor carries what lies on it, not what lies beside it, and its load slides to a stop when it stops; a held body can have joints.
  - Sequences: the physics cues parse and round-trip, and bad values name what is wrong.
  - `LevelInstance`: a blast throws nearer things faster, nothing beyond its reach, a fifth of it behind a wall, and adds at most 25 m/s; a sequence pushes, releases at its time, breaks and drives.
