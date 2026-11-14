# ADR-0037: Motion vectors, jitter and temporal antialiasing

- **Status:** Accepted, amended by ADR-0049 (the resolve becomes an upscaler; sharpening is the engine's own)
- **Date:** 2026-11-10
- **Supersedes:** the antialiasing part of ADR-0026 (FXAA and MSAA)

## Context

Step 9.7 of ADR-0028. Antialiasing was FXAA after tonemapping, or MSAA on the pre-pass and the mesh pass. Neither works with what Phase 9 brings: ray-traced effects take one or a few rays per pixel and are noisy, and their denoiser (9.8) reuses earlier frames, which needs to know where each pixel was in the frame before. FSR 2 (9.12) takes the same inputs: motion vectors, jittered frames, and a mask of what has no reliable history. TAA builds those inputs and is the engine's own resolve, which FSR 2 is later compared with.

What the frame knew did not reach the frame before. `SceneView` is rebuilt every frame from the ECS, and its draws carry no identity: the renderer cannot tell which draw of the last frame a draw was. Skinned draws are skinned once per frame into world space (ADR-0033), and last frame's skinned vertices sit in a buffer whose layout follows that frame's draws. Particles are blended into HDR color and write no depth. The pre-pass wrote one target, the normals; the RHI rendered to one color target at most.

## Decision

### Motion vectors

- **The scene gives the previous transforms.** `DrawItem::previousWorld` and `SceneView::previousJointMatrices`, parallel to `jointMatrices`; without them, a draw or its joints stood still. `ExtractScene` keeps, per drawn entity and joint, the matrix it extracted the last time (`scene::DrawnTransform`), written once the extraction is done, so that a joint shared by two skins gives both the same previous matrix. A draw seen for the first time gets its current matrix: no motion. Transforms are the interpolated ones that are drawn, so the previous matrix is what was drawn, not the last physics step.
- **Skinned draws are skinned twice in the same dispatch**: with the current joints into the vertices rays and passes read, and with the previous joints into a buffer of previous positions (12 bytes per vertex). The frame's joint buffer holds the previous joints after the current ones. Keeping last frame's skinned buffer instead would mean matching draws whose order changes from frame to frame.
- **The pre-pass writes them**, into a second target, `MeshPass::kMotionFormat` (RG16F): where the pixel's surface was the frame before, minus where it is, in texture coordinates, both without jitter. Instances carry their previous matrix, or the index of the previous skinned positions. The RHI gains several color targets: `RenderingDesc::moreColorTargets` and `GraphicsPipelineDesc::moreColorFormats`, up to `kMaxColorTargets` (4) in all, blended alike. The denoiser's other inputs (roughness, linear depth) can go into the same pre-pass in 9.8.
- **The sky has none written.** The resolve reprojects the view direction with the camera's rotation alone, as the sky is infinitely far, where the whole neighbourhood is at the far plane.
- **`FrameData` gains** the unjittered view-projection, the previous unjittered view-projection, and the jitter; it grows from 256 to 400 bytes.
- **`renderer.debug_view motion`** shows the motion vectors in color: grey where nothing moves.

### Jitter

- **Halton (2, 3), 8 points**, from the sequence's second point, centered on the pixel (`renderer::Jitter`). `FrameTargets::jitter` moves the projection of the pre-pass, the mesh pass, the sky and the particles, and `FrameData::inverseViewProjection` follows it, so that ambient occlusion and the sky unproject what was sampled. Light clusters (ADR-0008) use whole pixels and do not change.
- **Debug views are not jittered and skip the resolve**: the path tracer jitters its own rays and averages them, and keeps comparing the unjittered view-projection to know when to start over.

### The resolve

- **`TemporalResolvePass`, a compute pass, "Temporal resolve"**, after the particles and before tonemapping, at full resolution. It reads the HDR color, depth, motion vectors, the reactive mask and the history, and writes the new history, which tonemapping reads. Two history textures swap each frame. They are RGBA32F: the RHI's storage images have that one format, and every GPU the engine accepts filters it linearly.
- **Per pixel:**
  1. The motion vector of the nearest depth in the 3×3 neighbourhood, so that the edges of moving objects carry their motion.
  2. The history sampled there with a Catmull-Rom filter, in 5 bilinear taps, which blurs less than bilinear over many frames.
  3. Clipping of the history towards the neighbourhood's mean, in YCoCg, to the box of its standard deviations (variance clipping), tighter than its minimum and maximum: less ghosting.
  4. A blend with 1/10 of the new frame. Colors are clipped and blended after a reversible tone map, x / (1 + max(r, g, b)), so that one bright sample does not dominate and flicker; an edge converges to its coverage as the screen shows it, after tonemapping, not in linear light.
  5. No history off-screen, after a reset, and less of it where the reactive mask says so: the weight of the new frame goes from 1/10 to 1 with the mask.
- **Particles write a reactive mask**, their coverage, into a second target (`ParticlePass::kReactiveFormat`, R8), blended over each other like their colors; the particle pass clears it. FSR 2 takes the same mask.
- **No depth test for disocclusion.** Variance clipping removes what reappears from behind a moving object, as the tests show; a test against last frame's depth is added if ghosting shows in play.
- **No sharpening.** FSR 2 brings its own (RCAS) in 9.12; a pass of ours would be thrown away.
- **The history starts over** when the size changes, when a frame was not resolved (antialiasing off, a debug view, no camera), and when the view is cut: `SceneView::cut`, set on a scene's first view and by `Scene::CutView`, which the sandbox calls when it teleports the player. A cut also gives the frame no camera motion.

### Settings

- `renderer.antialiasing` is `taa` (the default) or `off`. `off` keeps the camera still and skips the resolve. Any other value, `fxaa` and `msaa…` from an old `user.cfg` included, gives a warning and `taa`. The menu offers the two.

### Deleted

- `FxaaPass`, `Fxaa.slang`, the tonemap pipeline for FXAA and its 8-bit target.
- Multisampling: the multisampled targets, `MeshPass::SetSamples` and `MeshPassSettings`, `Renderer::Impl::ApplySamples`, and in the RHI the sample counts of textures and pipelines, `DeviceInfo::maxSamples`, the resolve targets and `Access::DepthResolve`.

## Tests

- **Unit:** the Halton points, their range and their period; the previous matrix of a moved, a new and a still entity, and of a joint shared by two skins; the render graph's further color targets.
- **GPU, RHI:** a rendering that writes two color targets, each with its own clear.
- **GPU, motion vectors:** a first frame has none; a camera moved half a meter gives the expected vector, and none across a cut; a rigid draw and a skinned draw moved half a meter give theirs, and none without previous matrices.
- **GPU, resolve:** a first frame resolves to itself; an edge converges, after 32 frames, to its coverage by the jitter's points; a reset drops the history; a square that jumps across the screen leaves only the background where it was, and carries its history where it went. Particles mark their coverage in the reactive mask. A scene cuts its first view and a teleport's.
- **The renderer** switches between `taa` and `off` while running, resizes, and shows each debug view, with no validation error. Synchronization validation stays clean in the factory with TAA.
- **Not tested:** the sky's motion on its own; the edge test compares with the coverage of the jitter's points rather than with a supersampled image and FLIP.

## Consequences

- One antialiasing mode, which also smooths alpha-tested surfaces, shader edges and specular shimmer, which MSAA did not, and which the ray-traced steps need anyway.
- At 1920×1080 in Release on the RTX 3090, the factory's frame goes from 0.57–0.60 ms of GPU time with `off` to 0.77–0.80 ms with `taa`: about 0.2 ms for the motion target, the reactive mask and the resolve.
- The image is a little softer than with MSAA until sharpening comes with FSR 2.
- Moving objects can still ghost or smear in cases the tests do not draw; the history holds about ten frames.
- The history takes 2 × 16 bytes per pixel, 66 MB at 1080p, and the motion vectors 4 more.
- Every game gets motion vectors without code. A game that moves the camera in a jump calls `Scene::CutView`, or sees one frame of smear.

## Alternatives considered

- **Motion of static geometry from depth and the camera alone**, and a pass that draws only moving objects: saves writing motion for the level, but adds a pass, and every moving thing has to be known as moving.
- **Matching draws in the renderer** by a key the scene passes in: the renderer would keep state per draw, which the scene already has per entity.
- **Last frame's skinned vertices**, kept instead of skinned again: their layout follows last frame's draws.
- **Particles drawn after the resolve**, without jitter: no ghosting, but not antialiased, and FSR 2 wants them before it, with a reactive mask.
- **Keeping FXAA as a cheap option**: ADR-0028 keeps no fallback, and every ray-traced step after this one needs the temporal history anyway.
- **RGBA16F history**: half the memory, but the RHI's storage images would need a second format.
- **TAAU now**: rendering at a lower resolution is what FSR 2 brings in 9.12; motion vectors are in texture coordinates, so that a later upscale changes nothing upstream.
