# ADR-0048: Image formation: auto-exposure, bloom, AgX and HDR output

- **Status:** Proposed
- **Date:** 2026-11-14
- **Amends:** ADR-0041 (step 9.14 is split in two; the steps after it move up by one), ADR-0043 (how exposure reaches the GPU)

## Context

Step 9.14 of the roadmap (ADR-0041) held seven features: FSR 2 upscaling, dynamic resolution, auto-exposure, bloom, AgX tone mapping, HDR output and a brightness calibration screen. The image is formed today by a fixed exposure per scene (`Atmosphere::ev100`, ADR-0043), the menu's Brightness in stops added to it, temporal antialiasing at full resolution and the Khronos PBR Neutral tone map into an sRGB swapchain. The interface and the developer tools are drawn over the tonemapped image, on the swapchain.

The GPU works with light already multiplied by the exposure (ADR-0043): lights, the sky and emission are multiplied when the frame is uploaded, so that half floats and the radiance cache's fixed point (1/1024, samples clamped at 256) keep values near the screen's. ADR-0043 left a debt for auto-exposure: a change of exposure leaves every history (the cache, the denoiser, temporal antialiasing, the reservoirs) off by its ratio until it forgets it.

The game is a horror game. Auto-exposure as cameras do it would undo the darkness it depends on: a dark corridor would be metered up to grey.

## Decision

### Two steps

- **9.14, image formation**: auto-exposure, bloom, AgX, HDR output and the brightness calibration screen. They act on the final HDR image, whatever its resolution.
- **9.15, upscaling and dynamic resolution**, measured on the finished look. Volumetrics become 9.16, quality presets and pipelines 9.17.
- **The upscaler is the engine's own temporal upscaling** (TAAU), grown from the temporal antialiasing of ADR-0037, behind a seam where FSR could go. FSR 2, which ADR-0041 chose, has not changed since 2023; FSR 3.1 replaced it. The current FSR SDK (2.x) is distributed as prebuilt libraries, which Phase 14 (Linux) cannot take. FSR 3.1 is added from source only if TAAU at 67% loses against native resolution (FLIP and screenshots); 9.15's ADR records the measurement.

### Auto-exposure

- **A level's exposure range.** `Atmosphere::autoExposure`, `ev100Min` and `ev100Max`: the eye adapts between them and never beyond. The darkest a level may look is its author's choice, and a dark corridor stays dark. Without auto-exposure, `ev100` is fixed, as before; the test scenes keep it.
- **Metered on the GPU, adapted on the CPU.** A compute pass builds a histogram of the frame's luminance in absolute units (log2 nits, 128 bins), weighted towards the center of the screen, where the player looks and the flashlight points. It is copied to a buffer the CPU reads when the frame's slot comes back, `kFramesInFlight` frames later, as the GPU timings are. The CPU averages the bins between the 40th and the 95th percentile, turns the average into EV100 (`log2(L × 100 / 12.5)`, photography's reflected-light meter) and moves the eye towards it: fast towards bright light (0.5 s), slowly into the dark (3 s), as eyes do. Two frames of latency are nothing next to adaptations of seconds, and the rules stay testable on the CPU.
- **The GPU's exposure is fixed per level.** Light is pre-exposed at the middle of the level's range (`(ev100Min + ev100Max) / 2`), not at the eye's current EV. The difference, and the menu's Brightness, are a scale applied in the output pass. No history needs rescaling: ADR-0043's debt is paid by not incurring it. The ranges this allows are bounded by the precision of the cache and of half floats: a range wider than 8 stops is logged as a warning (4 stops either side of the middle stays inside the cache's 1/1024 and 256). The radiance clamp on indirect bounces (ADR-0045) is set in what the screen shows, so it is divided by the output scale.
- **Brightness no longer changes the GPU's light**: it moves the output scale, and its histories no longer go stale when the slider moves.

### Bloom

- **Without threshold, energy-conserving**: the post-antialiasing image is downsampled six times (13-tap filter, a Karis average on the first, against fireflies) and upsampled back with a 3 × 3 tent, each level added to the next, and 4% of the result replaces 4% of the image (Jimenez, "Next Generation Post Processing in Call of Duty: Advanced Warfare", 2014). Everything glows a little, bright things visibly; nothing pops at a threshold. `renderer.bloom` sets the share (0 turns it off).

