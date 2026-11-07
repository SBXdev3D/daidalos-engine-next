# ADR-0007: Vulkan 1.3 as the single rendering API

- **Status:** Accepted, amended 2026-10-30, 2026-11-01 and 2026-11-07
- **Date:** 2026-10-27

## Context

OVERVIEW originally planned a custom RHI with DirectX 12 first and Vulkan in a late phase. Vulkan runs on both Windows and Linux. Two backends double the work for every renderer feature, and building the RHI against DirectX 12 alone for most of the project would bake DirectX 12 assumptions into it.

## Decision

- Vulkan 1.3 is the only planned backend, on Windows and Linux.
- Required features: dynamic rendering, synchronization2, descriptor indexing (bindless). The Vulkan Memory Allocator (VMA) handles GPU memory.
- A thin RHI layer sits between the renderer and Vulkan. Its purpose is to keep Vulkan types out of the renderer, not to support every possible backend. It is made of concrete classes (amended 2026-10-30, see below).
- Validation layers are enabled in Debug builds. A clean validation log is part of the definition of done for graphics work.
- GPU resources are destroyed through a deferred-destruction queue tied to one timeline semaphore (amended 2026-10-30, see below).

## Consequences

- Linux works from the first renderer instead of a late phase.
- RenderDoc and the validation layers are the main debugging tools on both platforms.
- The former Phase 10 (Vulkan backend) becomes Linux validation.
- Adding DirectX 12 later requires a new ADR.

## Alternatives considered

- DirectX 12 first, Vulkan later (original plan): two backends to write and maintain.
- DirectX 12 first with an early Vulkan prototype: still two backends.
- An existing RHI (NVRHI, SDL3 GPU, Diligent): less to write, but low-level graphics understanding is a project goal (OVERVIEW §1.1). SDL3 GPU also lacks bindless resources.

## Amendment (2026-10-30): concrete RHI, timeline semaphore

**No virtual interfaces and no mock device.** With a single backend, an `IDevice` interface has one
real implementation, and a mock device can only check that calls were made, not that they render. The
RHI is made of concrete pimpl classes (`rhi::Device`, `rhi::Swapchain`, `rhi::CommandList`) with
generational handles. Code that reaches the GPU is tested on a real Vulkan driver: lavapipe in Linux CI,
the local GPU on development machines. Renderer logic that needs no GPU, such as scene extraction, stays
in plain functions with ordinary unit tests.

**One timeline semaphore instead of frame fences.** Every submission signals the next value of a single
semaphore. Destroying a resource records the value that the frame being recorded will reach, and the
object goes away once the semaphore has reached it. This gives frames in flight, immediate uploads and
deferred destruction a single ordering, without a fence per frame. Binary semaphores remain only where
the swapchain requires them.

## Amendment (2026-11-01): storage buffers, BC textures

**Push constants hold indices, data lives in storage buffers.** Two matrices filled the 128 bytes that Vulkan guarantees, with no room for a material. The global descriptor set gains a bindless array of storage buffers, next to the array of textures. Shaders declare it at `[[vk::binding(2, 0)]]`, once per element type. Per-frame data, per-instance data and the material table all live there, and the push constants carry indices into them. The lighting data of Phase 6 will do the same.

**Required device features** now include BC texture compression and storage buffer descriptor indexing. Shader demotion, which `discard` compiles to, is part of Vulkan 1.3.

## Amendment (2026-11-03): uploads are recorded, not waited for

`Device::UploadBuffer` and `UploadTexture` used to submit one copy and wait for the GPU each time, which made a level take tens of milliseconds to reach the GPU. They now record their copies into one pending command buffer. It goes to the queue, with a barrier, ahead of the next `BeginFrame`, `Submit` or `WaitIdle`, so everything submitted later sees the data, and its staging buffers are released by the timeline semaphore like any destroyed resource. Uploads remain forbidden between `BeginFrame` and `EndFrame`.

## Amendment (2026-11-07): Vulkan 1.4 and ray queries

Vulkan 1.4 replaces 1.3, and the device must support acceleration structures and ray queries, with buffer device addresses (ADR-0028, ADR-0031).
