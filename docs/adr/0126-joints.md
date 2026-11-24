# ADR-0126: Joints

- **Status:** Accepted
- **Date:** 2026-11-24
- **Amends:** ADR-0073 (loose objects can be held to each other and to the world), ADR-0125 (snaps join the physics sounds)

## Context

Step 21.2 of Phase 21 (`docs/proposals/physics.md`). The physics gallery's rooms need things that swing (lamps on chains, a wrecking ball), hinge (doors, a see-saw), hang on ropes, come apart (a glued shelf, a sign whose chain snaps) and are driven (a paddle wheel, a piston). Physics had no joints.

## Decision

- **Five kinds** (`physics::JointKind`), Jolt's constraints behind `PhysicsWorld::AddJoint`:
  - `point`: a ball joint;
  - `hinge`: turns about an axis only;
  - `distance`: keeps two ends between a shortest and a longest distance, so a rope goes slack;
  - `fixed`: glue;
  - `slider`: slides along an axis only.
  A joint holds a dynamic body to another body (dynamic or static), or to the world. It is described in world space, where the bodies are now.
- **Limits:** angles either side of where a hinge starts, travel along a slider, a distance joint's range. **Friction** on hinges and sliders. **Motors** drive a hinge or a slider to a speed or a position, up to a strength. Angles, positions and motors are those of the first body along the axis as given: Jolt measures the second body from the first, so it is given the opposite axis.
- **Breaking:** after each step, a joint's pull is the impulse its position part needed divided by the step. Past its break force, in newtons, it is turned off for good and reported (`BrokenJoints`). `BreakJoint` breaks one on demand: 21.3's `break` cue will use it.
- **Jointed bodies pass through each other**, by a contact validation that rejects their pairs. Links of a chain overlap where they meet, and a door overlaps its frame. A broken joint's bodies collide again.
- **Islands with joints are solved harder:** 30 velocity and 10 position steps against Jolt's 10 and 2. Light links under a heavy lamp still stretch: ten 0.3 kg links under 10 kg reached 1.41 m for 1.2 m. So a chain is made with a **hidden rope**, a distance joint as long as the chain from the hook to the lamp, which keeps it within 5%. The gallery's chain helper (21.4) adds it. Chains are links and joints in the level, not an engine type.
- **In levels,** a loose object's `joints` list each have:
  - a `kind`;
  - `to`, the level id of what it holds to: 0 is the world, and so is an entity without a body. An entity that is not there makes no joint, so a chain whose link was removed falls;
  - an `anchor`, an `axis`, and for a distance joint a `to_anchor` in the other entity's frame (or the world's);
  - `limits` (degrees for a hinge), `friction`, a `motor` and `break_force`.

  `LevelInstance` makes every joint again after each build or sync, between the bodies as they are, so that an edit is never made twice. The editor's copy and paste keeps joints between copies.
- **Lights on loose objects** follow them: a light entity under a lamp's body hangs from it as it swings, as `ALampHangsOnItsChainAndItsLightGoesWithIt` checks. Nothing had to change.
- **Sounds:** a breaking joint plays the snap of the surface of the body it held: a pinging link for metal, a crack for wood and plastic, a tear for cloth (`scripts/make_physics_sounds.py`).
- **Saved games** keep which joints broke. Bodies are put back where they were, as before (ADR-0072).
- **Editor:** a Joints section for loose objects (kind, what it holds to by id, anchor, axis, limits, friction, motor, break force), added with "Add a component". The collision overlay draws each joint where physics has it, as a yellow cross with a line to its other end.

## Consequences

- A saved game puts a jointed body back where it was, and its joints pull it to where they hold it: a lamp saved mid-swing may jerk on load.
- Joints are re-made at the bodies' current poses on each sync. In the editor that is the level's pose, since physics is paused until it simulates.
- Breaking reads the position part of a joint only: a fixed joint twisted without being pulled does not break.
- Tested:
  - `PhysicsWorld`: a joint needs a dynamic first body and two bodies; a hanging weight swings and keeps its length; a door stops at its limit; a rope goes slack then catches; glue breaks under too much weight, once; a broken joint lets go; motors turn and slide; a chain holds a lamp without stretching; removing a body removes its joints.
  - Level files: joints round-trip, with degrees in the file; bad joints are errors.
  - `LevelInstance`: a lamp hangs on its chain and its light goes with it; joints are not made twice, and a removed link takes its joints; a broken joint stays broken in a saved game.
  - `Bodies`: a breaking joint snaps.
