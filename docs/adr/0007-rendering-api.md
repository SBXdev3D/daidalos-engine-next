# ADR-0007: Vulkan 1.3 as the single rendering API

- **Status:** Accepted
- **Date:** 2026-10-27

## Context

OVERVIEW originally planned a custom RHI with DirectX 12 first and Vulkan in a late phase. Vulkan runs on both Windows and Linux. Two backends double the work for every renderer feature, and building the RHI against DirectX 12 alone for most of the project would bake DirectX 12 assumptions into it.

## Decision

- Vulkan 1.3 is the only planned backend, on Windows and Linux.
- Required features: dynamic rendering, synchronization2, descriptor indexing (bindless). The Vulkan Memory Allocator (VMA) handles GPU memory.
- A thin RHI layer sits between the renderer and Vulkan. Its purpose is to keep Vulkan types out of the renderer and to allow a mock device in tests, not to support every possible backend.
- Validation layers are enabled in Debug builds. A clean validation log is part of the definition of done for graphics work.
- GPU resources are destroyed through a deferred-destruction queue tied to frame fences.

## Consequences

- Linux works from the first renderer instead of a late phase.
- RenderDoc and the validation layers are the main debugging tools on both platforms.
- The former Phase 10 (Vulkan backend) becomes Linux validation.
- Adding DirectX 12 later requires a new ADR.

## Alternatives considered

- DirectX 12 first, Vulkan later (original plan): two backends to write and maintain.
- DirectX 12 first with an early Vulkan prototype: still two backends.
- An existing RHI (NVRHI, SDL3 GPU, Diligent): less to write, but low-level graphics understanding is a project goal (OVERVIEW §1.1). SDL3 GPU also lacks bindless resources.
