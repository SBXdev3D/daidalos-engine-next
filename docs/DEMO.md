# The demo

The sandbox is becoming a remake of the demo of the legacy Daidalos engine: a first-person shooter in the spirit of Doom and Quake, set in a dark underground factory, with an oppressive horror atmosphere: darkness the flashlight barely holds back, failing lights, sounds behind closed doors (ADR-0041). It serves as the engine's test bench: each phase of the [roadmap](../OVERVIEW.md) must make it better, so nothing is built without a real use.

## The original

The legacy demo lives in `../daidalos-engine`. Its level, `Resources/Scenes/factory.des`, is laid out on a grid of 10-unit tiles. This route is read from the level file and the code, not from playing it: the legacy executable has not been run.

```mermaid
flowchart LR
    A["Start room<br/>teleporter"] --> B["Central hall<br/>railings, drop, alarms"]
    B --> C["East wing<br/>flashlight, health, ammo"]
    C --> D["North rooms<br/>zombies wake up"]
    D --> E["Red access card"]
    E --> F["Red door<br/>in the hall"]
    F --> G["Ladders and fans<br/>underground"]
    G --> H["Last zombies"]
```

| Theme | What it had | Numbers worth keeping |
|---|---|---|
| Player | Walk, run, crouch, jump, ladders, footsteps, fall sounds | 3.5 m/s, run ×1.7, crouch ×0.6, 80 health at the start |
| Weapons | Handgun and shotgun, reloads, muzzle flash, smoke, weapon sway | Handgun: 12 rounds, 10 damage, 350 ms. Shotgun: 7 rounds, 5 pellets of 12 damage, 1200 ms |
| Zombies | Patrol, wait, follow, attack and die states, groans, blood | 50 health, 12 damage every 800 ms, view 60° over 12 m, search 9 s |
| Pickups | Health, armor, ammunition, flashlight, red card, spinning | Armor +50, handgun +12, shotgun +7 |
| World | Doors, card reader, fans, alarms, security camera, spot lights, green fog | Doors open 3.5 m away, stay 6 s |
| Screen | HUD, crosshair, loading screen, opening camera flight | |

## How it grows

| Phase | The demo gains | Status |
|---|---|---|
| 5: Scene | The level in the new engine, background loading, restart, doors, pickups, red card, darkness | Done |
| 6: Physics, audio and animation | Walking with collisions, jumps, crouching, ladders and solid doors, sound, skinned zombies and turning fans, hitscan handgun and shotgun, zombies that hunt, wounds and death | Done |
| 7: Modern rendering | Real lights replacing the fog, the flashlight's shadow, rotating alarms, normal maps, ambient occlusion, particles for smoke, blood and green fog | 7.1 to 7.4 done: lights, alarms, normal maps, shadows, ambient occlusion, particles |
| 8: GUI | The game's own HUD, crosshair, menus and loading screen | Done: HUD and crosshair in every build; loading screen with the turning gear, main, pause and options menus, death and "factory cleared" screens; developer tools in development builds |
| 9: Ray-traced rendering | Soft ray-traced shadows, the sky through the openings, moonlight and bounced light, reflections in metal and polished floors, glass over the shaft where a zombie patrols, lamps in real units; then many lights, exposure and volumetric fog: lamps' cones in the haze, dust in the start room, steam at the leak, fog lying on the floor of the big room and the corridor; quality presets; then fire in the furnace area, drips, sparks from a cable, dust motes, flies, casings and impacts by surface | Done: through 9.25, fire in the south room's pit, drips, a shorting cable, dust, moths and flies, rain through the big room's open roof, impacts by surface, the green gas as glowing fog, smoke from the barrel after a shot |
| 10: Level editor | The factory's lights and sky in a level file, new rooms built without code | 10.1 done: the factory is a level file; 10.2: the editor opens it; 10.3: undo, files and play from the editor; 10.4: assets; 10.5: editing many entities |
| 11: Materials, lights and atmosphere | Wet floors and puddles, decals of blood and grime, a better flashlight, flickering and dying lamps, film grain | 11.1 to 11.3 done: clear coat and sheen, profiles, cookies and animated lights, the flashlight with its sway and battery |
| 12: Sound | Doors that muffle sound, reverb that follows the rooms, music that follows the danger | |
| 13: Gameplay systems | Security camera, opening camera flight, better zombie navigation, scripted scares, saves | |
| 16: The showcase | The factory dressed with CC0 props by room and purpose, lamps on room circuits, scares along the route, a way out at the far end of the pumps, an end summary | 16.1 kit, 16.2 dressing, 16.3 scares and the way out, 16.4 hit markers, wound direction, recoil and flinch, 16.5 the opening flight, 16.6 the budget done: Phase 16 is done |

Zombies come earlier than the roadmap's AI phase, because a shooter with no one to shoot is not a demo. Their state machine belongs to the game, and Phase 13 only adds what they turn out to lack.

## Where it should improve on the original

