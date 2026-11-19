# ADR-0095: Async compute, measured and left off

- **Status:** Accepted
- **Date:** 2026-11-19

## Context

Step 18.3 (ADR-0092). The engine had one queue. Each frame was one command buffer and one submission, ordered by one timeline semaphore (ADR-0007). On most GPUs, a queue that computes without drawing can run beside the graphics queue. Its work fills the shader units that the graphics queue's work leaves idle.

Two kinds of passes could go there, because neither reads the G-buffer:

- the radiance cache's update and resolve: about 0.5 ms on High at 1080p on an RTX 3090;
- the volumetric fog: 0.1 to 0.2 ms.

Particle simulation could not: mesh particles are drawn in the G-buffer. The G-buffer itself takes 0.8 ms.

## Decision

The machinery is built and tested. It stays **off by default** (`renderer.async_compute false`), because on the measured GPU the frame was slower with it.

**The RHI** (`rhi::DeviceConfig::asyncCompute`):

- **A second queue** from a family that computes but does not draw, when the device has one.
- **Shared resources:** buffers, images and acceleration structures are created shared by both families (`VK_SHARING_MODE_CONCURRENT`), so that neither queue has to hand them to the other.
- **A frame in up to three parts.** `Device::BeginAsyncCompute` ends the first part of the frame's graphics commands and opens the compute queue's list. `Device::JoinAsyncCompute` starts the third part. At `EndFrame`, a second timeline semaphore orders them:
  - the compute queue waits for the first part;
  - the second part runs beside it;
  - the third part waits for it and signals the frame's value.
- **Barriers on the compute queue** keep only the stages that queue has. A source in graphics stages ran in the first part, which the semaphore orders.
- **Timestamps:** the compute queue has its own range of queries (`Device::CompletedComputeTimestamps`).

**The render graph:**

- **`PassDesc::async` marks a pass that may run on the compute queue.** `RenderGraph::PlanAsync` places the fork at the first such pass. It joins the queues before the first graphics pass after it that:
  - writes what an async pass uses;
  - uses what an async pass writes;
  - uses a texture an async pass uses, since the device tracks a texture's layout for one queue at a time;
  - or that a later async pass depends on.

  Async passes from the join on run on the graphics queue, in order.
- **Recording order:** the graphics passes that run beside the compute queue are recorded before the compute queue's passes. The device tracks each resource's last use in recording order, so those passes place their own barriers on the graphics queue.
- **GPU times:** the compute queue's passes are timed apart and logged as "(compute queue)", out of the frame's sum. The graphics queue's wait shows as "Waiting for the compute queue".

**The renderer:**

- The radiance cache's passes (`GlobalIllumination::AddCacheUpdate`) and the fog are added before the G-buffer, as async passes.
- With the setting off, they run there on the graphics queue, in order. The frame's time is unchanged (below).

## Measurements

High at 1080p, RTX 3090, the dressed factory with the flashlight on, the setting off and on in turns, two runs each.

| | Loading floor, off | Loading floor, on |
|---|---|---|
| Frame | 13.6 to 13.7 ms | 14.0 ms |
| G-buffer | 0.79 to 0.81 ms | 1.16 to 1.19 ms |
| Radiance cache | 0.52 to 0.53 ms | 1.21 ms, on the compute queue |
| Waiting for the compute queue | | 0.35 to 0.41 ms |

On the loading floor, the passes that share the GPU take 1.42 ms in order (G-buffer, cache, fog) and 1.54 to 1.57 ms side by side (G-buffer, then waiting).

In the start room, they also take longer side by side: 1.66 ms against 1.53. That room's frame read faster with the compute queue (10.8 against 11.4 to 12.3 ms), but the difference was in direct lighting and the denoiser, which the compute queue does not touch.

**Placed later:** the cache's update was also tried after the frame's rays, beside direct lighting, the denoiser and post-processing. The next frame then reads a cache one frame older. Direct lighting went from 2.85 to 2.88 ms to 3.24 to 3.29 ms beside it, and the loading floor's frame from 13.5 to 13.7 ms in order to 13.9 to 14.0 ms side by side.

**Why it loses here:** the G-buffer is not limited by rasterizing. Each pixel runs the material function: textures, decals (ADR-0094), wetness and relief. That leaves few shader units idle. The cache and direct lighting both trace rays, and compete for the same units. Async compute pays where the graphics queue waits on fixed-function work, such as shadow maps or a depth prepass, and this frame has almost none.

With validation and its synchronization checks on, frames with the compute queue report no error.

## Consequences

- `renderer.async_compute true` turns it on, read at startup. With it off, there is no second queue and resources are not shared.
- The cache and the fog stay before the G-buffer, on the graphics queue: the measurements above show no cost to the order.
- Step 18.6 adds a depth pass for occlusion (Hi-Z), which is the fixed-function work async compute overlaps well. It will be measured again then.
- Other GPUs, AMD's in particular, are said to gain more from async compute. Only the RTX 3090 was measured. On some GPUs, concurrent sharing turns off the compression of images, a cost the setting avoids when off.
- `RendererTest.RunsTheCacheAndTheFogOnTheComputeQueue` runs frames with it, synchronization validation on. The render graph's tests cover where the queues fork and join.

## Alternatives considered

- **Leaving the code out,** as levels of detail were (ADR-0090): no machinery to keep up. But 18.6 brings the work async compute is made for, and the parts most likely to be wrong (the queues, the semaphores, the barriers) are now validated and tested.
- **Handing resources between queues** (queue family ownership transfers) instead of sharing them: no cost to compression, but a release and an acquire for each resource that crosses, which the graph would have to place.
- **Several forks and joins a frame:** more overlap to try, but neither placement tried paid, and each fork costs a submission.
