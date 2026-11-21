# Assessment

*November 2026, at the end of the roadmap (Phases 0 to 17, ADR-0001 to ADR-0090).*

This is an honest look at where Daidalos Engine Next stands: the engine, the factory demo and the level editor, what is good, what is weak, and what to do next. Items marked **▲ Improve** are recommendations, roughly in order of value within each section. The last three sections answer the owner's questions:
- how performance can improve, and whether portal visibility exists;
- how the level editor compares with Unreal Engine, Unity and Godot;
- which high-quality visual techniques the engine does not have.

## At a glance

| | |
|---|---|
| Code | about 45,900 lines of engine C++ and Slang, 12,500 of applications, 19,000 of tests |
| Tests | 929 unit and GPU tests, golden images against the path tracer, smoke tests that play every scene with Vulkan validation, clean under AddressSanitizer |
| Decisions | 90 architecture decision records |
| Platforms | Windows (built, tested, shipped); Linux (compiles against Ubuntu 24.04's headers, not run since WSL2 is missing, ADR-0074) |
| Target | Vulkan 1.4 with hardware ray tracing: RTX 30, RDNA 2, Arc A or newer |
| Frame | 1080p, RTX 3090, dressed factory: Low 3.7 to 4.7 ms, Medium 6.4 to 7.6, High 11.7 to 15.0, Ultra 14.6 to 16.9 (ADR-0090) |

## The engine

### What is strong

- **The renderer is the engine's best part.** It renders a hybrid ray-traced image:
  - a G-buffer;
  - ReSTIR direct light from a thousand lights and from emissive triangles;
  - one-bounce indirect light through a radiance cache;
  - ray-traced reflections, glass with refraction, ray-traced shadows;
  - volumetric fog in froxels lit by every light;
  - particles in rays.

  Its frame is formed like a camera's: physical units, auto-exposure, AgX, HDR10, bloom, depth of field, motion blur, grain. It has its own temporal upscaler, with FSR 3.1 as an option, and dynamic resolution. A reference path tracer renders everything the frame does, and the golden tests compare the two.
- **Every decision is written down.** Ninety ADRs say what was chosen, measured and rejected. Few engines of any size can say why each part is the way it is.
- **Measured, not assumed.** Features came with their cost: the presets are measured, ReSTIR GI was built, measured and dropped, and levels of detail were measured and not built.
- **The systems a game needs exist and talk to each other:**
  - physics (Jolt);
  - navigation (Recast and Detour);
  - perception and utility AI;
  - Steam Audio with occlusion, reverb and pathing;
  - a layered score;
  - data-driven sequences;
  - saves, localization, accessibility, rebinding, photo mode;
  - crash reports.

  A level made in the editor gets all of them with no code.
- **Data is text and diffs well:** level files, prefabs, sequences, effects, text tables, settings.

### What is weak

- **One GPU vendor tested, one machine.** Everything was measured on an RTX 3090. AMD and Intel GPUs, and the 8 GB cards a minimum tier implies, have never run the engine.
  - ▲ **Improve:** run the golden tests and the budget on an RDNA 2 or 3 card and an RTX 3060 before trusting the presets.
- **Linux is unverified at run time** (ADR-0074).
  - ▲ **Improve:** install WSL2 or a Linux machine and run `scripts/validate_linux.sh`.
- **The GPU work is recorded by the CPU, draw by draw,** on one queue.
  - ▲ **Improve:** see *Performance* below.
- **No transparency by blending.** Glass is transmission and foliage is alpha-tested; smoke, holograms, dirty windows and stained glass cannot be drawn. The kit's blended materials had to be turned into glass or cut-outs.
  - ▲ **Improve:** a sorted forward pass for blended surfaces, lit by the same ReSTIR reservoirs, or weighted blended order-independent transparency.
- **The CPU side is single-threaded.** Scene extraction, animation, AI and command recording run on the main thread. The factory is small enough not to care.
  - ▲ **Improve:** ADR-0041's job system, when a level shows a CPU frame over its budget in Tracy.
- **Memory bugs hid behind passing tests.** AddressSanitizer found two in step 17.4 (ADR-0091): a use after scope in the level files' JSON reader since Phase 10, and a use after free in lightning bolts since 9.22. Either may explain the eight crash dumps of ADR-0080.
  - ▲ **Improve:** run the AddressSanitizer preset before every release, and keep PDBs for every build that runs for long. Crash reports are Windows only: Linux core dumps are not read yet.

## The demo

### What is strong

- The factory is a complete level:
  - an opening flight;
  - a dressed route through fourteen rooms;
  - scares that use the power circuits, sound and AI;
  - an exit and an end summary.

  It shows nearly every engine feature in a place that needs it.
- The atmosphere has improved the most on the legacy demo: dark rooms the flashlight barely holds, failing lights, wet floors, green gas, rain through the roof, sounds through walls.

### What is weak

- **The level's architecture is still the 2008 one:** boxes of tiled floor and brick wall at a 10-unit grid, under the new props. The props dress it; they cannot hide that every room is a box.
  - ▲ **Improve:** rebuild the walls, floors and ceilings in Blender with real architecture: columns, beams, ducts at the ceiling, stairs, windows, height changes. Keep the route.
- **The art is mixed.** The legacy textures sit beside Poly Haven's scanned props. The zombie is now a detailed model in a hazmat suit (ADR-0119), but the only enemy.
  - ▲ **Improve:** one texture set for the walls, and a second monster, made the same way (ADR-0119).
- **It has not been played for balance.** Ambush radii, recoil, zombie numbers and the ammunition count are first guesses (ADR-0084, ADR-0085).
  - ▲ **Improve:** play it end to end, several times, and tune.

## The level editor

### What is strong

The editor is built on the game's renderer, so the viewport is the game's image. After Phase 20 (ADR-0104 to ADR-0118) it has:
- a hierarchy with folders, hiding, locking and isolating; an inspector of every component; gizmos with pivots, snapping to steps, surfaces and corners;
- perspective, top, front and side views, cut through ceilings and walls, alone or four at once;
- one undo history for the level, its materials, its sequences and its effects, with a History panel, and documents in tabs;
- Simulate, and the game played in the editor's window;
- prefabs with per-copy overrides edited in place, blocks to try a room, a paint tool for decals, water and scattered props;
- a live material editor, a sequence timeline, an effect editor with curves;
- a map check, statistics against the budgets, the assets' references and safe moves, import settings;
- commands with remappable keys, Ctrl+P to search everything, context menus;
- lighting modes and debug views, overlays drawn in the scene's depth, sound, a camera preview;
- a loader, autosave with recovery, and a smoke test that starts it.

Its documents are the same text files the game loads.

### Compared with Unreal Engine, Unity and Godot

| | Daidalos | Unreal, Unity, Godot |
|---|---|---|
| Viewport | Perspective, top, front, side, four panes; the live one drawn, the others their last image | Every pane drawn live; true orthographic projections |
| Play | In the editor's window or the sandbox; Simulate with pause and step | In the editor, with the running game inspected live |
| Prefabs | Nested, per-part overrides of transform, light, model, removal | Nested, overrides of any field |
| Geometry | Blocks (box, stairs, ramp, cylinder) | BSP, ProBuilder, CSG, terrain and foliage |
| Materials | Parameters of glTF materials, live | Node graphs |
| Logic | Data sequences | Blueprints, visual scripting, script editors |
| Effects | A panel with curves, previewed by the level's emitters | Graph editors with their own preview viewport |
| Undo | Level, materials, sequences, effects | Every edit |
| Assets | References, safe moves, import settings for models | Dependency viewers, redirectors, import settings for every kind |
| Interface | Dear ImGui, themed, icons, context menus, a command palette | Native toolkits, fully themable |

▲ **Improve, in order of what level designers would feel next:**
1. **Inspect the game while it plays in the editor:** today the game takes the window; its entities could be shown in the hierarchy and inspector, read only.
2. **Overrides of any field** of a prefab's part, not only its transform, light and model.
3. **A grid texture for blocks,** and turning a blockout into a glTF for Blender.
4. **Import settings for textures and sounds,** beside models'.
5. **Panes drawn live,** if the renderer ever draws more than one view a frame.

## Performance

### Is portal visibility implemented?

Yes, since step 17.1 (ADR-0089). It was not before: ADR-0073 had deferred it, and the dressed factory showed it was needed.
- Rooms and doorways are boxes in the level file. The renderer walks from the camera's room through the openings it can see.
- Draws, decals and water in rooms it cannot see are skipped.
- From the start room, the triangles drawn fell from 1.8 million to 53,000, and the frame on High from 12.8 to 11.5 ms.
- The factory's main floor, yard and pit have cells; the upper floor and the underground do not yet.

### What else would make it faster

▲ **Improve, by expected gain on the measured frame (High, loading floor, 15 ms):**
1. **Indirect light (3.8 ms).** It traces a ray per pixel at the render resolution. Tracing at half resolution and upsampling with the G-buffer's depth and normals, as most engines do for diffuse light, would cut it by half or more. **Direct light (2.9 ms):** ReSTIR's candidates per pixel are the knob to measure against noise.
2. **Clustered decals and water.** Each pixel loops over up to 64 decals and 16 wet volumes. Portals removed the far ones, which saved more than a millisecond. A froxel or tile grid of decals, as lights already have, would make the cost proportional to what touches each pixel.
3. **Async compute.** Everything runs on one queue. The radiance cache update, the fog's froxels, particle simulation and the denoiser's passes can overlap the G-buffer and the ray-traced passes.
4. **GPU-driven rendering.** Draws compacted on the GPU, with two-phase occlusion against the previous frame's depth (Hi-Z). It needs no cells, catches occlusion that portals miss, and makes thousands of draws cheap. The RHI already has indirect draws.
5. **Reflections (2.6 ms on wet floors):** half resolution on rough surfaces, and reuse between frames for those that do not move.
6. **The denoiser (1.8 ms):** fewer spatial passes where a pixel's history is long and its noise low.
7. **CPU:**
   - a job system for scene extraction, animation and AI;
   - multithreaded command recording, once Tracy shows the main thread over budget.

   In the factory, the CPU is not the limit.

Measured and **not** worth doing now: levels of detail and meshoptimizer (half a million triangles cost 0.05 ms, ADR-0090); a second ray of indirect light on Ultra (invisible after denoising, ADR-0090); GPU-driven rendering with Hi-Z occlusion (the G-buffer is 0.7 to 0.9 ms and drawing nearest first made it slower, so hidden surfaces cost about 0.1 ms, ADR-0098); async compute on an RTX 3090 (slower, ADR-0095).

**Phase 18 did items 1, 2, 3, 5 and 6** (ADR-0093 to ADR-0097). At 1080p on an RTX 3090, the frame went from Low 3.8 / 4.8, Medium 6.5 / 7.7 and High 13.1 / 15.6 ms (start room / loading floor, before Phase 18, ADR-0093) to Low 3.2 / 3.7, Medium 5.0 / 5.7 and High 12.5 / 11.9 ms. The start room on High runs in one of two states about 0.5 ms apart, and the loading floor lost its overhead crane in the editor meanwhile.

**Phase 19 made the ray tracing faster at the same image** (ADR-0099 to ADR-0102): the top-level structure built for tracing, bottom-level structures compacted (30.7 to 13.6 MB), textures read at hits at the mip of the ray's cone. The loading floor takes 0.1 to 0.6 ms less on every preset: Low 3.7, Medium 5.5, High 11.6, Ultra 13.3 ms. What remains is traversal, shading at hits and ReSTIR's scattered neighbour reads; further gains trade image for time.

## Visual techniques not implemented

| Technique | Status | What it would add |
|---|---|---|
| Blended transparency (smoke sheets, holograms, dirty glass, stained glass) | Missing | The largest gap for content: anything partly see-through but not glass |
| ReSTIR PT (path-space reuse) | Deferred (ADR-0041) | Multi-bounce indirect light with ReSTIR's quality; recursive reflections and caustics through glass |
| Subsurface scattering (random walk) | Deferred | Skin, wax, marble, leaves |
| Hair and fur (strands) | Deferred | Characters |
| Anisotropy and iridescence (`KHR_materials_anisotropy`, `KHR_materials_iridescence`) | Missing | Brushed metal, soap films, oil on puddles: cheap and useful in a factory |
| A physical sky and volumetric clouds | Missing; the sky is a cube map | Time of day, clouds that move and light the scene |
| Water with refraction and caustics | Deferred | The flooded rooms the spec imagined |
| Simulated fire and smoke (fluid grids) | Deferred | Fire that moves with wind and fills rooms |
| Virtualized geometry (Nanite-like meshlets, mesh shaders) | Missing | Film-quality meshes at no cost per triangle; not needed at the factory's scale |
| Opacity micromaps, shader execution reordering | Deferred (ADR-0041) | Faster ray tracing of foliage; on hardware that has them |
| DLSS and XeSS | Out (licences; FSR 3.1 and the engine's own upscaler instead) | Sharper upscaling on NVIDIA and Intel |
| Frame generation, neural radiance caching | Out (ADR-0028) | Higher frame rates; a learned cache |
| Cloth and destruction | Deferred | Flags and curtains; walls that break |
| Texture streaming and virtual texturing | Waits for a level that does not fit | Larger worlds |

The renderer covers what a dark interior needs. For the next level of the factory's image, in order:
1. blended transparency;
2. anisotropic and iridescent materials;
3. ReSTIR PT for glass and multi-bounce light;
4. a physical sky for the yard.

## Where to go next

The roadmap is done. A next one could be, in the owner's order of priorities:

1. **The factory's architecture** rebuilt in Blender, the art unified, and the demo played and balanced.
2. **Inspecting the game while it plays in the editor,** and prefab overrides of any field (Phase 20 did the rest of the editor list).
3. **Blended transparency and material extensions.**
4. **AMD, Intel, lower-end GPUs and Linux** measured, with the presets and golden tests verified there.
5. **Clustered decals and async compute,** then GPU-driven rendering when a level needs it.
