# ADR-0024: Depth pre-pass, ambient occlusion and ambient light from above

- **Status:** Partly superseded by ADR-0040: the ambient light and the ambient occlusion are gone; the depth pre-pass stays
- **Date:** 2026-11-03

## Context

ADR-0008 planned a depth and normal pre-pass, and ambient occlusion computed from it. Since Phase 7.1, the factory's darkness is lit by an even ambient light, which shows neither the creases of the geometry nor the relief of the normal maps: light that comes equally from everywhere is the same whatever way a surface faces.

## Decision

- **A depth and normal pre-pass** draws every opaque mesh first, writing depth and world normals (normal maps included). The mesh pass then tests depth for equality and writes none, so each pixel is shaded once, however many surfaces overlap.
- **Both passes use the same compiled vertex shader**, from `MeshCommon.hlsli`. Two compilations of the same code could round differently and leave holes; one module cannot. The pre-pass only has its own pixel shader. Hot reload recompiles `Mesh.hlsl`, and the pre-pass picks up the new vertex shader at once; its pixel shader changes only with a build.
- **Screen-space ambient occlusion at half resolution**: 16 points in the hemisphere above each pixel, half a meter around it, turned by interleaved gradient noise, are tested against the pre-pass's depth. A 5×5 blur then smooths the result without crossing depth edges. Only the ambient light is darkened: direct light has its shadows. `renderer.ambient_occlusion` turns it on and off at any time.
- **Ambient light comes from above and from below.** `Atmosphere::ambient` comes from above, `ambientBelow` from below, and a surface gets the blend by how far its shaded normal points up. Both are white by default, which keeps an even light. In the factory, 1.5 times `sandbox.ambient` comes from above and half of it from below, so walls keep their level while floors are brighter and ceilings darker, and normal maps show where they lean up or down.

## Consequences

- In the factory, at 1280×720 in Release on an RTX 3090, the pre-pass takes 0.18 to 0.29 ms and ambient occlusion 0.31 to 0.35 ms, and the mesh pass falls from 0.74–1.40 ms to 0.37–0.68 ms: the frame costs about the same as before, with occlusion.
- Corners and creases darken to about 0.8 of their brightness; open surfaces keep theirs.
- Occlusion only knows what is on screen: geometry behind the camera or hidden behind something else does not occlude.
- The ambient light from above and below shows only the part of a normal map that leans up or down: vertical mortar lines stay flat away from lamps.
- Transparent surfaces, when they come, will be drawn after the mesh pass, without the pre-pass.

## Alternatives considered

- Baked ambient occlusion in the level: no runtime cost and no screen-space artefacts, but it needs a baking tool, and moving things such as doors and zombies would not occlude.
- Horizon-based or ground-truth ambient occlusion (HBAO, GTAO): more accurate for the same budget, but more code; the simple hemisphere is enough for the demo and can be replaced alone.
- A compute shader for occlusion: the RHI has no compute yet. It comes with the particles of Phase 7.4.
- Writing depth in the mesh pass too, without equality: simpler, but shades hidden surfaces again.
- Image-based ambient light (an environment map): the right answer outdoors and with reflections; indoors, two colors carry most of it for now.
