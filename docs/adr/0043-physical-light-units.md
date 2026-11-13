# ADR-0043: Physical light units

- **Status:** Accepted
- **Date:** 2026-11-13

## Context

Step 9.11 of the roadmap (ADR-0041). Lights still used the legacy engine's terms (ADR-0022): a light's color was what a white surface facing it reflected up close, and it faded linearly from a fraction of its range, its falloff, to zero at the range. Exposure was a plain multiplier, and emission and the sky had no unit. ReSTIR with emissive triangles (9.12) needs lights and emission in the same unit, and auto-exposure (9.13) an absolute scale; the engine spec asks for candela, lux, nits and EV everywhere. The factory's lamps lived in the names of glTF nodes, `spot;light_color=0.5:0.46:0.4:1;light_range=12;...`.

The owner chose to keep the factory's look: converted lights are as bright as before where it matters, at an exposure for a dim interior, rather than real night-time values tuned again by eye; and to move level lights to glTF's `KHR_lights_punctual`.

## Decision

### Units

- **Point and spot lights in candela**, directional lights in lux. `renderer::PointCandela` and `SpotCandela` turn lumens into candela, for a light's whole sphere or its cone.
- **A color and a temperature.** `scene::Light::color` is a linear tint, `intensity` the candela or lux, and `temperature` an optional color temperature in kelvin: `ColorTemperature` gives the linear sRGB color of a black body (Kim et al.'s fit of the Planckian locus), at a luminance of 1, so that it changes the hue and not the brightness. 2700 K is a warm bulb.
- **The inverse square of the distance.** A light gives `I / d²` lux at `d` meters, never nearer than the surface of its sphere (1 cm at least), times its spot cone. Its range stays, since the clusters need one, but is now where a smooth window brings it to zero: `(1 - (d / range)⁴)²` (Karis 2013). `falloff` is gone. A light without a range, such as one from glTF, ends where it falls to 0.05 lux, between 1 and 64 m (`LightRange`).
- **No more factor of π.** Shading is the illuminance times the BSDF and the cosine, in the mesh pass, the radiance cache, reflections, glass, particles and the path tracer alike; a white matte surface facing `E` lux sends `E / π` nits.
- **Emission and the sky in nits.** glTF's emissive factor is multiplied by `KHR_materials_emissive_strength`; `Atmosphere::skyIntensity` turns the sky's colors into nits.
- **Exposure in EV100.** `Atmosphere::ev100` replaces the scene's exposure multiplier: what reaches the screen is luminance times `1 / (1.2 × 2^EV100)`, photography's saturation-based sensitivity. `renderer.exposure` becomes `renderer.exposure_compensation`, in stops added to the scene's (0 by default; the menu's Brightness goes from -1 to +1 EV). The sandbox's darkness setting shifts the factory's EV100 of 3 by up to 4 stops.

### Pre-exposure

The GPU works with light already multiplied by the exposure (the approach of Frostbite and Unreal): lights and the sky are multiplied when the frame is uploaded, and emission where shaders read it (`FrameData::exposure`). The radiance cache, which sums samples in fixed point up to 256, the denoiser, temporal antialiasing and the half-float targets keep values near the screen's, as before; the tonemapper's exposure is 1. A change of exposure, from the darkness setting today and from auto-exposure in 9.13, leaves the histories off by its ratio until they forget it: the cache in about 8 frames. 9.13 rescales them.

### Level lights in glTF

- **`KHR_lights_punctual`** is read by `resources` (`ModelData::lights`, `ModelNode::light`) and becomes a `scene::Light` on its node's entity when a model is instantiated. glTF lights shine down their node's -Z, as the engine's do.
- **The factory's 19 lit lamps** are lights of a child node of their fixture, `lamp light`, turned to point down; their attributes leave the node names, and the sandbox no longer builds lamps. One lamp was off in the legacy level and stays dark. `scripts/import_legacy_level.py` writes them so (`lamp_light`), and the level was converted with the same functions.

### Retuning

Each light is as bright as before where it mattered, at EV100 3, and falls off with the square of the distance from there: `I = old color × π × d² × 1.2 × 2³`.

- Lamps: at the end of their old full-strength zone, range × falloff. From 290 to 1390 candela.
- Flashlight: 4 m, 1090 cd. Matched at 3 m, it faded too fast down corridors.
- Muzzle flash: 2 m, 150 cd. Alarms: 8 m, 965 cd. The moon: 24 lux. The sky: 9.6 nits.
- The Cornell box and the mirror room are lit by spots that shine down, wide (60 to 85 degrees), instead of point lights: a light 25 cm under a ceiling lights a small patch of it so brightly, with the inverse square, that one ray per pixel finds it too rarely for its bounce to be smooth; and the original Cornell box's panel lights nothing above it either. The Cornell box's 500 cd keep the image within 4 % of its brightness before; its golden image is written again.

## Tests

- **Unit**: color temperatures (6504 K about white, warmer below and bluer above, luminance 1 from 1000 to 40000 K); lumens to candela for points and spots; the default range; each EV halves the light; a temperature changes a light's color but not its brightness; KHR_lights_punctual and the emissive strength are read.
- **GPU**:
  - 2π candela 2 m in front of a white matte quad make it reflect half a nit, less what its specular lobe takes, and four times less twice as far;
  - a light reaches zero at its range;
  - a model's glTF lights become lights of its entities, with the default range where it has none;
  - every comparison with the path tracer is measured again (FLIP on an RTX 3090): direct shadows 0.009 (0.014 before), soft shadows denoised 0.022 (0.032), the Cornell box's indirect light 0.165 (0.145), the mirror room 0.247 averaged and 0.255 denoised (0.183 and 0.192), with 0.333 without reflections. The rooms are now lit by spots that shine down: their ceilings have only the light that bounces up, where the cache is least exact.
- The path tracer's sum of its direct light against the mesh pass's is within 1 % instead of 0.5 %: with the inverse square, light changes faster across a pixel, which the path tracer averages and the mesh pass samples at its center.

## Consequences

- Lights are authored as in the real world and in Blender's glTF export; the units of 9.12's emissive triangles and of 9.13's auto-exposure are set.
- The factory looks almost as before: pools of light are brighter at their center and softer at their edge, the flashlight is brighter near walls and dimmer far away.
- Old `renderer.exposure` lines in settings files are ignored, with a warning.
- Particles' colors stay what they look like on screen: their emission is not in nits.
- Real night values (moonlight at 0.25 lux, an EV100 near 0) wait for auto-exposure, which will find them by itself.
- A point light near a surface makes a small, very bright patch whose bounce is noisy; clamping indirect light on bounces, in 9.12, reduces it.

## Alternatives considered

- **The legacy units, as ADR-0022 chose**: no conversion, but no way to put emissive surfaces and lamps in one unit for ReSTIR.
- **Real night-time values now**: a factory to tune again by eye, before the auto-exposure that makes them practical.
- **Keeping lamps in node names, in new units**: the editor of Phase 10 and Blender work with glTF lights.
- **Floating-point sums in the radiance cache instead of pre-exposure**: needs atomic float addition, which not every target GPU has, and leaves the half-float targets with values from thousandths to thousands.
