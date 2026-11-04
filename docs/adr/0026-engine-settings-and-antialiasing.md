# ADR-0026: The engine's settings file, and antialiasing

- **Status:** Accepted
- **Date:** 2026-11-04

## Context

Settings are cvars (ADR-0015), and until now the sandbox's `sandbox.cfg` held all of them: the engine's (window, rendering, sound) and the game's. A second game would have had to copy them. Players also expect a few settings that the engine did not offer: how many lights cast shadows, antialiasing, the view distance and the field of view.

## Decision

- **One engine settings file, `daidalos.cfg`.** Its commented template is `config/daidalos.cfg`, and `dai_add_app` copies it next to every executable the first time, then leaves it alone: it is the player's. `ApplicationConfig::configFiles` lists the files to read, in order, `daidalos.cfg` by default; relative paths are next to the executable, missing files are skipped, and later files and then the command line win. A game adds its own file after the engine's: the sandbox keeps only its game settings in `sandbox.cfg`. Mouse sensitivity, which every game needs, becomes the engine setting `input.mouse_sensitivity`.
- **New settings**:
  - `renderer.shadow_lights` (0 to 16, 4 by default) and `renderer.shadow_resolution` (256 to 4096 texels per light, 1024), read at startup. The shadow atlas becomes the smallest square grid with a tile per light.
  - `view.fov`: degrees **across** the screen (90 by default), as games show it; the vertical angle follows the window's shape. A camera's own vertical angle becomes optional, and only cameras without one use the setting.
  - `view.distance` (150 m): a far plane in the projection. Shaders turn depth into meters with `LinearDepth`, which accounts for it. The light clusters span from the near plane to the far plane, and lights and rigid meshes wholly beyond it are skipped; such meshes still cast shadows into the view.
  - `renderer.antialiasing`: `off`, `fxaa` (the default), `msaa2`, `msaa4` or `msaa8`.
  - `window.fullscreen`: the window covers its display without borders, at the display's resolution.
  The renderer follows antialiasing, the field of view, the view distance and fullscreen while running.
- **FXAA** runs after tonemapping: tonemapping writes sRGB-encoded colors and their luma to an 8-bit image, and a pass after FXAA 3.11 finds the edges, follows them to their ends and blends across them into the back buffer.
- **MSAA** multisamples the pre-pass and the mesh pass. They draw into multisampled depth, normals and HDR targets, which resolve at the end of each pass into the single-sample targets that ambient occlusion, particles and tonemapping read: color and normals by averaging, depth by keeping the first sample. The RHI gains sample counts on textures and pipelines, and resolve targets in `BeginRendering`. A change of sample count waits for the GPU between two frames, recreates the targets and rebuilds the mesh pass's pipelines.

## Consequences

- Any application gets the engine's settings with no code; a game only adds its own file.
- At 1280×720 in Release on an RTX 3090, FXAA costs 0.02 ms, and 4 and 8 samples add about 0.08 and 0.10 ms to the pre-pass and the mesh pass.
- FXAA also softens textures a little; MSAA keeps them sharp, but only smooths the edges of geometry: alpha-tested surfaces stay jagged, since there is no alpha to coverage, and so do edges inside shaders.
- Particles and ambient occlusion work on the resolved targets, as before.
- The view distance cuts the scene sharply where fog does not hide it.
- A wide field of view stretches the weapon held in front of the camera, which has no field of view of its own.
- An old `sandbox.cfg` that still holds engine settings keeps working, since cvars do not care which file sets them; only `sandbox.mouse` is reported as unknown, as it is now `input.mouse_sensitivity`.

## Alternatives considered

- A settings file per game, as before: each game would repeat the engine's settings and their documentation.
- Settings in JSON or TOML: structure that cvars do not need; one `name value` per line stays readable and hand-editable.
- TAA: the best quality for the cost, but it needs motion vectors, skinned zombies included, and work against ghosting; it can be added as another mode.
- SMAA: sharper than FXAA, but it needs lookup textures and three passes.
- Multisampled sampling of depth in ambient occlusion and particles: exact at edges, but every pass that reads depth would need two versions of its shaders.
- A vertical field of view: what the engine used, but players and other games give it across the screen.
- A view distance that only culls meshes: the level is one large mesh, so nothing visible would change.
