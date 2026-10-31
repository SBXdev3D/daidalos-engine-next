# ADR-0002: Test framework and test layers

- **Status:** Accepted
- **Date:** 2026-10-27

## Context

Testing is part of the architecture (OVERVIEW §23). A framework is needed for unit tests, and graphics features need automated checks that do not require a physical GPU.

## Decision

- GoogleTest, run through CTest.
- Layers: unit tests (no window, GPU, or audio device), integration tests (subsystems with fakes), graphics tests (offscreen rendering on software Vulkan compared with golden images), and a short manual checklist per phase.
- Software Vulkan: lavapipe on Linux, SwiftShader on Windows.

## Consequences

- Each module has its own test executable under `tests/unit/<module>`.
- Golden images are stored per platform when software implementations differ.
- Visual Studio Test Explorer shows the tests.

## Alternatives considered

- Catch2 or doctest: lighter, with no decisive advantage over GoogleTest.
