# ADR-0057: Light profiles, cookies, and lights that flicker, fail and lose power

- **Status:** Accepted
- **Date:** 2026-11-17

## Context

Phase 11 step 2 (ADR-0041) asks for lights a horror game needs: IES profiles, cookie textures, rectangle and disk area lights, one parameter driving a light and its emissive mesh together, scriptable flicker, buzz, slow failure and power cuts, and presets (candles, lighters, emergency lights, screens). Lights are evaluated in many places: the lighting's ReSTIR passes, the fog, the reflections, the radiance cache and the path tracer, all through `Attenuation` in `MeshTypes.slangh`; emissive meshes are lights of their own through their triangles (ADR-0044).

## Decision

- **Profiles and cookies live in `Attenuation`.** A light carries the bindless index of a profile, a row of 128 half floats of its intensity by the angle from its direction, 0 to 180 degrees, read from an IES file (`resources::ParseIes`, LM-63 type C; a profile that varies around its axis is averaged around it), and, for a spot light, of a cookie, a grey image laid over its cone, with an up vector for the image. `LightShape` multiplies the attenuation by both, so every pass that lights anything, and the path tracer, shape the light alike. The GPU light grows from 64 to 80 bytes.
- **The profile shapes, the intensity stays the light's.** A profile is normalized to 1 in its brightest direction; the light's own intensity says how bright that direction is. The IES file's candela are logged, not applied, so that changing a lamp's profile does not change the level's exposure.
- **Animated lights are data in the light** (`scene::Light::animation`): a pattern (steady, flicker, buzz, failing, strobe, pulse), a rate, a depth, and a power circuit. `scene::LightFactor` is a pure function of the pattern, the time and a seed from the entity, deterministic and tested; `AnimateLights` writes each animated light's factor (`LightState`) each frame, before extraction, from the scene's light clock, which stops when the game pauses.
- **One factor drives the light and its lamp.** `AnimateLights` also writes an `EmissiveScale` on the lamp, the entity the light hangs from when it places a model (the factory's lamps are a model with their light as a child), or the light's own entity. Every draw under it carries the scale (`DrawItem::emissiveScale`, in the instance data): the G-buffer, ray hits and the emissive triangles that light the scene multiply their emission by it, the CPU weighs the lamp's triangles by it when it draws lights, a lamp at 0 gives no light at all, and one whose scale is not 1 counts as moved for the histories, as a flickering flame does (ADR-0053).
- **Power circuits are the scene's** (`Scene::Power()`): game code cuts or restores a circuit by name. A cut sags, catches once and dies over 0.35 s; a return stays dark a moment, blinks twice and comes on over 0.9 s, as fluorescent tubes do. Circuits no one switched are on.
- **Area lights are emissive meshes.** A rectangle or a disk that glows is an emissive mesh, which is a light already, shaded and shadowed as the path tracer sees it (ADR-0044), and animated with its lamp. No separate analytic area light is added.
- **Presets in the editor** (Create > Light presets): candle, lighter, buzzing tube, failing lamp, emergency light (on the `emergency` circuit), hazard strobe, and a lamp behind a fan. Three profiles (`downlight`, `narrow`, `bare_bulb`) and two cookies (a fan, a wire cage) are written by `scripts/make_light_profiles.py` from formulas, so they carry no outside licence.
- **Level files** keep `profile`, `cookie` and `animation` (`pattern`, `rate`, `depth`, `circuit`) on lights; their paths are relative to the level and move with Save As.

Measured on an RTX 3090 (`AProfileAndACookieShapeTheLightAsThePathTracerSees`): a point light shaped by a profile and a spot light by a cookie over a floor are 0.010 FLIP from the path tracer, and 0.85 without their shapes. Half an emissive panel's scale halves what it shows and lights, in the lighting (within 5%) and in the path tracer.

## Consequences

- A cookie is grey: colored cookies (stained glass) would make `Attenuation` return a color through every pass that uses it.
- A profile is the same all around the axis. A wall-washer's asymmetry is averaged away.
- Point lights now carry a direction, their entity's -Z, for their profile.
- Mesh particles keep their emission unscaled: they have no lamp.
- Screens as lights (a monitor's glow) are emissive meshes with an animated material: a video texture waits (ADR-0041 defers video textures).

## Alternatives considered

- **Analytic rectangle and disk lights with linearly transformed cosines** (Heitz et al. 2016): exact soft highlights for real-time, but a second kind of area light beside emissive triangles, its own sampling for ReSTIR and the path tracer, and a table to ship. The emissive triangles already light correctly, with shadows, in every pass.
- **Animating lights in game code each frame** (the sandbox's way for its emitters): no system, but each game would repeat it, nothing would be in the level file, and the editor could not show it.
- **A 2D profile texture** (around the axis too): keeps asymmetric lamps, at the cost of a 2D lookup and files that carry more than the factory needs.
