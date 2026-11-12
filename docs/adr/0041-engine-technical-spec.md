# ADR-0041: The engine technical spec

- **Status:** Accepted
- **Date:** 2026-11-12

## Context

The owner wrote a technical spec (2026-11-12, kept outside the repository) for the engine as the base of a dark, indoor, first-person horror game: a fully ray-traced renderer, materials, water and wetness, lamps and exposure, horror features (flashlight, volumetrics, ray visibility masks, dying lights), ray-traced audio, VFX and fire, animation, physics, AI, cinematics, UI, accessibility, production systems and a photo mode, with an implementation order. The game it aims at is the demo's direction too: inspired by Doom, with an oppressive, horror atmosphere.

Much of it is done or already on the roadmap (Phase 9, ADR-0028). This ADR records, for the rest, what joins the roadmap, in what order, what changes on the way, and what stays out. It does the same for the spec as ADR-0028 did for the rendering plan.

## Decision

### Kept as decided

- **Own implementations** of ReSTIR, the radiance cache and the denoiser. The spec names RTXDI, NRD, SHaRC, the Neural Radiance Cache, DLSS Super Resolution and Ray Reconstruction, and RTX Character Rendering: all are under the NVIDIA RTX SDKs License, whose section 4(e) conflicts with CC BY-NC-SA 4.0 (ADR-0028). Their algorithms are implemented from the papers, as today. The denoiser's inputs and outputs stay the seam where another would fit (ADR-0039).
- **Upscaling with FSR 2** (MIT). XeSS only if its licence allows it.
- **One binding model**, descriptor indexing. `VK_EXT_descriptor_heap` waits until it is KHR and a measurement shows the binding model costs anything: two binding models means two paths to test, on one GPU.
- **Async compute and resource aliasing** when a measurement asks for them (ADR-0028). The render graph exists; the spec's "build it early" is met.
- **Ray queries in compute and fragment shaders**, not ray tracing pipelines (ADR-0031). Pipeline libraries, which the spec asks for ray tracing pipelines, do not apply.
- **Primary visibility stays rasterized** (the pre-pass, ADR-0028). Every spec feature that changes what the camera sees (visibility masks, decals, wetness) must work in the mesh pass as well as at ray hits: through the one material function both call.
- **One test GPU**, an RTX 3090. The spec's minimum is "RTX 3000 class"; a 3090 is the top of it. 60 fps on an RTX 3060 is not verified, and the Low and Medium presets are tuned by estimate (half the 3090's budget) until a slower GPU is available.

### Joined to the roadmap

In `OVERVIEW.md`, section 26:

- **Phase 9** (ray-traced rendering) ends with: reflections and glass with reflection motion vectors (9.10); **physical light units** (9.11, new); ReSTIR DI and GI with emissive lights, validated on a dark corridor (9.12); upscaling, **dynamic resolution**, eye-like exposure, bloom, AgX, HDR, brightness calibration (9.13); volumetrics (9.14); **quality presets and stutter-free pipelines** (9.15, new).
- **Phase 10, a level editor** (new): a small one, to import models, place lights, choose the sky and save levels; see below.
- **Phase 11, materials, lights and atmosphere** (new): the OpenPBR model through glTF's material extensions, IES profiles, cookies and area lights, lights and emission driven by one parameter, the flashlight, ray visibility masks, decals at hits, wetness and puddles, particles in the acceleration structure, camera effects.
- **Phase 12, sound** (new): Steam Audio for HRTF, occlusion, transmission, reverb and propagation; acoustic materials; buses, snapshots and ducking; dynamic music.
- **Phase 13, gameplay systems** (was Phase 10, widened): navigation, perception from light and noise, behaviour trees, events and sequences, a sequencer for cutscenes and scares, save and load, physics interactions, ragdolls, cloth, animation layers and IK, morph targets.
- **Phase 14, Linux validation** (was 11).
- **Phase 15, content pipeline and production** (was 12): mesh optimization, accessibility, subtitles and localization, haptics, diegetic UI, crash reports, streaming when measured, photo mode.

### Changed from the spec

