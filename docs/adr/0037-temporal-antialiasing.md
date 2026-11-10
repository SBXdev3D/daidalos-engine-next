# ADR-0037: Motion vectors, jitter and temporal antialiasing

- **Status:** Proposed
- **Date:** 2026-11-10
- **Supersedes:** the antialiasing part of ADR-0026 (FXAA and MSAA)

## Context

Step 9.7 of ADR-0028. Antialiasing is FXAA after tonemapping, or MSAA on the pre-pass and the mesh pass. Neither works with what Phase 9 brings: ray-traced effects take one or a few rays per pixel and are noisy, and their denoiser (9.8) reuses earlier frames, which needs to know where each pixel was in the frame before. FSR 2 (9.12) takes the same inputs: motion vectors, jittered frames, and a mask of what has no reliable history. TAA builds those inputs and is the engine's own resolve, which FSR 2 is later compared with.

What the frame knows today does not reach the frame before. `SceneView` is rebuilt every frame from the ECS, and its draws carry no identity: the renderer cannot tell which draw of the last frame a draw was. Skinned draws are skinned once per frame into world space (ADR-0033), and last frame's skinned vertices sit in a buffer whose layout follows that frame's draws. Particles are blended into HDR color and write no depth. The pre-pass writes one target, the normals; the RHI renders to one color target at most.

## Decision

### Motion vectors

- **The scene gives the previous transforms.** `DrawItem::previousWorld` and `SceneView::previousJointMatrices`, parallel to `jointMatrices`. `ExtractScene` keeps, per entity, the matrix it extracted the frame before (a `PreviousWorldTransform` component, written at the end of the extraction). A draw seen for the first time gets its current matrix: no motion. Transforms are the interpolated ones that are drawn, so the previous matrix is what was drawn, not the last physics step.
- **Skinned draws are skinned twice in the same dispatch**: with the current joints into the vertices rays and passes read, and with the previous joints into a buffer of previous positions (12 bytes per vertex). Keeping last frame's skinned buffer instead would mean matching draws whose order changes from frame to frame.
- **The pre-pass writes them**, into a second target, `Motion` (RG16F): where the pixel was the frame before, minus where it is, in texture coordinates, both without jitter. The RHI gains several color targets in `RenderingDesc` and `PipelineDesc`. The denoiser's other inputs (roughness, linear depth) go into the same pre-pass in 9.8.
- **The sky has none written.** Where the pre-pass leaves the cleared value, the resolve reprojects the view direction with the camera's rotation alone, as the sky is infinitely far.
- **`FrameData` gains** the unjittered view-projection, the previous unjittered view-projection, and the jitter.

### Jitter

- **Halton (2, 3), 8 samples**, in pixels, centered on 0, added to the projection of the pre-pass, the mesh pass, the sky and the particles. Everything rasterized and the primary hits of rays agree. Light clusters (ADR-0008) use whole pixels and do not change.
- **The path tracer is not jittered by the camera**: it already jitters its own rays and averages them. It keeps comparing the unjittered view-projection to know when to start over. The path tracer and the ray debug view skip the resolve.

### The resolve

- **A compute pass, "Temporal resolve"**, between the particles and tonemapping, at full resolution. It reads the HDR color, depth, motion vectors, the reactive mask and the history, and writes the new history, which tonemapping reads. Two history textures (RGBA16F) swap each frame.
- **Per pixel:**
  1. The motion vector of the nearest depth in the 3×3 neighbourhood, so that edges of moving objects carry their motion.
  2. The history sampled there with a 5-tap Catmull-Rom filter, which blurs less than bilinear over many frames.
  3. Clipping of the history towards the neighbourhood's color, in YCoCg, by mean and standard deviation (variance clipping) rather than min and max: tighter, so less ghosting.
  4. A blend with 1/10 of the new frame, weighted by inverse luminance so that a single bright sample does not flicker.
  5. The history counts for nothing off-screen, after a reset, and where the reactive mask says so.
