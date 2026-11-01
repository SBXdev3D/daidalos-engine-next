# ADR-0011: Threading model

- **Status:** Accepted
- **Date:** 2026-10-27

## Context

OVERVIEW §7.4 favors single-threaded execution until a workload requires otherwise. Some concurrency exists regardless: the GPU runs in parallel with the CPU, the physics library uses a job system, and loading screens cannot animate if assets are decoded on the main thread.

## Decision

- Game logic, ECS updates, and render submission run on the main thread.
- GPU work runs with several frames in flight; GPU resources are released through a deferred-destruction queue.
- The physics library's job system is accepted and confined to the physics module.
- One worker pool decodes assets from Phase 5. Results are delivered only on the main thread, and cancelled jobs are never delivered.

Implemented in Phase 5 as `core::WorkerPool` (ADR-0017).

## Consequences

- Thread ownership rules stay simple: ECS and renderer state are only touched by the main thread.
- No general job system or lock-free structures before a measured need.

## Alternatives considered

- Fully single-threaded: loading freezes the window.
- General job system from the start: premature (OVERVIEW §25.4).
