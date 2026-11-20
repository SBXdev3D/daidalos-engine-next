# ADR-0116: Sound in the editor, scattering props, a camera preview

- **Status:** Accepted
- **Date:** 2026-11-20

## Context

Step 20.12 of Phase 20 (ADR-0104): proposal items 15, 19 and 21.

- **The editor was silent.** Its scenes had no audio, and the level had no sounds of its own anyway: the factory's hums, fans and alarms are added by the game's code from tags (ADR-0068). A designer could not place an ambient sound, nor hear one, before playing.
- **Props were placed one by one.** The paint tool (ADR-0078) stamped decals and wet patches only.
- **Nothing showed what a security camera or a spot light looks at** without flying the camera there by hand.

## Decision

- **A `sound` component** (`scene::LevelSound`): a clip relative to the level's file that loops where the entity is, on the ambience bus, at full volume within a distance and faded out at another. It can follow its entity's light, as the game's hums do. `LevelInstance` plays it when it makes the entity and stops it when the entity goes. The scene's existing `SoundEmitter` keeps it where the entity moves. The game plays these sounds too, beside those its code adds.
- **Hearing the level in the editor (Show > Hear the level), off by default.**
  - The editor's scenes now have audio, paused unless asked. The listener is the editor's camera, as the scene already sets it.
  - Scenes of tabs not shown, and the editor's scene while the game plays in its window (ADR-0110), are paused.
  - `.wav` and `.ogg` files show in the asset browser as sounds; dropping one in the view places an ambient sound.
  - A selected sound draws its two distances on the ground. The inspector edits its clip, volume, distances and light.
  - The map check reports a missing clip.
- **Scattering:** the paint tool gains a third brush, Props.
  - "Take the selection's models" fills its list. Each stamp of a stroke places one of them, chosen at random, at the stroke's spacing, turned at random about its up and sized by the jitter.
  - With "Turn to the surface", its up follows the surface's normal: moss on a wall, bolts on a slope.
  - A stroke's props hang under one entity and are one step of the history, as painted decals are (ADR-0078).
- **A camera preview.**
  - **Look through** (right-click menu, or the Camera preview panel) moves the editor's camera to the selection, looking along its -Z: a security camera's or a spot light's view. Escape goes back.
  - **Take a picture** draws twelve frames from there, so the denoiser and the eye's exposure settle, captures the last, and goes back. The Camera preview panel keeps the picture. The view shows the selection's for those frames (a fifth of a second).

## Consequences

- The editor's scenes load and decode the level's sound clips, even unheard.
- A picture cannot follow the selection live: the renderer draws one view a frame (ADR-0108). Look through is the live way to see from it.
- Reverb zones would need the acoustic simulation the game builds from its level geometry (ADR-0065), which the editor does not build. They are left out.

## Alternatives considered

- **Hearing the game's own sounds in the editor** (fans, alarms, hums by tag) by running the game's rules there: they belong to the game, which the editor now plays in its window (ADR-0110). A component lets any level have sounds the editor knows.
- **A second renderer view for a live picture-in-picture:** twice the frame's targets and history for a small window.
