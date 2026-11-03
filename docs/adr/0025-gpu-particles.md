# ADR-0025: Particles simulated on the GPU

- **Status:** Accepted
- **Date:** 2026-11-03

## Context

The legacy factory had smoke leaking from a pipe, green smoke and fog in the toxic area, blood on zombies, sparks on walls, and a flash and smoke at the gun's muzzle. Its particle system ran on the CPU, from templates in `default.dept`. The Phase 7 plan asked for a modern one, computed on the GPU, and the RHI had no compute shaders yet.

## Decision

- **The RHI gains compute**: compute pipelines with the same bindless layout as the others, `Dispatch`, `DrawIndirect` from a buffer the GPU writes, and a full memory `Barrier` between passes. A premultiplied blend mode joins `Opaque` and `Alpha`.
- **One pool of 16,384 particles** lives on the GPU, with a list of dead slots and two lists of alive ones that swap each frame. The CPU uploads the dead list once and never reads anything back.
- **Each frame, after the pre-pass**:
  1. A compute pass **emits** the frame's new particles. The CPU only sends one request per emitter and burst: an effect, a place, a rotation and a count. Each thread takes a slot from the dead list; when the pool is full, new particles are dropped.
  2. A pass **simulates** the alive ones: age, gravity, drag and rotation. Effects that collide bounce off the pre-pass's depth and normals: a particle that goes less than half a meter behind a visible surface is pushed out and its velocity reflected. Dead particles go back to the dead list, and the others are appended to the other alive list, with their view depth as sort key.
  3. A pass **finishes**: it writes the arguments of the indirect draw, and sort keys that sort last in the slots past the alive particles.
  4. A **bitonic sort** orders the keys back to front. Runs of 2,048 keys are sorted in each group's shared memory, and only the longer steps take a dispatch each: 10 dispatches in all.
- **After the mesh pass**, one indirect draw renders camera-facing quads from the sorted keys, into the HDR target. Particles read the depth to hide behind meshes and to fade where they meet a surface (soft particles). Lit effects take the ambient light and the lights of their cluster, without normals or shadows; the others keep their own color.
- **Premultiplied alpha** lets one draw hold both kinds of effects: smoke covers what is behind it, while sparks and flashes write an alpha of 0 and add their light.
- **Particles fade in over the first tenth of their life**, so that a dense stream does not start with the hard outline of stacked quads.
- **Effects are C++ data**: `renderer::ParticleEffectDesc` gives a texture cell, colors over life, size and growth, spawn volume, direction and spread, speed, gravity, drag, rotation, softness, collision and bounce. `scene::ParticleEmitter` emits one at a steady rate from an entity, turned like it, and `Scene::SpawnParticles` adds one-shot bursts, such as impacts.

## Consequences

- At 1280×720 in Release on an RTX 3090, the simulation costs 0.33 to 0.55 ms whatever the number of particles, since it covers the whole pool; drawing costs from 0.01 ms to 0.5 ms in the toxic fog, where large puffs overlap.
- The pool holds 16,384 particles instead of the 65,536 first planned: sorting the whole pool every frame stays cheap, and the factory needs far fewer.
- Collisions only know what is on screen: particles pass through walls behind the camera or hidden behind something else.
- Lit particles are lit evenly from every side, and they cast no shadow and receive none.
- Effects live in code, not in data files: a level names its emitters' effects, and the game maps the names to its effects.

## Alternatives considered

- Particles on the CPU, as in the legacy engine: simple, and enough for the factory, but the plan asked for compute, and the RHI needed it anyway.
- Sorting only the alive particles, with an indirect dispatch: less work, but a bitonic sort needs a power of two, and the CPU does not know the count; the whole pool costs a fixed and small time.
- No sort, with additive blending only: no cost, but smoke would glow instead of covering what is behind it.
- Order-independent transparency (weighted blended): no sort, but smoke takes the color of what is in front of it at thick spots.
- Collisions against the physics world: exact, but the physics lives on the CPU, and particles live on the GPU.
- Effects in data files: easy to tweak without a build, but the engine has no format and no editor for them yet.