- **Pace.** Faster movement, more enemies, ambushes when doors open: closer to Doom than to the original's slow zombies.
- **Dread.** Between the fights, the factory should feel hostile: dark corridors, lights that flicker and die, wet floors, scares set up by the level.
- **Feel.** Hit feedback, screen flash on damage, stronger recoil and sway.
- **Structure.** The factory level has shotgun ammunition, but no shotgun to pick up. The remake has a rifle next to its ammunition (a shotgun until ADR-0103), a way out at the far end of the pumps, and an end-of-level summary of time, kills and items (ADR-0084).
- **Sound.** Spatial audio is done. Occlusion by doors can come later, with rays from the listener.

## Running it

The level is committed in `assets/Factory/`, so the demo only needs a build. The executable finds it from its own location, so a double-click works:

```bash
build/windows-msvc/apps/sandbox/Release/sandbox.exe
```

If the level is missing, the sandbox shows a test floor. Other glTF files can be tried with `+sandbox.scene <path>`.

The level loads behind the legacy loading screen, then waits behind the main menu; `+sandbox.menu false` starts playing at once. `+sandbox.tour true` starts with the legacy demo's camera flight over the whole factory instead (ADR-0120), which ends in the player's eyes. Escape pauses. The options apply at once and are saved to `user.cfg` next to the executable when you leave them; delete it to go back to the defaults. The game ends two seconds after the player dies, or after the last zombie dies, with the time taken.

In development builds, the key under Escape opens the developer tools: the console (`help` lists its commands, `restart` starts over), statistics, the entities with the player's state, and the loaded resources.

`scripts/import_legacy_level.py` is not part of running the demo. It converts the legacy level again, or brings more legacy content over. It also bakes the animated MilkShape models to glTF files of their own: the zombie, the fans and the handgun, whose clips are the frame ranges the legacy game played. The committed level has since been edited by later steps, so the importer no longer reproduces it. Since Phase 10 the level is `Factory/Factory.level.json` (ADR-0054): its lights, fog, effects, sky, exposure, props and the game's objects, around the walls and doors of `Factory.gltf`; `scripts/gltf_to_level.py` made it once from the dressed glTF level.

## The fight

The numbers come from the original's code, except where the table says otherwise.

| | How it works |
|---|---|
| Handgun | 12 rounds, 10 damage, a shot every 0.35 s, reloads in 1.5 s (2 s in the original). Starts loaded, with nothing to reload it |
| Rifle | 5 rounds, 50 damage (a zombie dies of one hit), a shot every 1.3 s, reloads in 3 s, up to 30 rounds carried. It replaced the original's shotgun (ADR-0103): no realistic free shotgun model exists. It leans against a wall of the west wing, its ammunition can at its foot |
| Items | Poly Haven's models, as they would lie: a medical box (health), an old gas mask (armor), metal ammunition cans, a vintage flashlight, the red card at a keycard's size. They rest on desks, shelves, crates and floors, and the flashlight's beam makes them glint now and then (ADR-0103) |
| Aim | Rays from the eyes, spread as the original spread its bullets, up to 35 m. A shot is heard by zombies within 20 m |
| Zombies | 50 health. They see 12 m ahead, within 60° of where they face, if nothing is in between. They walk at 1.3 m/s, faster than the original's 0.7, straight at the player: there is no path finding. They strike for 12 every 1.5 s within 1.4 m, look for a lost player for 9 s, then go back to waiting, or to their patrol |
| Wounds | Armor takes half of each wound while it lasts. At 0 health the player falls, and F5 starts again |

| Control | Action |
|---|---|
| Mouse, right stick | Look |
| W, A, S, D, left stick | Move |
| Shift | Run |
| Space | Jump |
| C, left Ctrl | Crouch |
| W on a ladder | Climb up, or down when looking down |
| Left button, right trigger | Fire |
| R, right shoulder | Reload |
| 1, 2, Q, wheel, D-pad up | Change weapons |
| E | Use a door |
| F | Flashlight |
| F5 | Restart |
| Tab | Free the mouse |
| Escape | Pause |

The sandbox reads the engine's settings from `daidalos.cfg` and its own from `sandbox.cfg`, both next to the executable, which the build copies from `config/daidalos.cfg` and `apps/sandbox/sandbox.cfg` the first time and leaves alone afterwards; the command line overrides them with `+name value`. The [README](../README.md#building) lists them. `sandbox.start` places the player, with `x y z yaw pitch` in meters and degrees, and `sandbox.darkness 0` shows the level 16 times brighter, without its haze. `sandbox.flashlight true` starts with the flashlight on. Both are useful to reach a place quickly. The value of `sandbox.start` holds spaces, so it is quoted on the command line: `+sandbox.start "31.5 4.3 -54 0 -25"` looks down at a lit ceiling spot, whose room has its floor at 2.8 m, and `+sandbox.start "0 1.5 -36 0 10"` faces the turning alarms.

## Assets

The geometry and `handgun_ammo_d.png` are the author's own work. The other textures and the sounds are free assets whose licences allow commercial use. [assets/Factory/LICENSE.md](../assets/Factory/LICENSE.md) has the details.

Some legacy content is left out:

- `eva.md2` and its texture `eva_d.png`, a test model the level does not use.
- `test.wav` and `test2.wav`, which are leftover tests.
- `enter_the_maze.wav`, the level's ambient track, which is committed as a 3.4 MB OGG file instead.

The site the free assets came from is not recorded yet. The author confirmed on 8 November 2026 that the textures are free to use and that screenshots showing them can be published on Patreon.
