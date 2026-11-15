# ADR-0052: Quality presets chosen by measurement, and no pipeline in play

- **Status:** Accepted
- **Date:** 2026-11-15
- **Amends:** ADR-0041 (the presets are the settings the engine has, chosen at the first launch by measuring the GPU, not by its name), the rendering plan (section 12's presets table)

## Context

Step 9.18 of the roadmap, the last of Phase 9 (ADR-0041): Low, Medium, High and Ultra set the cost of every feature, chosen at the first launch from the GPU; the pipeline cache persists on disk and is rebuilt after a driver or GPU change; a pipeline created during gameplay is logged as a bug; VRAM use against `VK_EXT_memory_budget` in the statistics; each feature has its debug view.

What the engine had before this step:

- Every cost was its own `renderer.*` setting, set in `daidalos.cfg` or by the options, with no preset.
- The rendering plan's presets table (section 12) named features that were not built or were dropped: ReSTIR GI's resolution (ADR-0045), neural radiance caching, DLSS and XeSS, froxel grids of other sizes.
- Every pipeline was created at startup, without a `VkPipelineCache`. One could be created later: the output pass's, when HDR was turned on or off.
- Video memory was shown in the developer tools from VMA's estimate: its own allocations against 80% of the heap.

The factory at 1080p on an RTX 3090, everything on at full resolution, took 7.2 to 8.0 ms of GPU per frame: Phase 9's goal, 60 fps on High at 1080p on this GPU, was met before the step began.

## Decision

**Presets of the settings that exist.** `renderer.quality` is `low`, `medium`, `high`, `ultra`, or `auto` until the first launch has chosen. A preset decides eleven settings: the render scale and the lowest one dynamic resolution goes to, indirect light, its bounces and rays, reflections and the roughness they stop at, ReSTIR's spatial reuse, the volumetric fog, its slices and its cell size (`renderer.fog_tile`, new). Each was measured in the factory's big room at 1080p (below), and the presets were built from what each costs:

| Setting | Low | Medium | High | Ultra |
|---|---|---|---|---|
| Render scale, highest / lowest | 58% / 50% | 67% / 50% | 100% / 50% | 100% / 67% |
| Surfaces lit before the radiance cache | 0 | 1 | 1 | 2 |
| Rays of indirect light per pixel | 1 | 1 | 1 | 2 |
| Reflections up to roughness | 0.3 | 0.45 | 0.6 | 0.9 |
| ReSTIR spatial reuse | off | on | on | on |
| Fog slices / cell in pixels | 32 / 16 | 48 / 16 | 64 / 16 | 128 / 8 |

High is the engine's defaults: what it is tuned for, and what it looked like before the step. Medium is the minimum tier's (an RTX 3060 at 1080p, ADR-0041). The upscaler, antialiasing, the denoiser and the image's settings are not the presets': they are choices of taste or of comparison.

**A setting that is set wins over its preset.** A cvar now knows whether a config file, the command line or the player set it (`CVar::IsSet`); a setting the presets decide that nothing set follows the preset. One that is set makes the options show the preset as Custom; choosing a preset puts every setting it decides back to it (`CVarRegistry::Reset`, `Application::ChooseQuality`). `daidalos.cfg` sets none of them any more.

**The first launch measures, not a list of GPUs.** A table of GPU names goes stale and says nothing of the display's size. With `renderer.quality auto`, the first frames of the first level are drawn at High at full render scale, dynamic resolution off; after 90 frames, the next 120 are timed on the GPU. The best preset whose frame, estimated from its measured share of High's, fits the target frame rate's budget with a 25% margin is kept among the player's settings. Frames without a level, such as the loading screen, do not count.

**A pipeline cache on disk.** The device creates every pipeline through one `VkPipelineCache`, read from `pipelines.cache` next to the executable at startup and written when the device is destroyed. The file starts with the driver's version, then the driver's data, whose header names the vendor, the device and the cache's UUID: a cache from another GPU or driver is not given to the driver, and is built again.

**No pipeline after loading.** The device counts pipelines and the time they took. After the first frame that draws a level, the application locks creation: a pipeline created then is still created, but logged as an error with its name and time, counted, and shown in the developer tools. The output pass's pipelines are created at startup for every format a swapchain can take, so turning HDR on or off creates none. Reloading shaders is the developer's choice and is exempt.

**Video memory as the driver sees it.** `VK_EXT_memory_budget` is enabled when the device has it, and VMA reads the use and the budget from it. The statistics line shows them next to the frame times.

**Debug views.** Every Phase 9 feature has one, or a switch: ray tracing (`rays`), the path tracer (`path`), motion vectors (`motion`), normal maps (`normals`), indirect light and its cache (`indirect`, `radiance_cache`), reflections and glass (`reflections`), ReSTIR DI (`lights`), bloom (`bloom`), the fog (`fog`); the eye's histogram, the render size, video memory and the quality in the developer tools; `renderer.denoiser false` shows the light as traced.

## Measurements

RTX 3090, the factory's big room at 1080p, dynamic resolution off, the flashlight on. One frame's GPU time each, which moves by about 0.5 ms from frame to frame:

| From High (7.7 ms) | ms |
|---|---|
| Render scale 67% / 50% | 3.9 / 3.2 |
| No surface lit before the cache | 7.0 |
| Two rays of indirect light | 10.0 |
| No indirect light | 5.3 |
| No reflections / up to roughness 0.3 / 0.9 | 7.0 / 7.8 / 8.8 |
| No spatial reuse | 6.8 |
| Fog: 32 slices / 128 slices / off | within the noise; the fog costs 0.4 ms (ADR-0051) |

The presets, three frames each in two runs (Low in the second only: in the first, its highest scale was 67%, and it took 3.5 to 3.6 ms, little under Medium):

| Preset | GPU ms | Share of High |
|---|---|---|
| Low | 2.6 to 3.5 | 0.36 |
| Medium | 3.9 to 5.2 | 0.50 |
| High | 8.0 to 9.4 | 1 |
| Ultra | 12.8 to 16.7 | 1.65 |

On this GPU the first launch measured High at 7.9 ms and chose Ultra, which holds 60 fps at 1080p (75 fps measured). Low saves little over Medium: below two thirds of the resolution, the costs that do not scale with it dominate, which is why Low's highest scale is 58%.

Creating every pipeline took 76 to 89 ms with the driver's own cache off (`__GL_SHADER_DISK_CACHE=0`), 8 to 9 ms from the engine's. NVIDIA's driver keeps a cache of its own, so on this machine the gain is small; it counts for the first launch after a driver update, other vendors and Linux.

In the factory, with validation and synchronization validation on, at Ultra, no error was reported and no pipeline was created after loading.

## Consequences

- The minimum tier is not measured: no RTX 3060 is at hand. About 2.2 times slower than an RTX 3090, High at full scale would take about 17 ms there, and the first launch would choose Medium, about 9 ms. This is an estimate.
- The shares of High were measured in one room on one GPU. Where the costs that do not scale with resolution weigh differently, the choice can be a preset off; dynamic resolution holds the frame rate either way, and the player can choose another.
- Players who set a preset-decided setting before this step, in the options or in their copy of `daidalos.cfg`, see Custom until they choose a preset; the build copies `daidalos.cfg` once and leaves it alone.
- The menu's Indirect light switch gives way to Quality: twelve rows still fit a 1080-pixel canvas.
- `pipelines.cache` is written next to the executable, as `user.cfg` is.

## Tests

- **`RendererTest.CreatesNoPipelineOnceItHasDrawnAFrame`:** after the first frame, every debug view, both upscalers at three scales, antialiasing off and on, HDR and SDR, and every preset-decided setting at both ends: no pipeline is created, and validation reports nothing.
- **`Quality.*`:** High is the engine's defaults; each preset costs at least as much as the one below in every setting; names go both ways; the first launch's choice for an RTX 3090 (Ultra), a GPU 2.2 times slower (Medium), 120 fps (Medium), and the floor (Low).
- **`CVarRegistry.KnowsWhetherSomethingSetAVariable`**, **`CVarRegistry.ResetForgetsAValueKeptForAnUnregisteredName`**.
