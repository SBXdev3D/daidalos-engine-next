# ADR-0033: Acceleration structures, and skinning once per frame

- **Status:** Accepted
- **Date:** 2026-11-07

## Context

Step 9.3 of ADR-0028: what rays are traced against. A bottom-level acceleration structure (BLAS) holds the triangles of one mesh; a top-level structure (TLAS) places instances of them in the world. Skinned meshes were skinned by the vertex shader, separately in the shadow, pre-pass and mesh passes; no buffer held their skinned positions, which a BLAS is built from. And whether a hit is opaque, or both faces can be hit, depends on each draw's material, not on the mesh.

## Decision

- **The RHI has acceleration structures** (`CreateAccelerationStructure`, `CommandList::BuildAccelerationStructure(s)`, `Device::WriteInstances`), with new `rhi::Access` values so that the render graph and synchronization validation order their builds and the rays that read them. Top-level structures get a slot in a bindless array that shaders trace by index (`g_accelerationStructures`, binding 3). Every shader may use ray queries.
- **One BLAS per mesh**, built with the uploads right after its vertices arrive, favoring tracing speed, and never rebuilt.
- **Skinning moves to a compute pass** that writes each skinned draw's vertices, in world space, once per frame. The shadow, pre-pass and mesh passes draw those vertices with an identity world matrix, and the skinned draws' BLAS are built from the same buffer: rasterization and rays see the same surface, which ray-traced shadows (9.5) need to avoid acne and gaps.
- **Skinned draws' BLAS are refitted every frame.** A skinned draw keeps the structure of the skinned draw in its position the frame before, while the geometry is the same, which is the identity a refit needs; the first build is a full one. The plan was to rebuild them every frame; measured, rebuilding the factory's seven took 0.197 ms of GPU time, refitting takes 0.049 ms. A refit keeps the first build's hierarchy, so rays slow down as limbs move far from that pose; a full rebuild can be scheduled once that shows in a measurement.
- **A TLAS per frame slot**, rebuilt every frame from the frame's draws, favoring build speed. An instance's custom index is its draw's index in the frame's instance data, which now holds, for each draw, its vertex and index buffers and material: what a hit needs to be shaded. Instances are opaque unless their material is alpha-tested, double-sided ones are hit from both sides, and masks separate shadow casters from the rest, so that first-person weapons will not shadow the world.
- **No compaction.** It would save memory at the cost of a size query read back from the GPU and a second copy a frame later. The factory's 73 structures hold 1.7 MB.
- **A debug view**, `renderer.debug_view rays`: one ray per pixel from the camera, each instance in its own color, shaded by the angle of the triangle it hits. It shows that the structures match what rasterization draws, skinned draws included.

## Consequences

- Graph passes "Skinning" and "Acceleration structures" (skinned structures, then the scene's) run before the shadow maps.
- Mesh vertex and index buffers are also storage buffers and build inputs. The bindless storage buffer array grows from 1,024 to 4,096 slots.
- GPU time in the factory, 1280×720, RTX 3090, Release, about 30,000 frames: skinning 0.011 ms, skinned structures 0.049 ms, scene structure 0.095 ms for 71 instances. The rest of the frame is unchanged. Building the factory's meshes on the CPU takes about 3.5 ms more, for the 58 BLAS.
- The screenshots of the lit factory match those of the previous build as closely as two runs of the previous build match each other.
- Raster and rays share one skinning, so the vertex shaders no longer read joints.

## Alternatives considered

- Skinning in the vertex shader, and again in compute for rays: two results that rounding can tell apart, and three skinnings per draw instead of one.
- Rebuilding skinned structures every frame: no identity to track, but four times the GPU time.
- Compaction now: complexity for 1.7 MB.
