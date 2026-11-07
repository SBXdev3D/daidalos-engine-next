# ADR-0034: A reference path tracer, and one BSDF

- **Status:** Accepted
- **Date:** 2026-11-07

## Context

Step 9.4 of ADR-0028. Each later step of Phase 9 replaces a raster technique with a ray-traced one, and must be compared with what the scene should look like. That reference has to be correct before it is fast, and it has to use the same materials, lights and BSDF as the rest of the renderer, or the comparison means nothing.

## Decision

- **Storage images in the RHI.** `TextureUsage::Storage` (Rgba32Float), a bindless array of read-write images (`g_storageImages`, binding 4) indexed by `Device::StorageImageIndex`, and barriers through the same `Access` tracking. Every ray-traced step after this one writes images.
- **One BSDF, in a Slang module.** `Bsdf.slang` holds the engine's BSDF (GGX, height-correlated Smith, Schlick, Lambert) and its sampling: GGX visible normals (Heitz 2018) and a cosine-weighted diffuse lobe, chosen by how much each reflects. The mesh pass and the path tracer `import` it. This is the first Slang module (ADR-0031 waited for it).
- **The path tracer is one compute pass with ray queries** (`PathTracer.slang`, `PathTracerPass`). One path per pixel per frame, averaged in an Rgba32Float image while the view, the size and the settings stay; moving objects leave trails, which is accepted for a reference. Hits are shaded with the real materials, read at their full detail (rays have no screen derivatives yet), alpha tested during traversal. Every light is sampled at every surface with a shadow ray that stops 10 cm short of the light, as shadow maps ignore what is that close (ADR-0023). Lights keep the engine's linear falloff and its convention that a light's color is what a white surface facing it reflects. Russian roulette ends dim paths after the second bounce. Emission is added wherever a path meets it; a path that leaves the scene sees a background radiance, black by default. There is no fog and no ambient term: light bounces instead.
- **`renderer.debug_view path`** shows it in place of the lit scene, tonemapped as usual, with `renderer.path_bounces` (default 4) and `renderer.path_samples` (default 4096).
- **The Cornell box** (`renderer::MakeCornellBox`), built from cubes, is the scene of the tests and of `sandbox.scene cornell`. Its ceiling panel is replaced by a point light, since the engine has no area lights (ADR-0028).
- **Tests that pin the tracer down without a reference image**: white furnace tests (a white matte cube and a white metal cube under a white sky must reflect what they receive, within what single-scattering microfacets lose), and a comparison with the mesh pass: one bounce of the path tracer must give the mesh pass's lighting, point and spot lights included. A regression test compares the Cornell box with a committed image (`tests/assets/golden/cornell-48.bin`), loosely enough for another driver's rounding; `DISABLED_WriteCornellBoxReference` writes a new one.
- **FLIP waits for step 9.5**, where real-time output is first compared with the path tracer. Within 9.4, the furnace and the mesh pass comparison are stricter than a perceptual metric.

## Consequences

- One bounce of the path tracer agrees with the mesh pass to 0.03% in total over the pixels away from edges.
- The factory is lit mostly by its ambient term and the player's flashlight. Path traced, with light bouncing instead of an ambient term, it is much darker; the level's lights will need work when ray-traced indirect light replaces the ambient term (9.8).
- GPU time at 1280×720 on an RTX 3090, one path per pixel: 0.25 ms in the factory with four bounces, 0.03 ms with one, 0.05 ms in the Cornell box. Few lights reach most surfaces, which keeps it cheap.
- The device now also needs storage image descriptors that can be updated after binding.

## Alternatives considered

- Accumulating in a storage buffer instead of an image: avoids storage images now, which every later step needs anyway.
- A ray tracing pipeline instead of ray queries: not needed for a tracer that does not have to be fast (ADR-0031).
- Rasterized and traced materials in separate code: the comparison would test the copy, not the renderer.
