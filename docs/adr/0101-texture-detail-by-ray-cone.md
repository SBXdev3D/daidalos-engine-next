# ADR-0101: Texture detail by ray cone

- **Status:** Accepted
- **Date:** 2026-11-19

## Context

Step 19.2 (ADR-0099). Rays have no screen derivatives, so every material read at a hit (`ShadeHit`) took the full-size mip: base color, normal, roughness and metalness, emission. A far wall's 2048² texture read at a random point is a texture-cache miss per ray, and its detail is aliasing that the denoiser averages away.

## Decision

- **A ray stands for a cone** as wide as one pixel where it leaves the camera (Akenine-Möller et al., "Texture Level of Detail Strategies for Real-Time Ray Tracing", 2019). `PixelSpread` is the angle of one pixel of the render, from the frame's inverse view-projection. At a hit, the cone's width is that angle times the distance travelled from the camera: to the pixel, then along each bounce.
- **The mip** (`ConeLevel`, `SampleCone`): half of log2 of the triangle's texture area over its world area, plus log2 of the cone's width over the cosine between the ray and the face (at least 0.05), plus half of log2 of the texture's texel count, clamped to its mips.
- **Bounces do not widen the cone by their lobe.** A mirror keeps the detail the screen would show; a diffuse or rough ray reads sharper than it could, never blurrier. Widening by the lobe would save more, and change more.
- **Which rays:** indirect light from pixels and its bounces (`TraceBounce`, `BouncedLight`), reflections (`GiReflect`), and the radiance cache's paths, whose cone is as wide as the 4 × 4 pixels each stands for (`GiUpdate`).
- **Unchanged:** the alpha test, which keeps the full-size mip so that cut-out shadows keep their shape; glass; and the path tracer, the reference.

## Measurements

At 1080p on an RTX 3090, dressed factory, 19.1's build and this one in turns, two runs each:

| | Radiance cache | Indirect light | Direct light | Reflections | Frame |
|---|---|---|---|---|---|
| High, loading floor | 0.48 → 0.43 ms | 3.03 to 3.11 → 2.99 to 3.15 ms | 2.66 to 2.69 → 2.58 to 2.59 ms | 1.64 → 1.60 to 1.62 ms | 11.77 to 11.91 → 11.49 to 11.66 ms |
| Medium, loading floor | 0.28 to 0.30 → 0.25 ms | 0.78 to 0.87 → 0.67 to 0.84 ms | 1.04 to 1.05 → 1.02 to 1.03 ms | 0.77 to 0.80 → 0.78 to 0.83 ms | 5.61 to 5.77 → 5.50 to 5.61 ms |
| Medium, start room | 0.27 → 0.23 ms | 0.69 to 0.71 → 0.59 to 0.65 ms | | | 4.85 to 5.00 → 4.78 to 4.83 ms |

The frame takes 0.1 to 0.3 ms less. The radiance cache, whose paths travel farthest, gains the most, 10 to 15%. Less than hoped: the texture reads were a smaller share of the ray passes than their traversal and shading.

**The image:** screenshots of the start room and the loading floor on Medium and High, at the game's brightness and 16 times brighter, old against new: the differences are the alarm beams' angle and the rain, as between two runs of one build. The wet floor's reflections keep their detail. The GPU tests that compare indirect light and reflections with the path tracer pass.

## Consequences

- Each sampled texture adds a query of its size; the level's triangles are read once more at each hit, for their areas.
- The cone's width is there for later uses: decals already pick their detail from the hit's distance (ADR-0060), which the cone could replace.

## Alternatives considered

- **Widening the cone by each bounce's lobe** (the paper's full method for rough surfaces): a coarser mip for diffuse bounces. Their light is averaged over many rays anyway, so the mean would hold, but it is a step away from "the same image" this phase promised; left for a later measurement.
- **A fixed mip bias for all ray hits:** simpler, but a mirror close to a wall would blur, and a far wall would still read too fine.
