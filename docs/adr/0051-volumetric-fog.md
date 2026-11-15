# ADR-0051: Volumetric fog in froxels, and fog in the path tracer

- **Status:** Accepted
- **Date:** 2026-11-15
- **Amends:** ADR-0039 (the composite's distance fog goes; the denoiser's history is short where the light moved), ADR-0034 (the path tracer has fog), ADR-0044 (the light map flags lights that moved)

## Context

Step 9.17 of the roadmap: froxel volumetric fog with ray-traced shadows and local volumes, replacing exponential fog. The fog until now was a color mixed in with distance, `exp(-density × distance)`, in the composite, glass and particles. That color was light from nowhere, which ADR-0040 had ruled out everywhere else, and the path tracer had no fog at all.

The rendering plan (section 8) asks for a camera-aligned grid of about 160 × 90 × 128, lit by ReSTIR DI samples with ray-query shadows, with temporal reprojection, local fog volumes, and a multiple-scattering approximation.

## Decision

**A participating medium, lit by the lights.** The level's fog (`Atmosphere::fog`) has a density, the light lost per meter, an albedo per color, and one Henyey-Greenstein anisotropy for every fog. It fills everything within the view distance of the camera, the same everywhere: the height falloff the plan proposed was left out, so that the light it takes is computed exactly. Boxes of fog (`scene::FogVolume`, `SceneView::fogVolumes`) add their density: a cube scaled by the entity's transform, thinning at its faces over a fraction of its half-size, with a value noise that moves at a velocity, computed in the shader. All fog is within the view distance: beyond it, nothing is drawn.

**Froxels.** `VolumetricFog` keeps a grid that follows the camera: tiles of 16 pixels of the output, whatever the render scale, and 64 slices (`renderer.fog_slices`), spaced exponentially from the near plane to the far plane. The RHI gains 3D textures, which shaders find through the same bindless arrays as 2D ones. A first compute pass draws a point at random in each froxel, takes the medium there and the light it scatters towards the camera: one of the frame's lights, chosen among eight drawn by power and weighed by what they would scatter, and one shadow ray, so alpha-tested blades cut the shafts. It blends the result with what the froxel held last frame, found where its center was then (95% history). A second pass adds the froxels up from the camera, integrating each slice exactly, so that each holds the light scattered in front of it and the fraction of the light from behind that crosses it.

**In motion.** Found in play, walking backward with the flashlight, after the first version:

- *The fog near the camera broke up.* The flashlight is a small share of the factory's power, so drawn by power it was among a froxel's eight candidates in few frames, and next to the camera it is nearly the only light. Each candidate is now drawn, with even odds, from the lights by power or from a second alias table by power near the camera, `FogSourceWeight`: k² / (d² + k²) of its power at d meters from the camera, k = 4 m; its probability is the average of both (defensive sampling), so the estimate stays unbiased and a far light keeps at least half its chance. The second table alone made the fog lit by far panels blotchier.
- *Froxels out of view last frame started over* with one ray's noise, at the screen's edges, which walking backward brings into view. They now read the nearest edge of last frame's grid, with 80% history.
- *Surfaces lagged behind the flashlight.* The light map, which flagged each light the last frame did not have, now also flags each light that moved since (an analytic light by 1 mm or a hundredth of a degree, an emissive draw whose matrix changed). Where a pixel's light moved, the lighting tells the denoiser, which then averages at most 4 frames instead of 32; froxels lit by a light that moved keep 80% of their history instead of 95%. The lag showed most beside the weapon: floor that comes out from behind it has no history and showed this frame's light, next to floor that showed the light's last frames.

**One lookup for everything.** The composite, glass and particles read that texture at their depth, and the sky is seen through the fog to the far plane. Each pixel reads it half a froxel off, a different way each pixel and frame (interleaved gradient noise): temporal antialiasing averages these offsets, which blends neighbouring froxels' noise instead of showing the grid. `renderer.debug_view fog` shows the scattered light alone. `renderer.volumetric_fog false` clears the air.

**Light to surfaces crosses the level's fog.** Every light sample loses what the level's fog takes between the light and the point (`LevelFogTransmittance`), analytically. Boxes of fog do not shadow light.

