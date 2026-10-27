# ADR-0014: Repository hosting, CI and legacy code

- **Status:** Accepted
- **Date:** 2026-10-27

## Context

The repository was hosted on Bitbucket with an unused pipeline template. It also contained code from an earlier version of the engine.

## Decision

- The repository is hosted on GitHub: https://github.com/StephaneBX/daidalos-engine-next. The Bitbucket repository remains as the `bitbucket` remote until it is archived.
- CI runs on GitHub Actions: Windows (MSVC) and Linux (Clang), build and test on every push and pull request.
- Every CI step can also be run locally with the same CMake presets.
- Legacy code in `Core/` is deleted as each part is replaced; it is not kept in a legacy folder (git history keeps it).

## Consequences

- `bitbucket-pipelines.yml` is deleted.
- GPU tests in CI use software Vulkan (ADR-0002).

## Alternatives considered

- Keep Bitbucket Pipelines: the template never worked, and GitHub Actions provides Windows runners.
- Keep legacy code in a `legacy/` folder: clutter, and git history already preserves it.