### AgX

- **AgX replaces PBR Neutral**: the Blender and Filament transform (inset matrix, log2 encoding from -12.47 to +4.03 stops around middle grey, a sigmoid fitted by a polynomial, outset matrix), in Rec. 2020 as they do it. Bright lights go towards white instead of clipping to saturated primaries: lamps, emissive panels and the flashlight's hot spot. It desaturates bright textures, which PBR Neutral was chosen to keep (ADR-0022); `renderer.tonemap neutral` keeps PBR Neutral for comparison. A C++ twin (`DisplayTransform`) gives the same values on the CPU, for the calibration screen and tests.

### HDR output

- **HDR10**: an `A2B10G10R10` swapchain in the ST 2084 (PQ) color space, Rec. 2020 primaries, when the surface offers it, `renderer.hdr` is on and the window is on an HDR display. Otherwise sRGB, as before. `VK_EXT_swapchain_colorspace` is enabled when present; `VK_EXT_hdr_metadata` sends the peak brightness when present. Neither is required.
- **Paper white and peak from the system**: SDL3 gives the window's SDR white level and HDR headroom, which the system's own HDR calibration sets (Windows' HDR Calibration app). The game asks for neither; `renderer.hdr_paper_white` and `renderer.hdr_peak` override them, in nits.
- **AgX, then expanded**: the SDR transform forms the image; highlights are then expanded towards the peak by the inverse of a Reinhard curve on the largest channel, `x / (1 - x (1 - 1 / headroom))`: 1 becomes the headroom, middle grey moves by about 15%, hues stay. The SDR and HDR images look alike, the HDR one with brighter lights. A simple start, to be revised on the owner's HDR display.
- **The interface on a layer of its own.** The game's interface and the developer tools are drawn into an sRGB layer (`Rgba8Srgb`, premultiplied alpha), not on the swapchain. The output pass puts the scene, its exposure, bloom and display transform, and the layer together, at paper white, and encodes them for the swapchain: sRGB or PQ. The interface's pipelines no longer depend on the swapchain's format, which can change while the game runs. The layer costs a clear and a read at the window's size.

### Brightness calibration

- **On first launch** (`sandbox.calibrated` false), before the main menu: a symbol barely brighter than black, drawn in the color the display transform gives a fixed dark scene luminance at the chosen Brightness, and the Brightness slider; "Adjust until the symbol is barely visible". The color comes from `DisplayTransform` on the CPU, so the screen needs no pass of its own. It is also in the options. The HDR peak is the system's (above), so there is no peak screen.

### Debug views and statistics

- `renderer.debug_view bloom` shows the bloom alone; the statistics show the eye's EV100, the metered EV100 and the level's range; the developer tools draw the histogram.

## Consequences

- The factory changes look: AgX desaturates its brightest lamps, bloom softens them, and the eye adapts between dark rooms and lit ones within the range the level sets.
- Levels need an exposure range. The factory's is chosen by eye with screenshots, around its EV100 of 3.
- A level brighter than 8 stops end to end (a sunlit exterior seen from a dark interior) would need ADR-0043's history rescaling after all; the warning says when.
- The output pass replaces the tonemap pass; the interface and developer tools draw into a layer of their own.
- HDR output is checked on the owner's display, not by a test: tests cover the encoding functions on the CPU.

## Alternatives considered

- **Pre-exposure at the eye's EV, histories rescaled every frame** (Frostbite, Unreal): any range, but every temporal pass, the cache and the reservoirs carry the ratio; one forgotten history is a flash on every change. A horror game's levels need a few stops, not twenty.
- **Adaptation on the GPU**, the exposure in a buffer that shaders read: no latency, but lights are exposed on the CPU when they are uploaded, and the adaptation rules could not be tested on the CPU.
- **Auto-exposure without a range**: the eye of a camera, which would light up the dark.
- **FSR 2 or 3.1 now**: an external SDK with a Windows-only shader compiler or prebuilt libraries, before knowing whether the engine's own upscaler is enough.
- **scRGB output** (a linear half-float swapchain): blending stays linear, but its support on Vulkan is narrower than HDR10's, and the interface layer removes the reason to want it.
- **A peak brightness screen**: the system already knows the display's peak and the player has calibrated it there.
- **ACES**: a filmic look with hue shifts in bright saturated colors (blue lights go purple), which AgX avoids.