- **Particles write a reactive mask**, their alpha, into a second target of their pass (R8). The resolve trusts the history less where it is high. FSR 2 takes the same mask.
- **No depth test for disocclusion yet.** Variance clipping removes most of what reappears from behind a moving object; a test against last frame's depth costs another texture and is added if the measurement in the factory shows ghosting.
- **No sharpening yet.** FSR 2 brings its own (RCAS) in 9.12; a pass of ours would be thrown away.
- **History resets** when the size changes, the antialiasing changes, the debug view changes, and when the game says so: `SceneView::cut`, set by a teleport or a scene change.

### Settings

- `renderer.antialiasing` becomes `taa` (the default) or `off`. `off` keeps the camera still and skips the resolve: for comparisons and debugging. `fxaa`, `msaa2`, `msaa4` and `msaa8` in an old `user.cfg` give a warning and `taa`. The menu offers the two.
- **A debug view, `motion`**, shows the motion vectors in color.

### Deleted

- `FxaaPass`, `Fxaa.slang`, the tonemap pipeline for FXAA and its 8-bit target.
- Multisampling: the multisampled targets, `MeshPass::SetSamples`, `Renderer::ApplySamples`, and in the RHI the sample counts, the resolve targets and `Access::DepthResolve`. Nothing else uses them.

## Plan

One pull request, in commits that each build and pass the tests:

1. RHI: several color targets per rendering and per pipeline; sample counts and resolves removed. FXAA and MSAA removed from the renderer, `renderer.antialiasing` reduced to `off` (temporarily the only mode).
2. Scene: previous transforms in `SceneView`; skinning writes previous positions.
3. Pre-pass motion vectors, the `motion` debug view.
4. Jitter, and the temporal resolve; `taa` becomes the default. Particles' reactive mask.
5. Menus, `daidalos.cfg`, ADR-0026 and 0028 amended, `ARCHITECTURE.md`, measurement.

## Tests

- **Unit:** the Halton sequence and the jittered projection; the rule that chooses the previous matrix of a new entity; the old setting values.
- **GPU, motion vectors:** a camera that moves by a known amount gives the expected vector on a static wall and on the sky; a rigid draw and a skinned draw moved by known amounts give theirs; a draw seen for the first time gives none.
- **GPU, resolve:** a still scene with edges converges, after 32 frames, to within a FLIP threshold of the same scene rendered 8×8 supersampled. An object moving across a background leaves no trace: 16 frames after it passed, the background is within a threshold of the background without the object. A reset drops the history.
- **Synchronization validation** stays clean in the factory, with TAA, with `off`, and with both debug views.
- **Measurement:** GPU time of the pre-pass before and after the second target, and of the resolve, in the factory at 1080p on the RTX 3090; screenshots of the factory with `off` and `taa`, still and moving.

## Consequences

- One antialiasing mode, which also smooths alpha-tested surfaces, shader edges and specular shimmer, which MSAA did not, and which the ray-traced steps need anyway.
- The image is a little softer than with MSAA until sharpening comes with FSR 2.
- Moving objects can ghost or smear; the tests above bound it, but only on the cases they draw.
- The pre-pass writes 4 more bytes per pixel, and skinning does about twice the work, for positions only.
- Every game gets motion vectors without code; a game that teleports the camera sets `SceneView::cut`, or sees one frame of smear.

## Alternatives considered

- **Motion of static geometry from depth and the camera alone**, and a pass that draws only moving objects: saves writing motion for the level, but adds a pass, and every moving thing has to be known as moving.
- **Matching draws in the renderer** by a key the scene passes in: the renderer would keep state per draw, which the scene already has per entity.
- **Last frame's skinned vertices**, kept instead of skinned again: their layout follows last frame's draws.
- **Particles drawn after the resolve**, without jitter: no ghosting, but not antialiased, and FSR 2 wants them before it, with a reactive mask.
- **Keeping FXAA as a cheap option**: ADR-0028 keeps no fallback, and every ray-traced step after this one needs the temporal history anyway.
- **TAAU now**: rendering at a lower resolution is what FSR 2 brings in 9.12; the resolve reads and writes at the same size, with motion vectors in texture coordinates so that a later upscale changes nothing upstream.