- **Physical units first.** Lights move from the legacy intensity and range (ADR-0022) to lumens and candela with inverse-square falloff, emission to nits, exposure to EV, before ReSTIR: emissive triangles that are lights must be in the same units as the lights, and auto-exposure needs an absolute scale. The factory's lights are retuned in the same step.
- **Materials through glTF.** OpenPBR is the parameter model, but its layers come from glTF's `KHR_materials_*` extensions (clear coat, sheen, transmission, volume, IOR, anisotropy, iridescence, emissive strength), which Blender exports. No own material format. Lobes arrive one at a time, each with a use in the factory or a test scene, and each in the path tracer in the same step.
- **A small level editor, not the spec's full one.** The spec asks for an editor with a ray-traced viewport, gizmos, prefabs, undo, material, light and painting tools, and script editing. The owner wants one to import models, place lights and choose skies, simple for now. Phase 10 builds that: a level file that lists models, lights, the sky and named entities; an editor executable with the game's renderer as its viewport, selection, gizmos, an inspector, undo and redo; the game loads the level files it saves. It replaces "a level is a glTF file plus code" (ADR-0017): lights that the factory adds in code move into its level file. Models are still made in Blender. Later phases add their own fields to the inspector (light profiles, visibility masks, porosity, triggers) as they come; prefabs, painting, material and VFX editors, and the sequencer's timeline wait until content asks for them. It comes after Phase 9, so that its viewport is the finished renderer, and before Phase 11, whose lights and materials are edited in it.
- **Ray-traced audio uses Steam Audio** (Apache 2.0), not the render BVH. Rays for sound run on the CPU against the physics world or Steam Audio's own scene, over several frames; reading GPU acceleration structures back for sound would add latency and tie the audio to the renderer. OVERVIEW 2.2 already excludes an audio engine written from scratch.
- **Scripting waits.** Scares and light sequences start as data: timelines of light, sound and camera cues, triggered by events (Phase 13). A language (Lua through sol2) comes when someone who does not write C++ authors content, with a decision then.
- **The job system, custom allocators, level streaming and texture streaming wait for measurements.** Rendering is bound by the GPU; the factory loads in one piece and fits in VRAM. `VK_EXT_memory_budget` joins the statistics now so that a level that does not fit shows up. Streaming, when it comes, streams mip levels by room, not sparse textures, whose support and speed differ by vendor.
- **Low-light quality is validated in 9.12**, on a dark corridor lit only by the flashlight, not at the end: it is the spec's main risk.
- **The photo mode is engine-side, late** (Phase 15): the path tracer must first render everything the real-time frame does (volumetrics, glass, particles, depth of field). ADR-0028's "development tool only" becomes "development tool, and later the photo mode".

### Deferred (no step yet; each needs a reason to start)

ReSTIR PT (mirrors get recursive reflections in 9.10 first), random-walk subsurface scattering, strand hair and skin rendering, deep water with refraction and caustics, the GPU leak simulation, grid-based fire (flames start as flipbooks plus a flickering light, the spec's Low tier), video textures, opacity micromaps (until alpha-tested geometry measurably costs), shader execution reordering (no gain on an RTX 3090, and it needs ray tracing pipelines), adaptive ray budgets, motion matching (needs a motion-capture database), facial animation and lip sync (needs voiced dialogue), destruction, the VFX editor, USD import, mod support.

### Out

- The neural tier, NRC and frame generation (ADR-0028).
- FMOD and Wwise: proprietary licences.
- Opt-in telemetry: it needs a server and a privacy policy, for no engine need.
- Co-op networking: OVERVIEW 2.2 excludes a networking framework. The spec asks for the decision early because it shapes the core; it is no.
- VR: a 90 fps stereo target with path-traced lighting does not fit an RTX 3000-class GPU.
- Steam Deck: no hardware ray tracing fast enough.

### Rules taken from the spec

- **A debug view per feature**, in the developer tools, as every 9.x step already has.
- **Quality presets** (Low, Medium, High, Ultra) map to settings; a feature with a cost has a setting. Presets come together in 9.15, not one tier switch per feature from the start.
- **Hardware-specific features are optional** and the engine runs without them.
- **Physically based units** everywhere from 9.11.
- **No pipeline is created during gameplay**: the pipeline cache persists on disk (9.15), and a pipeline created after loading is logged as a bug.
- **Test scenes**: the Cornell box, then a mirror room (9.10), a dark corridor with only the flashlight and a room of 1,000 lights (9.12), a flooded room (Phase 11). Each runs golden-image tests against the path tracer.

## Consequences

- The roadmap gains three phases (the level editor, materials and atmosphere, sound); the former Phases 10 to 12 become 13 to 15, and `docs/DEMO.md` follows. Older ADRs keep the numbers of their time.
- The factory's lights are retuned in 9.11 and look different for a step.
- Without NVIDIA's SDKs, the low-light quality the spec hopes to get from Ray Reconstruction depends on the own denoiser: 9.12 measures it on the dark corridor before Phase 11 builds on it.
- Levels get a file format of their own, which ADR-0017 avoided; its details are decided in Phase 10's ADR.
- The demo keeps Doom's pace and adds the spec's oppressive atmosphere (`docs/DEMO.md`).

## Alternatives considered

- **Taking the spec as written**: NVIDIA's SDKs would need a change of licence (ADR-0013), and its order (editor and scripting before materials) would put the largest tools before the content that justifies them.
- **Levels authored in Blender only**, with engine data in glTF extras and live tuning in the developer tools: no editor to build, but lights, skies and placement would stay in code or in hand-edited properties. The owner chose an editor.
- **The spec's full editor before materials**: prefabs, painting tools and script editing before the content that would use them.
- **A custom ray-traced audio on the GPU**: reuses the acceleration structures, but adds a readback, ties audio to the renderer, and redoes what Steam Audio does.
