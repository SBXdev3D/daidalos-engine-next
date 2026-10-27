# ADR-0015: Development tooling

- **Status:** Accepted
- **Date:** 2026-10-27

## Context

OVERVIEW placed debug UI and profiling late in the roadmap. These tools make rendering and engine work faster from the start.

## Decision

- Tracy profiler from Phase 1 (CPU zones, later GPU zones), compiled out when disabled.
- Crash minidumps on Windows from Phase 1.
- A small configuration-variable (cvar) system from Phase 1, used for render toggles and debug options.
- Dear ImGui debug overlay from Phase 3, compiled out of shipping builds.
- Shader hot reload from Phase 3 (ADR-0009).

## Consequences

- Measurements exist before any optimization (OVERVIEW §25.4).
- Debug tools never require gameplay code to access backend internals.

## Alternatives considered

- Add tools in Phase 8 as originally planned: slower development during the renderer phases.
