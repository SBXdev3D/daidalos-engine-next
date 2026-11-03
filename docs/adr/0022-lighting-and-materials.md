# ADR-0022: Lighting and materials

- **Status:** Accepted
- **Date:** 2026-11-03

## Context

Until Phase 7, the factory's darkness was fog, with one cone of light cut through it for the flashlight. Materials carried full glTF metallic-roughness data, but only the base color was shaded, under a fixed light. The legacy level has 8 ceiling spots and 2 turning alarms, and 24 of its 33 textures have a normal map, made by the project's author, which the importer used to drop.

## Decision

- **Linear HDR, then tonemapping.** Meshes are shaded into an `Rgba16Float` target. A fullscreen pass applies `renderer.exposure` and the Khronos PBR Neutral tonemapper into the sRGB swapchain. PBR Neutral leaves colors below 0.76 untouched, so textures keep the look they were made with, and only highlights are compressed. The debug overlay draws after it, in display space.
- **Metallic-roughness PBR**: GGX distribution, height-correlated Smith visibility, Schlick's Fresnel over Lambert diffuse, with every glTF texture: base color, metallic-roughness, normal, occlusion and emission.
- **Simple light units.** A light's color times its intensity is what a white surface facing it reflects up close: the BRDF is multiplied by π. Lights fade linearly from a fraction of their range to zero at the range, and spot lights between an inner and an outer angle. These are the legacy engine's own terms, so its values carry over directly: a 0.4 grey lamp lights a white wall to 0.4. There are no lumens and no physical camera.
- **Ambient light** is one color in the scene's atmosphere. It is white by default, which shows a scene without lights as before, and dark in the factory. A small analytic term (Karis's fit of the split-sum lookup) adds its specular part, so that metals are not black without reflections.
- **Normal maps follow glTF**: tangents with their handedness in `w`, the bitangent is `cross(normal, tangent) * w`, and green points up the image. Missing tangents are generated at load with **MikkTSpace**, the standard that glTF and baking tools use, fed with `v` flipped because MikkTSpace expects OpenGL texture coordinates. Only red and green are read, and blue is rebuilt, so two-channel compression (BC5) will work. The legacy maps were measured green-up against their diffuse maps' grooves, so they are used as they are.
- **Lights are entities.** `scene::Light` (point or spot) is extracted from world transforms into `SceneView::lights`; a spot light shines down its entity's local -Z. The renderer has no light of its own.
- **Every pass is timed on the GPU** with timestamps, read without waiting when a frame slot comes round, and shown in the overlay.
- **Every light is evaluated for every pixel** for now. Clustered light assignment comes in Phase 7.3 (ADR-0008).

## Consequences

- The factory's lamps, alarms, flashlight and muzzle flash are ordinary entities, and so will be any light a game adds.
- With about 14 lights, the mesh pass of the factory costs 0.74 ms of GPU time at 1280×720 on an RTX 3090 in Release, and the tonemap pass 0.06 ms.
- Shading a pixel costs one loop iteration per light in the frame until clustering, which caps practical light counts at a few dozen.
- A normal-mapped mesh without tangents can gain vertices, where MikkTSpace splits its seams.
- Lights did not cast shadows yet: ADR-0023 adds them to spot lights.

## Alternatives considered

- Physical units (lumens, candela, exposure value): right for photographic lighting, but the legacy values would need converting by guesswork, and nothing in the demo needs real-world brightness.
- ACES filmic tonemapping: popular, but it shifts hues and darkens mid-tones, which changes the look of textures made for an LDR engine.
- Tangents computed per triangle, as the legacy engine did: simpler, but they disagree with the tools that bake normal maps and show seams.
- Flipping green for Direct3D-style maps: not needed once the legacy maps were measured.

## Amendment (2026-11-03): filtering and normal strength

Linear samplers use anisotropic filtering, 16x by default (`renderer.anisotropy`, clamped to the device), so floors and walls seen at a grazing angle stay sharp. `renderer.normal_strength` multiplies the tilt of every normal map. It is 1 by default, as authored; the legacy maps are subtle (half the pixels of the brick wall lean less than 1 degree), and the sandbox's settings file sets 2. A normal map only shows under direct light: the ambient light comes from everywhere, so normals do not change it.
