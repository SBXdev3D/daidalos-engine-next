# ADR-0012: Indoor visibility

- **Status:** Accepted
- **Date:** 2026-10-27

## Context

OVERVIEW §2.3 and §10.11 mentioned frustum culling, spatial partitioning, portals, and BSP for indoor scenes.

## Decision

- Frustum culling against bounding volumes first.
- A bounding volume hierarchy for static geometry.
- Portal/cell visibility only if a representative test level shows a measured benefit, and only if the level format stores cells and portals.
- Hierarchical-Z GPU occlusion culling as the general alternative if needed.
- BSP is not planned.

## Consequences

- Visibility work starts simple, and every additional technique must be justified by measurements against a baseline.
- Level design is not constrained by BSP requirements (convex brushes, splitting).

## Alternatives considered

- BSP: designed for CPU visibility on older hardware, complicates level editing, and gives little that a BVH and GPU depth testing do not.
