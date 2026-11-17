# ADR-0060: Decals in the material function

- **Status:** Accepted
- **Date:** 2026-11-17

## Context

Phase 11 step 5 (ADR-0041) asks for decals evaluated in the material function, so that they show in reflections and indirect light: blood, mold, grime, stains, and impacts (bullet holes, splatter, scorch marks). A decal drawn as its own geometry over the G-buffer would be invisible to every ray: mirrors, glass and bounced light would show the clean wall.

## Decision

- **A decal is a box that projects an image along its -Z** (`renderer::Decal`): base color with coverage in alpha, an optional normal map, a tint, an opacity, the roughness and metalness the surface takes where it is covered, and a fade for surfaces that turn away from it. Its box's X and Y are the image, its Z how deep it reaches.
- **One function applies decals, `ApplyDecals` (`Decals.slangh`),** to the base color, metalness, roughness and normal, before anything is lit. The G-buffer calls it, choosing the image's level from the pixel's footprint; ray hits call it too (`ShadeHit`), at a level from the hit's distance. Reflections, glass, the radiance cache, bounced light and the path tracer therefore see decals as the screen does.
- **64 decals a frame, the nearest to the camera,** within the view distance. Each draw gets a 64-bit mask of the decals whose boxes touch its bounding sphere (`InstanceData::decalMask`, worked out on the CPU), and a pixel tests only those. A pixel with no decal near pays one comparison.
- **Level decals are entities** (`scene::Decal`, `decal` in level files, paths relative to the level), and **impacts come and go:** `Scene::SpawnDecal` lays a decal on a surface, turned by a random angle, and fades it over the end of its life; the 96 most recent stay. The sandbox leaves bullet holes where shots land (a dent with bright edges on metal), and blood on whatever is up to 2.5 m behind a zombie that is hit.
- **`renderer.debug_view decals`** shows the lit scene with what decals cover in magenta.
- **The images are written by `scripts/make_decals.py`** from noise and shapes, so they carry no outside licence: a blood splatter, blood drips, grime, mold, a scorch mark, a bullet hole with its normal map, and a dent in metal. The editor places them from Create > Decals.

Measured on an RTX 3090: a decal on a floor is 0.006 FLIP from the path tracer (0.071 without it), and a decal on a block shows in a polished metal floor on as many pixels as the path tracer finds there. Their cost was not measured in isolation here: a pixel tests only the decals its draw's mask names, and Phase 17 measures the factory with many impacts on every preset.

## Consequences

- Beyond 64 decals in view, the farthest are not drawn. A level dense with decals near the camera would need clustering in screen or world space; the mask is the place to put it.
- Decals change the material, not the shape: a bullet hole's normal map tilts the light, but the hole has no depth to look into.
- Decals do not cover skinned characters well: the mask uses the draw's bind-pose bounds and the box is fixed in the world.
- Emissive decals (glowing embers in a scorch) and wet decals (puddles) are not in this step; wetness (11.6) will add water to surfaces through the same function.

## Alternatives considered

- **Deferred decals drawn as boxes over the G-buffer:** the common way, cheap per decal and with no limit to their number, but reflections, glass and bounced light, which shade hits from the materials, would not see them.
- **Decals baked into textures:** free at run time, but nothing could come and go, and the factory's textures are shared between many walls.
- **A world-space grid of decals for ray hits:** exact for hits far from the camera, but a structure to build each frame for what reflections of nearby decals already show.