**The path tracer has fog** (ADR-0034). Paths find where light scatters by delta tracking, against the densest the fog gets; there the lights are sampled through the phase function and the path goes on in a direction the phase function draws. Shadow rays lose what ratio tracking estimates, boxes included. It is the reference the froxels are measured against.

**Changed from the plan.**

- **No ReSTIR reservoirs per froxel.** The plan's ReSTIR DI per froxel was built: each froxel kept its light, and combined eight new candidates with the lights it and two neighbours kept last frame. In fog among a thousand lamps it measured worse than no reuse, by FLIP against the path tracer: 0.094 to 0.114 against 0.083, and about 2% too dark, with blobs around the lights it had found. It was dropped.
- **No multiple scattering, and no bounced light in the fog.** The radiance cache holds light per surface orientation, which has no value in mid-air, and reading it would cost a ray per froxel. Indoor haze is thin; both are deferred.
- **64 slices, not 128**, at the High preset; 9.18's presets set it.
- **One anisotropy for every fog**, not one per volume.

## Measurements

RTX 3090. Against the path tracer, direct light only in both (one scattering, or one surface), the froxels spanning as much of the view as the game's at 1440p:

| Scene | Froxels | Without fog | Brightness |
|---|---|---|---|
| Cornell box, a narrow spot, haze and a dense box | 0.090 | 0.165 | 0.99 |
| The same, a grid 8 times coarser | 0.32 | | |
| A thousand lamps and a panel, black walls | 0.088, one frame 0.098 | | 0.98 to 1.00 |

A grid as coarse as the light's cone blurs it into a glow around the lamp. Among the thousand lamps, drawing by power alone measured 0.083 to 0.089 averaged and 0.101 to 0.103 for one frame: the two tables cost nothing there, where every lamp is near the camera; their gain is the flashlight's, in play.

In the factory at 1440p, the two passes cost 0.38 ms. Dropped on the way, measured: 24 candidates instead of 8 (0.86 ms, little gain); a 3×3 blur of the froxels before adding them up (FLIP 0.090 to 0.119, +0.3 ms); weighing samples by the inverse of their brightness in the history, which took the glow away with the noise.

## Consequences

- **Fog lit by many lights far from the camera is blotchy.** Drawn by power over the whole level or near the camera, a light next to a far froxel among hundreds, such as the factory's emissive panels among 720 lights, is rarely a candidate: its light comes in rare, bright samples, which the history spreads into slow blotches, most visible in the fog debug view. The reuse above did not cure it. Drawing lights from those near each froxel, a grid of lights by position that the surface lighting could also use, is a later step, measured on its own.
- Where a light moved, surfaces and fog are noisier while it moves: the history that hid the noise is what lagged. Standing still, the flashlight does not move, and the histories are whole again.
- The radiance cache still lags behind a moving light by a few frames (ADR-0040): the flag does not reach it.
- Boxes of fog do not shadow the light that crosses them, nor do particles; ADR-0041's Phase 11 puts particles in the acceleration structure and lights smoke as density.
- The factory's haze is now dark where no lamp reaches it, and its fog no longer depends on a color: the steam of the leak shows in the flashlight, and the dust of the start room in the lamp's cone.
- The RHI's storage image slots go from 64 to 128: a resize holds twice the targets until the GPU is done with the old ones, and the fog's three new textures exceeded 64.

## Tests

- **`PathTracerTest.VolumetricFogMatchesThePathTracer`:** the Cornell box in fog, below FLIP 0.12 and well below the image without fog, within 5% of the path tracer's brightness.
- **`PathTracerTest.FogAmongAThousandLightsMatchesThePathTracer`:** below FLIP 0.11 averaged and 0.13 for one frame, within 5% of its brightness.
- **`FogSourceWeight.KeepsThePowerOfLightsNearTheCameraAndLowersFarOnes`**, **`ExtractScene.FogVolumesAreBoxesAtTheirEntity`**, **`GpuTest.A3DTextureIsSampledAndStoredThroughTheBindlessArrays`**, and `RendererTest.ShowsEachDebugView` with the fog view.
