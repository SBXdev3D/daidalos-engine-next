# ADR-0099: Phase 19, faster ray tracing at the same image

- **Status:** Accepted
- **Date:** 2026-11-19

## Context

After Phase 18 (ADR-0098), the High frame at 1080p on an RTX 3090 is 12 ms, and about 8 ms of it is ray tracing: direct light (ReSTIR, ADR-0044), indirect light (ADR-0045), reflections (ADR-0042), the radiance cache (ADR-0040), and the denoiser behind them. On 2026-11-19, the owner asked what could make the ray-traced passes faster without changing the image, and to do it as Phase 19.

What is already right: opaque instances are forced opaque, so only alpha-tested materials run the alpha test; shadow rays end at their first hit; instance masks keep each kind of ray to what it must see; static bottom-level structures are built for fast tracing.

What is not:

- **The top-level structure is built for fast building** (`rebuiltEveryFrame`), though it holds a few hundred instances, takes microseconds to build either way, and millions of rays cross it each frame.
- **No bottom-level structure is compacted:** each keeps the size the driver allowed before building, typically half again what it needs.
- **Every material read at a ray hit is at the full-size mip** (`SampleLevelZero` in `RayQueries.slangh`): base color, normal, roughness, emission and the alpha test, for every indirect, reflection and cache ray. A far wall's 2048² texture read at random is a texture-cache miss per ray.
- **The ray passes are large shaders** with decals, wetness, relief and the full material at their hits, in groups of 8 × 8 threads, unprofiled.

Shader execution reordering and opacity micromaps, the hardware's own answers, need an Ada GPU (RTX 40 series) or later; the RTX 3090 is Ampere.

## Decision

Phase 19, one PR per step, each measured before and after on the old and new build in turns; the image is compared with screenshots at the game's brightness and 16 times brighter, and with the path tracer's FLIP where a step can change a pixel:

1. **19.1, the structures.** The top-level structure built for fast tracing; the static bottom-level structures compacted after their build.
2. **19.2, texture detail by ray cone.** Each ray carries the width of the cone it stands for (Akenine-Möller et al., "Texture Level of Detail Strategies for Real-Time Ray Tracing", 2019); a hit reads the mip that the cone's footprint covers. A far wall's texel read at full size is aliasing that the denoiser averages away; the right mip is that average, read once. Mirrors keep a narrow cone and their detail.
3. **19.3, the shaders.** Profiled with Nsight Graphics if it is installed, otherwise by timing variants: group shape, occupancy, ray lengths, and splitting tracing from shading where registers limit occupancy.

"The same image" means no visible change; 19.2 changes values below what the denoiser leaves, and says by how much.

## Consequences

- As in Phase 18 (ADR-0092), a step that does not pay for itself is recorded and left out.
- The presets' shares of High (ADR-0052, ADR-0098) are measured again at the end of the phase.
