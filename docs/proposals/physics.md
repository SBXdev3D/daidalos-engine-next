# Proposal: better physics, and a physics gallery to film

*2026-11-24. Approved on 2026-11-24 as Phase 21. Steps 21.1 (ADR-0125), 21.2 (ADR-0126), 21.3 (ADR-0127) and 21.4 (ADR-0128) done: Phase 21 is done. Replaces `explosion-forces.md`, which becomes step 21.3. Asked for after the effects gallery (ADR-0122): explosions that throw things, physics in general, then a sequence of rooms with one physics theme per room, to record as a video. Sizes are guesses from reading the code.*

## Where physics is

- `PhysicsWorld` (ADR-0018, ADR-0073) hides Jolt. It has static meshes, kinematic boxes (doors, lifts), characters, and **dynamic boxes only**: friction 0.6 and restitution 0.1 for every body, an impulse through the center of mass, and nothing else. Jolt runs single-threaded at 60 Hz, which keeps it deterministic, and recordings depend on that (ADR-0123).
- A level's `body` component is a box's size, center and mass. The factory has six crates, and none of the gallery's props has a body.
- **Nothing hears a collision.** There's no contact listener, so a crate that falls on the floor makes no sound and raises no dust. On film, that would be the first thing anyone notices.
- **No joints**, so nothing can swing, hang, hinge or snap.
- **No blast force** and no sequence cue that touches physics. The `move` cue moves entities, not bodies.

## Plan: Phase 21, four steps

### 21.1 Shapes, materials, contacts (ADR-0125)

