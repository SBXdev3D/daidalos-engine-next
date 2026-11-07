# ADR-0023: Spot-light shadows

- **Status:** Superseded by ADR-0035 (2026-11-07)
- **Date:** 2026-11-03

## Context

Since Phase 7.1 the factory has real lights, but they shine through walls. The roadmap asks for shadow maps for spot lights, with a documented budget. The legacy level marks which lamps cast shadows (`lightShadow_caster`), and the legacy alarms and handgun light had shadows too. Point lights would need six faces each, and the demo has none that matter.

## Decision

- **Spot lights only.** A light asks for a shadow with `castsShadows`. Point lights never cast one.
- **A budget of four shadow maps per frame**, given to the spot lights that ask, nearest to the camera first. A light carried by the camera, such as the flashlight, is always first. The others still light, without a shadow.
- **One atlas**: a 2048×2048 depth texture, in tiles of 1024×1024, two by two, with the same reverse-Z convention as the main view. Each light's map covers its cone (capped at 160 degrees), from 10 cm to its range. Nothing nearer than 10 cm casts a shadow, so a lamp's own fixture never darkens its light.
- **The mesh pass takes three steps**: `Prepare` fills the frame's buffers once, `RecordShadows` draws every shadow map in one depth-only rendering scope, and `Record` draws the meshes, which read the atlas. Shadow maps reuse the frame's instance data, skinning included. Alpha-tested materials cut their holes in shadows too. No face is culled, because the level's walls have one side.
- **Against acne and peter-panning**: a depth bias in the shadow map, and receivers pushed along their normal by 1.5 texels, measured at their distance from the light.
- **Soft edges**: nine hardware comparisons, each filtered over four texels, so edges blend over about three texels.
- **Draws can opt out** with `castsShadows` on `MeshRenderer` and `SkinnedMeshRenderer`: the sandbox's weapons do, because held in front of the flashlight they would darken half the room.

## Consequences

- In the factory's alarm hall, the four shadow maps take 0.28 to 0.37 ms of GPU time at 1280×720 on an RTX 3090 in Release, with every draw of the level drawn into each map.
- A fifth lamp near the player lights through walls until it becomes one of the four nearest. Moving through the level can make a lamp's shadow appear or vanish.
- Every draw is drawn into every shadow map, even far outside its cone: culling (Phase 10) should skip them.
- The atlas costs 16 MiB of GPU memory, used or not.

## Alternatives considered

- Point-light shadows with cube maps: six times the cost per light, for lights the demo does not have.
- One texture per shadow map: simpler to address, but more descriptors and more state changes, for the same memory.
- Variance or moment shadow maps: softer, but they leak light where occluders overlap, and need a blur pass.
- Shadows for every light: the cost grows with the number of lamps in the level, which would make frame time depend on level design.
