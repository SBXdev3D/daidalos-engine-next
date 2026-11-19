# ADR-0091: The bug sweep

- **Status:** Accepted
- **Date:** 2026-11-19

## Context

Step 17.4 (ADR-0088): the tests under AddressSanitizer, the shutdown that took seconds, settings a player's file kept after they were gone, and what earlier phases left open.

## Decision and findings

- **AddressSanitizer** (`windows-msvc-asan`) over the unit and GPU tests found two memory bugs that every other run had passed by luck:
  - **The level files' JSON reader** (since Phase 10, `JsonTree.cpp` since 15.5) looped over an array or object taken from a temporary simdjson result. The temporary died before the loop ran, which is a stack use after scope at every level, prefab and glTF material read. The array and the object are now held by name.
  - **Lightning bolts** (`MakeBolt`, since 9.22) kept a reference to the bolt's first path while pushing branches into the same vector, which can move it: a heap use after free each time a bolt branched. The factory's arcing cables and the effects room's storm made bolts every few seconds. The path is now copied.

  Either could explain the eight unexplained crash dumps of ADR-0080, all reading 0x60 in the sandbox, though their missing symbols leave that unproven.
- **Texture-less particle effects:** a Debug assertion forbade them, while the ray-traced path drew them as squares of their color and the raster pass sampled an invalid texture index. They are now allowed and drawn alike everywhere. The path tracer's tests use them.
- **The shutdown** takes 0.7 s from closing the window. It took 7.5 s in Phase 14, most likely the DirectInput enumeration that 11.5 turned off. Nothing more to do.
- **Settings the game no longer has** were warned about at every start, because the player's file kept them. They are now dropped from it at its next save, and the warning says so.
- **The Linux compile check** (ADR-0074) over every source changed since Phase 15 finds no error. `MeshPassTests.cpp` gets the `<algorithm>` include it lacked.

## Consequences

- An AddressSanitizer run belongs before every release: two bugs in a codebase with 929 passing tests show what it catches. The upscaling test times out under it (Debug, instrumented), and is left out of that run.
- The gameplay numbers of Phase 16 (ambushes, recoil, ammunition) were not tuned by playing. The assessment lists it.