`PhysicsWorld`:
- **Shapes:** box, sphere, cylinder, capsule, and a **convex hull** of the model's mesh for props that no primitive fits (cart, generator, tool chest), with at most 64 points, built when the level loads. The level's `body` gets `"shape": "box" | "sphere" | "cylinder" | "capsule" | "hull"`. It defaults to box, so current levels load unchanged.
- **Materials per body:** friction, restitution, linear and angular damping, and a **surface** (metal, wood, plastic, concrete, cloth...), the same names as footsteps and impacts (ADR-0067). Defaults come from the surface, so a level writes `"surface": "metal"` and gets plausible numbers.
- **Impulse at a point** (bodies pushed off-center spin), and a **sphere overlap query** for the dynamic bodies around a point.
- **Continuous collision** (Jolt's `LinearCast` motion quality) for small, fast bodies, so blast debris doesn't go through walls.
- **Contact events:** a contact listener collects each new contact's bodies, point and approach speed during `Step`, and the scene reads them afterwards. It also reports sliding and rolling contacts with their speed.
- **Debug view:** body shapes, contacts and sleeping bodies drawn as lines (`physics.debug_draw`), in the game and the editor.

Scene:
- **Impact sounds:** the surface's impact clips (ADR-0067) at the contact, with volume and pitch from the approach speed, and a short cooldown per body pair so that a resting crate doesn't buzz.
- **Rolling and scraping loops:** a loop per body that rolls or slides, with volume from the speed, made by a new `scripts/make_physics_sounds.py` in the same way as the surface sounds.
- **Dust:** a small puff (an existing effect) when something heavy lands hard.

Editor: the shape list, **Fit to model** (sizes the shape from the model's bounds), surface and material fields, and the debug view in the viewport.

### 21.2 Joints (ADR-0126)

- **Kinds:** `hinge` (doors, lids, a see-saw, a lever), `point` (a ball joint: chains, a hanging lamp), `distance` (a rope, with a min/max, which can go slack), `fixed` (glued until it breaks), and `slider` (a drawer, a piston).
- **Options:** limits (hinge angles, slider travel), a **motor** (a hinge turning at a speed, a slider driven to a position), and a **break force**. Past that force the joint snaps, with a sound (metal snap or rope snap by surface).
- **In the level:** a `joint` component on the moving entity, naming the other entity, or nothing for the world, and giving the anchor and axis in the entity's frame. **Chains** are a `chain` helper in the level script, with N links of point joints and a link model (a generated mesh), not a new engine type.
- **Lights on bodies:** a light under a loose object follows it, so a lamp swinging on a chain sweeps its light and its shadow through the room. I need to check whether the light hierarchy already follows a moved entity; if it doesn't, this is the fix.
- **Editor:** the joint section, with the anchor and axis as gizmos and a line to the other entity. Simulate (ADR-0110) shows the joint working.

### 21.3 Forces and cues (ADR-0127: the explosion proposal, extended)

- **`Scene::Blast(center, radius, strength)`**, as `explosion-forces.md` describes it. Strength falls off with distance, lifted by 0.3 up, capped at 25 m/s, applied at the nearest point plus a seeded offset, and scaled to 0.2 when a wall is in the way. It also breaks joints whose force goes past their limit, so a shelf held by fixed joints comes apart.
- **New sequence cues:**
  - `blast` (entity, radius, strength)
  - `impulse` (entity, a vector, optionally at a point): a kick, a thrown crate
  - `release`: a body held still until now starts to fall
  - `break`: snap a joint, such as cutting a rope
  - `motor`: set a joint motor's speed or target
- **Held bodies:** `"held": true` keeps a body still and kinematic until a `release` cue, so crates can wait at the ceiling for their cue.
- **Conveyor surfaces:** a static or kinematic body with a surface velocity (Jolt contact settings), which carries what lies on it. It's cheap, and it gives a room of its own.
- **In the factory game:** shots push loose objects (the hit direction times the gun's impulse, at the hit point). Explosive barrels stay for later, as the old proposal says, because they need damage to characters.
- **The effects gallery's explosion room:** its props get bodies and real masses, and each blast gets a `blast` cue, as the old proposal says.

### 21.4 The physics gallery (ADR-0128)

`scripts/make_physics_gallery.py` writes `Factory/PhysicsGallery` and `Sequences/physics_tour.sequence.json`, using the effects gallery's room, shot, fade and `--check` helpers, which move into a shared `scripts/gallery.py`. Same rules as before: no titles, room sounds and the bodies' own sounds, no music, black holds between rooms, recorded with `record.directory`.

Rooms, about 15 to 20 s each, about 4 minutes in all:

| # | Room | What happens |
|---|---|---|
| 1 | **Swinging** | Caged lamps on chains, kicked one after another. Their lights and shadows sweep the walls. A heavy hook on a chain swings through the frame. |
| 2 | **Falling** | Crates, cement bags and a jerrycan held at the ceiling are released onto a stack of crates and barrels below, which gives way. |
| 3 | **Rolling** | Barrels and gas bottles roll down a ramp and across the floor, knocking over stools and a wet-floor sign. Rolling sounds carry it. |
| 4 | **Explosions** | Three blasts in a room of tanks, barrels, crates and a cart. Things fly, spin and pile up differently after each blast, and a crate wall is blown apart. |
| 5 | **Wrecking ball** | A ball on a chain, released from high, smashes through a wall of crates. |
| 6 | **Dominoes** | A line of planks curves around the room, and the last one tips a shelf. |
| 7 | **Materials** | The same drop for a plastic crate, a steel tool chest and a rubber ball, then the same push on a dry floor and an oily one: mass, bounce and friction side by side. |
| 8 | **Joints and breaking** | A door swings on its hinge. A sign hangs on two chains until one snaps and it swings on the other. A shelf glued together comes apart under a blast. A see-saw throws a crate. |
| 9 | **Machines** | A conveyor carries boxes off its end, a piston (slider motor) shoves crates off a ledge, and a motorised hinge turns a paddle wheel that bats them about. |
| 10 | **Chain reaction (finale)** | A ball rolls down a ramp, knocks over dominoes, tips a see-saw, a crate drops on a gas bottle, and the bottle blows: every feature in one take. |

New models, generated by script like `make_crate.py` and textured with the factory's materials: a plank (domino), a ball (rubber and steel), a chain link, a hook, a ramp and a see-saw board.

Before I call any room done, I'll check its run for **repeatability**: two recordings of the same room end with the bodies in the same places. Both the video and the tests depend on this.

## Not in this phase, and why

- **Ragdolls:** the zombie has no collision shapes or joint limits (ADR-0073). Joints from 21.2 make them possible later, as their own step.
- **Cloth and soft bodies:** these need meshes that deform every frame, with a BLAS rebuild per frame, which is a renderer change. They're worth their own proposal.
- **Multi-threaded Jolt:** it would cost determinism. I'll measure the busiest room first; a few hundred bodies at 60 Hz should fit in a single thread.
- **Fracture** (crates breaking into pieces): this needs pre-cut models. A crate that snaps apart can be faked with fixed joints in 21.2.
- **Zombies pushing crates:** navigation doesn't know about loose objects. That's a gameplay change, not something to film.

## Tests

- **`PhysicsWorld`:** each shape falls and rests; a cylinder rolls down a slope where a box slides; restitution bounces and friction stops; an off-center impulse spins; a contact reports its speed; a joint holds, its limit stops it, its motor turns it, and it breaks past its force; a sphere query finds only what is inside.
- **`Scene`:** blast falloff, shielding and radius; a held body stays until released; an impact plays a sound once, not every step; two runs from one state end the same.
- **Level and sequence files:** shapes, materials, joints and the new cues survive a round trip, bad values are errors, and an old `body` loads as a box.

## Size

Large, about the size of Phase 20's biggest steps put together. 21.1 and 21.2 are the engine work, 21.3 is moderate, and 21.4 is mostly the level script plus the time it takes to tune every room by eye.
