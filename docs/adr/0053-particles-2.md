# ADR-0053: Particles 2.0, the effects of an indoor horror game

- **Status:** Accepted
- **Date:** 2026-11-15
- **Amends:** ADR-0025 (effects become data files; collisions against the acceleration structure instead of the screen; the GPU spawns particles and reports hits to the CPU; a larger pool), ADR-0041 (particles get their own steps, 9.19 to 9.23, before the Phase 10 editor)

## Context

ADR-0025 built particles for the legacy factory's effects: smoke from a leak, green smoke and fog, blood, sparks, the muzzle's flash and smoke. Since then they take real lights (ADR-0044) and sit in the volumetric fog (ADR-0051). What an effect can do is still what those seven needed:

- One fixed cell of an atlas per effect: nothing animates.
- Color, opacity and size go in a straight line from birth to death.
- Every particle is a quad facing the camera: a raindrop is a dot, a spark a disc.
- Forces are gravity and drag: no wind, no turbulence, nothing that attracts.
- Collisions only know what is on screen, and the CPU never learns of them: no splash, no decal, no sound.
- Particles light nothing: a fire needs a lamp placed by hand beside it.
- Effects are C++ (`apps/sandbox/src/Factory.cpp`): the Phase 10 editor cannot place or tune them.
- 16,384 particles, a pool sized for the factory.

Fire, rain, a storm, energy beams were asked for. The roadmap already counts on what is missing: "flames are flipbooks plus a light" (Phase 11, deferred list), but there are no flipbooks.

## The effects

The list the system must make, for indoor horror. Each is built in a test room, its cost measured, and the factory gets those it has a place for.

