# ADR-0013: Licence

- **Status:** Superseded by ADR-0047
- **Date:** 2026-10-27

## Context

Legacy source files carried two different licences (zlib and CC BY-NC-SA 3.0). The project needs one licence.

## Decision

- The project is licensed under Creative Commons Attribution-NonCommercial-ShareAlike 4.0 (CC BY-NC-SA 4.0).
- A `LICENSE` file is added in Phase 0, and each source file carries the short header defined in `docs/CONVENTIONS.md`.

## Consequences

- Commercial use by others is not allowed. The copyright holder can still license the engine differently, including for their own commercial games.
- All planned dependencies use permissive licences (zlib, MIT, BSD, Apache 2.0, public domain) and are compatible.
- Creative Commons does not recommend its licences for software (no terms for source distribution or patents). PolyForm Noncommercial 1.0.0 was offered as a software-specific alternative and not chosen.

## Alternatives considered

- zlib or MIT: allows commercial use by anyone.
- PolyForm Noncommercial 1.0.0: written for software, same intent.
