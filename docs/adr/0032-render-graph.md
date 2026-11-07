# ADR-0032: A render graph that places barriers, labels and times passes

- **Status:** Accepted
- **Date:** 2026-11-07

## Context

Step 9.2 of ADR-0028. The renderer synchronized its passes by hand: about 25 `Transition` and `Discard` calls in `Renderer::Render`, more inside the passes, and five full memory barriers in the particle simulation. Depth changed state four times a frame, with branches for multisampling. Every ray-traced step to come adds passes, and synchronization validation, which Phase 9 requires, had never run. GPU times came from a fixed list of ten names, whether a pass ran or not.

## Decision

- **`renderer::RenderGraph`**, internal to the renderer module (no new module, ADR-0021), is rebuilt every frame. A pass declares the textures and buffers it uses (`rhi::TextureUse`, `rhi::BufferUse`) and, if it renders, its targets (`rhi::RenderingDesc`); the graph begins and ends the rendering, and declares its targets as written. A target that is cleared or receives a resolve loses its content first.
- **Barriers come from the RHI's tracking.** `rhi::Access` names each way the GPU uses a resource (targets, reads in each shader stage, compute writes, indirect arguments, copies, presentation, depth resolve). Each texture and buffer remembers its last write and the reads since (`rhi::AccessState`). `CommandList::Use` applies the rule `rhi::NextAccess`, a pure function: a write, a change of layout or a discard waits for every earlier use; a read waits for the last write unless an earlier barrier already covered its stage. `Transition` and `Discard` remain, as shorthands over the same tracking, for uploads, the swapchain and tests.
- **No reordering, no culling, no resource ownership.** Passes run in the order they are added; they use the textures and buffers the renderer and the passes create. Transient resources and memory aliasing wait for a measurement (ADR-0028), as does async compute.
- **Debug labels** wrap each group and each pass, for RenderDoc and Nsight.
- **GPU times per group.** A pass can name a group; passes in a row with the same group share a line in `Renderer::GpuTimes` (the particle simulation's emission, simulation, finish and 10 sort dispatches are one "Simulation"). Only the groups that ran are timed. The names follow the frame slot whose timestamps they describe (`rhi::Frame::slot`).
- **Passes register themselves.** `MeshPass::AddShadows`, `AmbientOcclusionPass::AddTo`, `ParticlePass::AddSimulation` and `AddDraw` add their passes; the renderer adds the pre-pass, mesh, tonemap, antialiasing, interface and overlay passes.
- **Synchronization validation** is an option of the device (`DeviceConfig::synchronizationValidation`, cvar `renderer.sync_validation`), on in every GPU test.

## Consequences

- `Renderer::Render` declares what each pass uses instead of how to get there; no pass in the renderer records a barrier.
- Synchronization validation found two hazards on its first run, both older than the graph. A multisampled depth resolve happens at the color output stage, which the barriers around the resolved depth did not wait for (`Access::DepthResolve`). Two uploads into one buffer in the same batch had no barrier between their copies (`Device::UploadBuffer` now declares its write). A third was in the old code: particle simulation read depth and normals in a compute shader after a transition that only covered fragment shaders, saved by the full memory barrier that followed it.
- Particle simulation went from 0.091 to 0.053 ms of GPU time: its up to fourteen full memory barriers became barriers on the buffers each dispatch uses. The frame's total is unchanged.
- The first group's time includes how much it overlaps the end of the previous frame: a timestamp is not a barrier. The shadow pass, first in the frame, has measured 0.045 and 0.073 ms with the same shaders in different builds.
- Unit tests check the rule and the graph's plan without a GPU.

## Alternatives considered

- Barriers computed by the graph alone, from its own state: a second record of what each resource last did, next to the device's, which uploads and the swapchain would not update.
- A graph that owns and aliases transient targets: the targets are few and small until ray tracing needs more.
- Keeping hand-written barriers: each new pass of Phase 9 would add more, unchecked until validation runs.
