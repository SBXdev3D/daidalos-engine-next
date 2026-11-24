# ADR-0125: Body shapes, materials and contacts

- **Status:** Accepted
- **Date:** 2026-11-24
- **Amends:** ADR-0073 (loose objects take shapes other than boxes, and a material)

## Context

The owner wants a video of the engine's physics, one theme per room (`docs/proposals/physics.md`, step 21.1 of Phase 21). Physics had dynamic boxes only, all with the same friction and bounce: a barrel slid like a crate, a sack bounced like a crate. Nothing listened to collisions, so a crate that fell made no sound and raised no dust. Explosions (21.3) need an impulse at a point and the bodies around one, and rooms (21.4) need shapes that roll and surfaces that slide.

## Decision

- **Shapes.** `PhysicsWorld::AddDynamicBody` takes a `DynamicBodyDesc`: a shape (box, sphere, cylinder, capsule or hull), a mass, a `BodyMaterial` and whether it is fast. A sphere, a cylinder and a capsule fit in the box of the shape's size, standing along Y, so that a level keeps describing every shape by the same size and center. A hull wraps the points it is given; of many, it keeps those furthest out along 64 directions spread over the sphere, so that a model of thousands of vertices makes a hull quickly and the same every time. `AddDynamicBox` stays, as a box.
- **Materials.** Friction, restitution and linear and angular damping per body, static meshes included (friction and restitution only). Jolt combines two bodies' frictions as the square root of their product and takes the larger restitution.
- **Fast bodies** are swept each step (Jolt's linear cast), for debris that would otherwise go through thin walls.
- **Contacts.** A contact listener collects, during `Step`, each new contact (bodies, the middle of the points, the normal, the speed at which they closed along it) and each contact that goes on while a body moves (how fast the surfaces slide past each other at the point: a rolling body barely slides). `PhysicsWorld::Contacts` returns the last step's.
- **Queries and pushes.** `AddImpulseAt` (a push at a point, which turns what it does not push through the center of mass), `DynamicBodiesInSphere`, `BodyAngularVelocity`, `IsSleeping`, and `Outlines`: the edges of the triangles Jolt makes of each shape near a point, for debug views.
- **In levels,** the `body` component gains `shape` (box by default, so levels written before load unchanged), `surface`, `friction`, `restitution` and `fast`. A body's surface is the level's, or its model's main surface: the one most of its triangles are made of, glass only if nothing else is (a lamp is metal with a glass shade). A surface gives friction, bounce and damping (`scene::SurfaceMaterial`), which `friction` and `restitution` override: metal slides and rings, wood holds, plastic bounces a little, a cloth sack lands dead, rubber bounces, ice barely holds. Static boxes (ADR-0083) take theirs too. A hull wraps the entity's model as its scale makes it; without a model, or if Jolt cannot make it, the body is a box and the log says so.
- **Sounds and dust** (`scene::PhysicsFeedback`, run by the scene after each physics step): a new contact closing faster than 0.35 m/s plays a hit of the surface of the heavier loose object in it, louder the faster, lower for heavier things, a heavy clip from 30 kg·m/s; one hit per body in 0.12 s and six per step at most, so that a crate settling on its corners does not rattle. A landing of at least 2.5 m/s and 60 kg·m/s raises the level's `dust_puff`. A body that touches something and moves plays a rolling loop if its contact barely slides, a scraping loop if it slides, both following its speed and weight. `Scene::LoadPhysicsSounds` loads them from a directory by name; the sandbox loads `Factory/Sounds/Physics`, which `scripts/make_physics_sounds.py` synthesises as the footsteps are (ADR-0067): hits per surface, light and heavy, and loops. Voices take a `pitch` (`audio::PlayOptions`).
- **The editor** chooses a body's shape and surface, sets friction and bounce or leaves them to the surface, sizes a shape to the model's bounds ("Fit to the model"), and its collision overlay draws the shapes physics really has, where they are while simulating (`PhysicsWorld::Outlines`): static in cyan, moving in magenta, asleep in grey.
- **Physics stays on one thread.** Jolt is deterministic there, so a recording plays the same each time (ADR-0123): the feedback's random numbers also start from a fixed seed. `TheSameStartEndsTheSame` checks it.

## Consequences

- The factory's crates are wood (their material says so): a little more bounce, and they are heard landing, sliding and when thrown. Its barrels and props remain static.
- The sounds are made by script. If they read as synthetic on a recording, recorded clips can replace the files by name.
- A hull is convex: a cart or a chair rests on the hull of its wheels or legs, not between them. Compound shapes come when a room needs them.
- The level's static meshes have no surface material in physics yet: a crate on ice needs the floor to be a static box with `"surface": "ice"`.
- Tested: each shape rests where its bottom touches the floor; a hull of too few points is an error, one of thousands keeps its size; a drum rolls down a slope where a box does not turn; ice slides where wood holds; rubber bounces where wood lands dead; an off-center push turns a body; a landing reports its speed and stops reporting at rest; a rolling ball barely slides; only dynamic bodies in the sphere are found; outlines leave characters out; the same start ends the same (`PhysicsWorld`). Shapes, surfaces and bad shapes in level files; a level's crate is wood, and its hull is made from the model (`LevelInstance`). A landing is heard once and raises dust; a gentle touch is silent (`Bodies`).
