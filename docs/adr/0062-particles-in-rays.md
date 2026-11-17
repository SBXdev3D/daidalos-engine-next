# ADR-0062: Particles in the acceleration structure, and smoke in the fog

- **Status:** Accepted
- **Date:** 2026-11-17

## Context

Phase 11 step 7 (ADR-0041) asks for particles in the acceleration structure (a bottom-level structure rebuilt each frame, opt-out per effect), so that smoke, sparks and mesh particles (step 9.22) cast shadows and show in reflections; and for smoke and steam as small density volumes lit by the volumetrics pass (ADR-0051). Until now rays passed through every particle: a mirror showed no sparks, a smoke column cast no shadow, and the flashlight's beam did not show in smoke, which was only sprites.

## Decision

- **A box per slot of the pool.** The particle pass owns a bottom-level structure of axis-aligned boxes, one per slot of its pool (AABB geometry, new in the RHI: `rhi::AabbGeometry`). The simulation writes each traced particle's box where it will be when the next frame's rays meet it, with room for its motion; a particle that dies, or an effect that is not traced, gets a NaN box, which Vulkan leaves out. `RayTracingScene` builds it before the frame's top-level structure and adds it as one instance (custom index `0xFFFFFF`, not opaque).
- **Only the slots in use are built.** The dead list hands out the lowest slots first, so the particles alive stay packed in the first slots; the simulation reports the highest traced slot with its hits, and the build covers it, plus a quarter and 1024 for the slots taken since (`CommandList::BuildAccelerationStructure(structure, boxes)`).
- **Rays test the particles themselves** (`CrossParticle`, `RayQueries.slangh`): a particle is a disc facing the ray at its center, or for velocity particles (sparks, rain) a segment along their motion, with the image, color and opacity the screen shows. Its opacity is the chance of stopping the ray, from a hash of the ray's origin: smoke's shadows and reflections are dithered, and the denoisers and the temporal resolve smooth them. `TraceClosest` and `Occluded` take the frame, which says where the particles are (`FrameData::particleData`, `particleEffects`, `particleCurves`).
- **What rays see of a particle is what the screen shows:** its color, times the light the simulation found at it when lit (ADR-0044), as emission. It is not lit again, so a reflection costs no shading, and the radiance cache never keeps it.
- **Who sees particles:** reflections and shadows (mask `kMaskParticleShadow` 0x40, new, and `kMaskReflection`). Bounced light does not: a spark found by one ray in a thousand would be a firefly. Additive particles (sparks, flames) cast no shadow; mesh particles cast shadows, as balls of their size, and do not show in reflections, which would show a ball. The particles' own shadow rays, cast while the simulation moves them, skip particles (`DAI_SHADOWS_SKIP_PARTICLES`): a pass cannot read what it writes.
- **Effects opt out** with `"traced": false`: in the factory, rain, splashes, ripples, motes, snow, spores and ash, which are many and small. Moths stay traced: their shadows flutter under the lamps.
- **Smoke in the fog.** An effect's `"fog_density"` adds fog around each particle, thinning to nothing at its edge, where it is opaque. The simulation lists up to 1024 such particles; a pass of the volumetric fog (`FogSmoke.slang`), a thread group per particle, adds their density into a texture of the froxel grid (32-bit integers, atomics), which the fog's lighting adds to the medium with an albedo of 0.9, and its integration empties for the next frame. The lamps and the flashlight now light the smoke from within. The factory's smoke column (1.5), smoke leaks (0.6) and pipe mist (0.4) have one.
- **`renderer.traced_particles`**, on but in the Low preset.

Measured on an RTX 3090 at 2560x1440: the build costs 0.09 ms in the rain room, and the frame is the same within the noise of runs (19.05 and 19.56 ms traced, 19.23 and 19.68 not). `ParticlesCastShadows`: a ball of smoke under the lamp takes 55% of the light off the view. `ParticlesShowInMirrors`: a red spark the harness does not draw shows in a mirror floor. `SmokeThickensTheFog`: smoke with a fog density brightens the view by 35% near the lamp.

## Consequences

- Shadows and reflections of particles lag a frame of motion behind in the boxes, but not in the particles: rays read where the particles are now, inside boxes made large enough.
- Smoke's reflections and shadows are noisy before the denoisers; thin smoke stops few rays.
- The path tracer sees particles as the raster does, but the smoke in the fog only in the volumetric pass: the reference renders no smoke volumes.
- Smoke in the fog has one albedo; colored smoke (green spores) scatters white light in the fog.
- A particle that fills the view near the camera adds to at most 65536 froxels.

## Alternatives considered

- **Triangles for each particle** (a quad facing the camera, or crossed quads): no shader test, but a quad facing the camera vanishes edge-on to a light, and crossed quads show their seams in shadows.
- **One box per alive particle, in the order of the alive list:** the build covers exactly the particles alive, but the box index would need a table to the particle, and the slots past the count would need clearing each frame. Packing the slots gives most of the saving for none of it.
- **Smoke in the fog by a loop over particles in each froxel:** no texture, but a thousand particles times a million froxels.
