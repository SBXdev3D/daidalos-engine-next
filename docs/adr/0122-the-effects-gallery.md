# ADR-0122: The effects gallery

- **Status:** Accepted
- **Date:** 2026-11-23
- **Amends:** ADR-0071 (a fade cue; a particles cue sprays along its entity's +Y), ADR-0053 (a second test level, made to be filmed)

## Context

The owner wants a video of what the particles can do. The effects test room (ADR-0053, `Factory/EffectsRoom`) has every effect of the list, but it was made to measure them: bare boxes, six identical lamps, forty effects in rows next to each other. Filmed, it looks like a test.

They asked for a room per effect (snow, rain, fire, explosion, wind, fog, electricity, cryogenics, then water, steam and smoke, creatures and impacts as well), a cinematic camera that moves a little, and a fade out and in between rooms. They record the screen with OBS, so the tour plays in real time; they add titles and music when editing, so the tour shows no text and plays only the rooms' sounds.

Sequences (ADR-0071) already had what a film needs, except a fade: camera cues, smooth paths and focus pulls (ADR-0120), timed particles, sounds, shakes, moves and mix snapshots. Nothing started a sequence on its own except the factory's tour (`sandbox.tour`).

## Decision

- **A level of twelve rooms, made by a script.** `scripts/make_effects_gallery.py` writes `Factory/EffectsGallery.gltf` (the rooms' walls, floors, ceilings and fittings, of the factory's materials, with coloured strips of its glow) and `Factory/EffectsGallery.level.json` (props of the CC0 showcase kit, lamps and their prefabs, lights with patterns, emitters, ribbons, lightning, fog, wind, weather, wetness, decals and sounds) straight, without `gltf_to_level.py`'s markers, so that every kind of entity is at hand. The rooms are fire, explosion, electricity, rain, snow, wind, fog, cryo, water, steam and smoke, creatures, and impacts. Each is dressed and lit around its effect: particles read against dark backgrounds, lit from behind or the side.
- **Rooms 60 m apart.** Emitters beyond 50 m of the camera emit nothing (ADR-0053), so a room's effects start only when the camera is in it. The tour cuts to the next room while the screen is black and holds the black 1.6 s, so that smoke, steam and flames are there when the picture comes back; flies and moths are spawned in one go by cues at the cut.
- **The tour is data from the same script**, `Factory/Sequences/effects_tour.sequence.json`: per room a camera cue on a smooth path with a focus pull, 17 s (19 for the explosions), timed cues (blasts, sparks, a cryogenic jet, shots and their impacts, a wisp moved along a path), and fades. Each camera cue's keys last half a second past the next cut, so that the next cue takes the camera from it and the player's view never shows between rooms. Writing the shots next to their rooms keeps a prop and the camera that films it together; as for the factory tour, the script is the source. `--check` lists camera passes closer than 0.3 m to a wall or a prop's box.
- **A fade cue**: `{"fade": {"to": 1, "seconds": 1, "color": [0, 0, 0]}}` covers the picture with a colour, eased, from where the fade is (a fade cut short turns back without a jump). `Scene::Fade` gives the colour and how much; the game draws it over the scene and its subtitles, under its menus. Skipping a cinematic lifts it. The editor's sequence panel offers it.
- **A particles cue sprays along its entity's +Y**, not straight up: a shot's impact flies out of the wall it hits. The one particles cue of the factory's sequences has an unturned entity and does as before.
- **`sandbox.sequence <path>`** plays a sequence at the start instead of the menu and the opening, at the level's darkness, and hides the developer tools' frame summary, which a recording would show. `sandbox.tour` is still the factory's tour with its own darkness and music.
- **New effects**: cryogenics (`cryo_vapour`, heavier than air, which pours over an edge and spreads on the floor; `cryo_jet`; `frost_glitter`), the layers of an explosion (`explosion_flare` with a strong light, `explosion_fireball`, balls of the fire flipbook thrown out from the blast, `explosion_sparks`, `explosion_debris`, `explosion_shock` along the floor, `explosion_smoke`), `steam_vent`, wind (`wind_dust`, `wind_ash`, `wind_wisps`, which show the gale itself), `bonfire`, `barrel_fire`, `hearth_smoke`, `drip_edge`, `impact_puff`, `moths_swarm` and `flies_swarm` (the moths and flies made big enough to be filmed), and the ribbon `lightning_indoor`. `--only <rooms>` also writes a tour of those rooms alone, `effects_preview.sequence.json`, ignored by git, to look at them without waiting for the others.
- **What the screenshots taught**: particles read against dark, lit backgrounds, and thick room fog turns every room milky, so most rooms keep a thin haze (0.01 to 0.03 per meter) and light their effect from behind or the side; lit dust puffs went black outside the lamps' cones, so the range's impacts are unlit; a lamp in the shot blinds the moths around it, so their lamp is shaded and they glow on their own; decals in place before the blasts read as holes in the floor and were dropped. A lightning flash multiplies its light, so a storm flashes a light of its room, never the moon: the moon lights the whole level, and a flash of it, every few seconds, showed in every room as a burst of noise while the denoisers caught up; the indoor strike's light is never quite off (1 cd), or the flash would multiply nothing.
- **The rooms are walls for physics and sound**: the script writes `EffectsGallery.gltf.import.json` with `level_collision` (ADR-0117), so that the rooms are static bodies, a navigation mesh and acoustic geometry. Without it, every room's loops were heard in every other: no wall stopped them, and past its maximum distance a sound kept a floor of its volume (minimum over maximum) and sent its square root to the listener's reverb. Through Steam Audio a sound now fades out past its maximum distance, gone at twice it, and sends to the reverb only what comes through the walls (ADR-0065, ADR-0066).
- **Room sounds** are made by `scripts/make_ambience_sounds.py` from noise, filters and partials, with fixed seeds, as the surfaces' (ADR-0067): loops of fire, rain, wind, hum, steam, water, cryo and flies, and an explosion and a zap for cues, in `Factory/Sounds/Ambience`. The tour mutes the music bus at its start.

## Consequences

- `+sandbox.scene Factory/EffectsGallery.level.json +sandbox.sequence Factory/Sequences/effects_tour.sequence.json` plays the tour, about 3 minutes 26 seconds, and hands over to the player in the fire room at the end. The gallery is also a level to walk through.
- The test room stays as it was: its tests and measurements use it.
- A room's effects start from nothing at each cut, so an effect that takes long to build (snow on the floor) shows less than it would in play. The black hold covers most of it.
- The tour is fixed to the rooms as the script writes them; the sequence panel can edit it, but the script overwrites it.

## Alternatives considered

- **Reworking the test room**: its rows are what its measurements compare; a gallery of rooms is another level.
- **Built-in capture at a fixed frame rate** (frames to PNG, sound mixed offline): no hitch would show whatever a frame costs, but sound would need an offline mixer. The owner records with OBS. The owner later asked for it after all: ADR-0123.
- **Prewarming emitters out of view** so that effects are full at the cut: it would mean simulating every room's particles at once, against the pool and the budget, for what a second and a half of black does.
- **Fades drawn by the renderer**: the game already draws what covers the scene (the loading screen, the menus, the damage flash), and a fade belongs with them.