| Group | Effect | What it needs beyond today |
|---|---|---|
| Fire and heat | Fire: flames, a burning barrel or brazier | Flipbook, curves, buoyancy, turbulence, a light, heat haze |
| | Candle and lighter flames | Flipbook or a shaped quad axis-aligned to up, a small flickering light (Phase 11's candle and lighter presets use it) |
| | Embers rising from a fire | Glow, turbulence, buoyancy, flicker over life |
| | Smoke from a fire, cigarette smoke | Turbulence, a lit flipbook, light scattered forward (backlit by the flashlight) |
| | Heat haze over fire, a hot pipe | Distortion |
| | Explosion: fireball, shock, debris, smoke | Flipbook, light, distortion, mesh debris, sub-emitters (smoke and sparks after the fireball) |
| Water | Rain past windows, under a skylight, in a courtyard | Streaks along velocity, a volume that follows the camera within the level's rain zones, collisions anywhere, splashes |
| | Drips from a ceiling or pipe | Sub-emitters: a drop, then a splash and a ripple (Phase 11 wetness) |
| | A burst pipe's stream and spray | Streaks, a cone, collisions, splashes, mist |
| | Bubbles on a toxic pool | Rising, then popping into a small burst |
| Air | Dust motes in a lamp's cone or a projector's beam | Turbulence, lit tiny particles; the beam itself is the volumetric fog |
| | Dust and debris falling from the ceiling when something heavy moves | Burst from a line or a surface, mesh debris, sub-emitter puff on landing |
| | Ash or snow through a broken roof | A camera volume within zones, wind, turbulence |
| | Breath in a cold room | A burst following the camera, inheriting its velocity |
| | Papers and leaves blown by a draft | Mesh particles, wind volumes, coming to rest |
| | Spores and toxic gas | Today's green smoke, with turbulence |
| Electricity | Sparks from a cut cable or a shorting lamp | Streaks, collisions, a brief light |
| | Electric arcs, lightning | Ribbons, a flash of light |
| | Lasers and energy beams | Ribbons |
| Combat | Impacts by surface: concrete dust and chips, metal sparks, wood splinters, glass shards, flesh | Sub-emitters, mesh particles, the surface's kind from the hit (Phase 11 decals) |
| | Shell casings | Mesh particles that bounce and come to rest, a hit reported for the sound |
| | Tracers | Streaks or ribbons |
| | Blood: spurts, mist, drops that leave stains | Collisions reported to the CPU for decals (Phase 11) |
| | Muzzle flash | A flipbook and a light the length of the flash |
| Life and the uncanny | Flies around a corpse, moths around a lamp | Attractors, mesh or tiny particles, wing flicker |
| | Wisps, a presence | Ribbons, attractors along a path, distortion; seen only in mirrors through Phase 11's visibility masks |
| Weather | A storm seen from inside | Rain above, lightning: a ribbon bolt seen through windows and a flash light on the level's directional light, a hook for thunder (Phase 12) |

Out of scope, and why:

- Simulated fire and smoke on a grid: deferred by ADR-0041; flipbooks and turbulence look close enough at a fraction of the cost.
- Rain on glass, wet surfaces, puddles: materials, Phase 11's wetness.
- Snow or water that piles up, cloth, debris that stacks: physics, not particles. Mesh particles bounce and rest; they do not stack.
- Light shafts: the volumetric fog already makes them, with shadows. Particles add the dust inside them.
- Smoke that shadows and shows in reflections: Phase 11's existing item, kept.

## Decision

Five steps of Phase 9, 9.19 to 9.23, each its own pull request, in this order, before the Phase 10 editor: the editor then places effect files from the start, rather than names that the game maps to code.

### 9.19 Effects as data, and how they look

- **Effect files.** An effect is a JSON file (`.effect.json`), read with simdjson, which fastgltf already brings. Every field of `ParticleEffectDesc` is in it, and the new ones below. A level names its emitters' effects by file, as glTF nodes `particles;<effect>` do today with a code name. Development builds reload a file when it changes. The C++ description stays, for tests. The factory's seven effects move to files and look the same.
- **Flipbooks.** An effect plays a run of the atlas's cells over its life, or at a rate, looped or not, from a random start frame; frames are blended, so that slow fire does not step.
- **Curves over life.** Color and opacity follow a gradient, and size, emission and speed a curve, each of as many keys as the file gives, blended linearly. They are baked into 256 samples each, in a buffer beside the effect table, and read by life.
- **Shapes.** A particle faces the camera (as now), stretches along its velocity on screen (rain, sparks, tracers), turns only around an axis (flames, a column of smoke), or lies in a plane of the world (ripples, splashes, a decal-like burst on a floor).
- **Emission.** Spawn shapes gain the cone, the cylinder and the line. An emitter can emit per meter it travels as well as per second, its particles spread along the way it went that frame, inherit a fraction of its velocity, and fire bursts on a schedule. Disabling an emitter starts it over, so a burst fires again when it is enabled.
- **Pool.** 65,536 particles. The sort is measured first: if sorting the whole pool costs more than 0.3 ms at 1080p on the RTX 3090, the sort covers the alive particles only, rounded up to a power of two on the GPU and dispatched indirectly, which ADR-0025 turned down when the pool was small.

As built: the simulation group (emission, simulation, finish and the sort of the whole pool) takes 0.14 ms in the factory at 1080p on the RTX 3090, against 0.11 ms with the old pool of 16,384: the sort stays over the whole pool. Spawning on the triangles of a mesh moved to step 9.23: it needs each mesh's triangles and their areas on the GPU, which only emissive meshes have today, and its users, burning objects and debris falling from a surface, come in that step. Effect files name their texture relative to themselves, give angles in degrees, and reject unknown keys; the factory's level nodes name the files, `particles;effect=smoke_leak`.

### 9.20 How they move

- **Forces.** Wind: the level's, plus boxes of wind (`scene::WindVolume`, a direction, a strength, gusts), which the fog's noise can follow. Turbulence: curl noise, by strength and scale, evolving in time. Attractors and vortices: a point or an axis that pulls or turns, on an entity. Buoyancy is a negative gravity, already possible.
- **Collisions against the acceleration structure.** A colliding particle traces its motion of the frame with a ray query against the frame's TLAS, alpha test included, and finds the triangle's normal from the vertex buffers the alpha test already reads. They hit what is behind the camera and behind other things: rain stops at a roof, a casing bounces off a floor out of view. The screen-space collisions go: one way to collide is simpler, and a ray per colliding particle is cheap on the GPUs the engine accepts (ADR-0041). A hit can bounce, stop the particle where it is (it rests), or kill it.
- **Sub-emitters on the GPU.** On death or on a hit, a particle can spawn another effect there, along the hit's normal: a drop's splash, a spark's smoke. The simulation appends these requests to a list that the next frame's emit pass reads with an indirect dispatch.
- **Hits reported to the CPU.** Effects that ask for it write their hits (position, normal, effect, the surface's material) to a ring buffer that the CPU reads two frames late, as it reads GPU timings. Phase 11's decals (blood stains, scorches) and Phase 12's sounds (casings, drips, rain on metal) use them. ADR-0025's "the CPU never reads back" ends here.

As built:

- **Drag is against the air.** An effect's `drag` now pulls its particles towards the air's velocity rather than to a stop: in still air nothing changes, and in wind the same number says how readily the wind carries them. One number instead of a second one for the wind; an effect with no drag is too heavy for the wind. The level's wind is `Atmosphere::wind`, with gusts; boxes of wind add theirs, fading at their faces like boxes of fog. Their wind turns with their entity. The fog's noise does not follow the wind yet: it moves at its own velocity, as before.
- **Attractors** pull towards a point and turn around their entity's +Y, fading to nothing at a radius; each can be kept to one effect, so that flies circle a corpse while its smoke rises.
- **Turbulence** is the curl of three gradient noises, by central differences, drifting with time: divergence free, so particles swirl without bunching up.
- **Collisions** see glass too, so rain stops at a skylight. Only impacts count, more than 0.3 m/s into the surface: a particle that slides, pressed against the floor by gravity every frame, neither reports nor spawns. A bounce too weak to leave turns into a slide, and a particle slower than that rests, and moves no more; one that sticks rests at once. A resting particle stays where it rested if the surface moves away.
- **Sub-spawns** go to one of two lists that swap each frame, up to 4,096 requests a frame. The pass that spawns them is dispatched for all of them, and threads past the count do nothing: the RHI has no indirect dispatch, and 64 empty groups cost nothing measurable. Their effect's +Y follows the hit's normal, or the dying particle's axis. An effect file names the effects of `onDeath` and `onHit` by their files; files that lead back to themselves are an error.
- **Hits** are read through the simulation's frame slots: when a slot comes round again, the GPU has finished its last frame, and the CPU reads its hits, up to 1,024, before reusing it. The same header brings back the number of particles alive, which the developer tools show.
- **Costs**, at 1080p on the RTX 3090: the factory's simulation, 786 particles, takes 0.15 ms (0.14 ms before this step); 20,765 particles that collide, swirl and ride the air take 0.23 ms.

### 9.21 Light and heat

- **Emitters that light.** An emitter can own a light: a `scene::Light` at the emitter, a sphere of a given radius, whose intensity follows a curve over the emitter's activity, times a flicker (noise of a rate and depth). It is an ordinary light for ReSTIR, flagged as moving when it flickers, so the denoiser's history stays short there. One light per emitter, not per particle: the CPU builds the lights' alias table and does not know the particles, and a fire's thousand particles are one light to the eye.
- **Smoke lit as a medium.** Lit particles scatter the light they choose with the fog's Henyey-Greenstein phase function, by an anisotropy per effect: smoke between the flashlight and the eye glows. Effects whose flipbook has six-way light maps (light from each axis, baked with the flipbook) shade each pixel with the direction to the particle's chosen light, so a column of smoke has a lit side and a dark one.
- **Distortion.** Distortion particles write a screen offset into their own target; a pass after the particles bends the frame by it, before the glass pass copies the frame. They mark the reactive mask, as other particles do.

### 9.22 Ribbons and meshes

- **Ribbons on the CPU.** Beams, arcs, lightning and tracers are few and short-lived, and built from a shape: a line, a path, or a bolt split by midpoint displacement and re-split each few frames. The CPU writes their strips into a buffer each frame, drawn in the particle pass with the particles' blending, lighting and fog, sorted by their middle. Trails behind GPU particles are left out: streaks along velocity cover sparks and tracers.
- **Mesh particles.** An effect can draw a mesh instead of a quad: casings, shards, splinters, papers, debris, flies. They are opaque, drawn into the G-buffer with an indirect draw per mesh effect, from their own list that the finish pass compacts, and lit by the deferred lighting like any surface, with motion vectors from their last position. They are not in the acceleration structure until Phase 11's item puts particles there.

### 9.23 The effects, and weather seen from inside

- **Spawning on a mesh.** Particles appear on the triangles of the emitter's mesh, drawn by area: what burns, what crumbles. Moved here from step 9.19.
- **Rain and snow zones.** Boxes in the level (`scene::WeatherZone`) where it rains or snows. An emitter follows the camera and spawns only inside the zones within its reach, so an indoor scene with one courtyard spends nothing on the rest. Particles stop at roofs by step 9.20's collisions, with a splash.
- **Lightning.** A scripted flash: the level's directional light (the moon through the windows) jumps for a few frames, a ribbon bolt shows behind the windows, and an event for Phase 12's thunder, after a delay.
- **The effects of the list above**, as files, in a test room (`effects.scene`, as the dark corridor), each measured. The factory gets fire in its furnace area, drips, sparks from a cable, dust motes, flies, casings, impacts by surface, and keeps its seven. The sandbox's weapons spawn impacts by the surface they hit.

### Budget and quality

Particles, all their passes together, cost at most 1 ms at 1080p on the RTX 3090 in the factory with its new effects, and 1.5 ms in the test room under a storm. Emitters farther than their effect's distance, or out of view with a margin, do not emit. `renderer.particle_density` scales emission rates, and the presets set it (ADR-0052): 0.5 on Low, 1 above. Every new pipeline is created at load (ADR-0052).

### Where the effects' images come from

The engine's assets are MIT or Apache (ADR-0047), and flipbooks bought or made with paid tools could not be shipped under that. The flipbooks (fire, smoke, explosions, and their six-way light maps) are made by `texture-tool flipbook`: a small offline simulation of density and temperature on a 3D grid, advected by curl noise, rendered by the path tracer's medium code to frames, and encoded with ADR-0046's KTX2 encoder. Its outputs are the owner's. CC0 sources are the fallback if the tool's frames fall short.

## Consequences

- Fire, rain, sparks, beams and the rest are effects written as files and placed by the level, and by the Phase 10 editor, not code.
- The GPU now feeds the CPU: decals and sounds can follow particles, two frames late.
- Collisions see the whole level, not just the screen, for one ray per colliding particle.
- A fire lights its room by one light per emitter: its shadows come from the emitter's sphere, not from each flame.
- A larger pool and the new passes cost memory and time, bounded by the budget above and the presets.
- The flipbook tool is a new tool to build and keep, in exchange for images the engine owns.
- The path tracer still does not see particles: Phase 11's item does that.

## Alternatives considered

- **Keep particles as they are and fake fire with lights and fog**: no animated flames, no rain; not what was asked.
- **A node graph for effects, as Niagara or VFX Graph**: the most flexible, but a graph editor and a compiler from graphs to shaders are larger than the effects ask. Fixed modules with curves cover the list above; a graph can come later if content outgrows them.
- **Keep collisions on the screen and add ray queries for some effects**: two ways to collide; the ray is cheap enough to be the only one.
- **A light per particle**: thousands of lights for ReSTIR to choose among, built on the GPU, where the alias table is built on the CPU; one light per emitter looks the same at a fire's scale.
- **Ribbons on the GPU, from chains of particles**: needed for trails behind thousands of particles, which streaks already cover; the few ribbons the list needs are cheaper to build on the CPU.
- **Mesh particles as rigid bodies in Jolt**: they would stack and roll truly, but the GPU would have to hand them to the CPU; casings and shards bounce and rest well enough on the GPU.
- **Not sorting additive particles**: halves the sort, but an additive spark behind smoke would show through it.
- **Flipbooks from EmberGen or similar**: better looking soonest, but bought tools' outputs do not fit the engine's licence without checking each tool's terms, and the engine's path tracer can render media already.
- **Rain as a screen-space effect**: cheap, but it falls through roofs and ignores the light; the zones and collisions make it part of the level.
