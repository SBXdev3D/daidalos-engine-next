# ADR-0120: The factory tour

- **Status:** Accepted
- **Date:** 2026-11-21
- **Amends:** ADR-0071 (a camera cue may take one smooth path through its keys), ADR-0086 (doors open for a cinematic camera; skipping lifts the flight's mix)

## Context

The legacy demo had a cinematic camera that flew over most of the factory: `Resources/Cinematics/intro.dgc` of the legacy engine, attached to `factory.des` by a cinematic trigger. It has 41 steps, each a position, a rotation (pitch down and yaw from +Z towards +X, in degrees) and the milliseconds to the next step, 3 minutes 24 seconds in all: the loading floor, the lift room, the lockers and the red card room, the west corridor, a drop down the shaft into the gas room, up the far shaft, the upper corridor, the yard. The legacy engine drew a cubic curve through the positions and eased the rotation within each step.

The remake already opens with a 19-second flight (ADR-0086). The owner wanted the old one back, adapted and improved, and a setting to start the sandbox with it. They chose to keep it apart from the opening, to keep the old route and pace while fixing what no longer fits, and to end it at the player's eyes.

Played as they are, the keys do not fit the engine or the level any more:

- A camera cue eases into each key and goes straight between them (ADR-0071): through 41 keys, the camera would stop 40 times. It also blends the points the keys look at, which turns the wrong way or flips over the old path's half turns, and a key that looks straight down leaves `lookAt` no way to tell up.
- The level has gained props, a crane and lockers since, and the curve through the old keys cuts corners: checked against the level, it passed through walls, a crawl duct, the lockers and every closed door on its way.

## Decision

- **A camera cue may take a smooth path** (`"path": "smooth"`, `scene::CameraPath::Smooth`; `"eased"`, the default, is as before and is not written). The camera goes through every key at its time on a cubic curve whose speed changes smoothly (a Catmull-Rom spline over the keys' uneven times), at rest only at the first and last key. Where it looks is a yaw and a pitch on the same curve, each yaw unwrapped so that every turn takes the shorter way, the pitch kept a degree short of straight up or down; the look's tangent is level at a key where it holds or turns back (Fritsch-Carlson), so that between two keys looking straight up the shaft it stays there rather than swinging past and spinning the picture; aperture and focus follow the same curve. `scene::CameraAt` takes the path; the scene and the editor's drawn path use it, and the sequence panel has a "Smooth path" box. A camera looking straight up or down (within a quarter of a degree) now takes -Z as the top of the picture instead of an undefined one; switching earlier, at 87 degrees, turned the picture over in one frame while the tour climbed a shaft. A test samples the tour at 60 frames a second and fails if the look or the top of the picture turns more than 3 degrees in a frame.
- **The tour is data, made by a script.** `scripts/make_factory_tour.py` reads the legacy `intro.dgc`, converts each step (meters, Z mirrored, as the level was converted; the rotation as a point four meters ahead) at the legacy times, and writes `assets/Factory/Sequences/tour.sequence.json`. Hand fixes in the script, each with its reason: keys moved where the curve now meets the level (inside the loading floor's east wall, over the lockers, into the duct at its height), keys added in door frames and the duct, a key half way through each legacy half turn so that it turns the way the legacy camera did (into the first shaft, half turned and half looking down, so that the half turn happens on the way down, as in the legacy flight, and not while looking straight down). After the last legacy step it flies back over the yard, down the north corridor, across the hall and through the start room's door, and turns into the player's eyes, where the opening ends. 62 keys, 3 minutes 43 seconds.
- **`--check`** samples the path as the engine does and lists every stretch closer than 0.3 m to the level, a door or a prop, with what it is and where: the fixes came from it. It now finds only the doors and a zombie that walks away.
- **Doors open for a cinematic camera** as they do for the player, from 6 m (`kCameraDoorRadius`), since it moves faster; `Scene::CinematicPosition` says where a camera cue has the camera. Only the doors with a green lamp open for it: those that open by themselves, and the card reader's door. The doors opened by hand under a red lamp stay shut, as the owner asked. A door opened for the camera closes a second after the camera has gone, even one that stays open once the player opens it, and its lock is kept. The opening (ADR-0086) passed through the start room's closed door too, and now sees it open.
- **`sandbox.tour true`** loads the level straight into the tour instead of the main menu; the short opening is then not played. Escape, jump or use skip it, as the opening (ADR-0086). Skipping a flight now also lifts its muffled mix at once, rather than when the flight would have ended.
- **The tour flies to the old demo's track,** `enter_the_maze.ogg` (178 s, looped), as the legacy flight did: at no danger the layered score (ADR-0068) plays only its quiet first layer, and the tour sounded silent. When the camera hands over, or is skipped, the track fades out over three seconds and the score starts. With `sandbox.classic_music` the track simply plays on. The world's sounds are not muffled during the tour.
- **The tour flies lighter:** most of the factory is black without the flashlight, so the tour plays at `sandbox.tour_darkness` (0.5), and the darkness returns to `sandbox.darkness` over two seconds as the player takes over.

## Consequences

- The tour is fixed to the level as it is now. If a room changes, `--check` says which part of the flight to move, and the script writes the sequence again. The sequence panel can edit the result, but the script would then overwrite it: changes belong in the script.
- A smooth path needs enough keys to hold it: between two keys far apart it bends more than straight lines would. The check is there for that.
- Some stretches stay dark at half darkness (the crawl duct, the upper corridor): they are dark rooms, and the tour shows them as such.
- Zombies do not react to the camera. The tour circles the hazmat zombie in the red card room, as the legacy flight circled its zombie.

## Alternatives considered

- **Changing the eased path for every camera cue:** the opening's six keys are shots, and a curve through them would round its corners. A choice per cue keeps both.
- **Copying the keys unchanged:** the camera would pass through the new props and the duct's walls.
- **Opening doors with cues in the tour's sequence:** the doors are game objects that close by themselves; a door that opens for any cinematic camera also serves the opening and later flights.
- **A light carried by the camera** instead of lighter darkness: it would light the walls nearest the camera and leave the rooms beyond dark, which is the flashlight's look, not a view of the level.
