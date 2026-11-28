# ADR-0129: Shot cues, and rooms that wait for their turn

- **Status:** Accepted
- **Date:** 2026-11-26
- **Amends:** ADR-0127 (a cue kind), ADR-0128 (the swinging, joints and machines rooms; the rooms of the tour)

## Context

Two notes from the owner on the physics gallery (ADR-0128):

- In the machines room, the crates were already on the floor when the room came on screen. Every room is in one level, and the physics runs in all of them from the start of the tour. The belt carried its boxes from the first frame, so it had dropped them all off its end about 2.5 minutes before the camera reached the room. Tuning with `--only Machines` hid it: that preview starts the clock in the room.
- In the swinging room, the lamps started to swing with nothing to start them. They should be shot.

Then, on the shot lamps:

- No sparks where the bullet hit a lamp. The game finds what a shot hits is made of in `BodySurfaces`, which held only the level's walls and floors: a loose object was of nothing, so it threw nothing.
- The lamps seemed to start all together, 1.4 s apart and swinging on: they should go one after another, as a shooter would take them.
- One room of dominoes is enough: keep the best. The room of two crates gliding on the floor goes. A boulder falling onto stairs and tumbling down them comes in.

## Decision

- **A `shot` cue:** `{"shot": {"entity": "Lamp", "point": [0, -1.1, 0], "from": [x, y, z], "weapon": "handgun"}}`. A gun nobody sees fires from `from` (in the world) through `point` (in the loose object's frame, where the object is at that moment). `weapon` is `handgun` (the default) or `rifle`.
- **The engine reads the cue and the game fires it.** The weapons, their push, their impact effects, holes and sounds are the sandbox's (`Combat.cpp`), so the scene only works out the point aimed at and queues the shot (`Scene::TakeShots`), as it queues event cues. The sandbox fires the queue on every frame through the same code that lands the player's shots (`LandShot`): one ray with no spread, a push of 2 kg·m/s per point of damage (20 for the handgun, 100 for the rifle; ADR-0127), sparks and a hole on metal, splinters on wood, and the impact sound. The gunshot is heard from `from`. The cue shows no muzzle flash, because the gun can't be seen, and it hurts no zombie.
- **The swinging room** shoots its three lamps with the handgun, in the same directions as the old pushes, from as far back as the room allows, 2.6 s apart (at 2.6, 5.2 and 7.8 s; the hook goes at 10.6 s, the room lasts 21 s). 20 kg·m/s is what the pushes were (17 to 21), so the swings stay the same.
- **Loose objects are of their surface for shots too:** the sandbox adds each `RigidBody`'s surface to `BodySurfaces` when it builds the level, so a lamp throws sparks, a crate splinters, and the impact sounds are theirs. A shot leaves no hole on a loose object, which would stay in the air once it moves.
- **The joints room** shoots the door with the rifle, from behind and then back from in front, instead of kicking it twice at 75 kg·m/s.
- **The machines room waits for its turn.** Its belt boxes are held (ADR-0127) and let go at the room's 0 s, and its paddle's motor is still in the level and starts at 0 s. The room then plays as its `--only` preview did. A room whose objects are moved by something that runs all the time (a conveyor, a motor) has to hold them until it starts.
- **The rooms, nine:** the Finale keeps its dominoes, which start the chain reaction that ends the tour; the Dominoes room, dominoes and nothing after them, goes. The Materials room goes with its lanes. **Stairs** takes its place (index 6, so the other rooms stay where they were and play as recorded): a flight of eleven concrete steps, 30 cm by 55 cm, from a 3 m landing, between rusted parapets; a 2-tonne boulder (`Props/boulder`, a lumpy sphere 1.3 m across with Poly Haven's `rock_face`, a convex hull, of `concrete`) is let go over the second step at 2.6 s, tumbles down in about 2 s and scatters a pyramid of crates at the foot. The camera stands beyond the foot, looking up the flight, and turns to follow the boulder.
- **The machines room's paddle turns the other way** (`PADDLE_SPEED` -150 °/s): its left side comes down on the boxes at the belt's end and bats them back up over the belt, which brings some back to be hit again, instead of lifting them off across the room. Asked by the owner, recorded and kept.
- **Stairs, made bigger** at the owner's request: the room is 30 m long and 14 m high; fourteen steps of 40 cm by 70 cm from a 6 m landing, 5 m wide; the boulder is 2 m across and 8 tonnes, let go 5 m over the second step; ten crates in a pyramid at the foot. The camera starts past the foot and backs away to the far wall as the boulder comes down toward it.
- **Pool, a new room** (index 5, the Dominoes room's place): a basin 5.6 m by 3.6 m, and a square duct 1 m wide from the ceiling, 3.2 m over it. Above the ceiling, out of sight, a hopper with a sloping floor holds 980 plastic balls, 16 cm across, in five colors (`Props/ball_*`, plain glossy plastic), held in rows five across and seven deep; the rows are let go one every 0.15 s from 2 s, nearest the duct first, and pour down it in a stream. Two crates, a drum and a rubber ball stand in the pool for them to hit.
- **The physics world holds more contacts:** 16384 body pairs and contacts (were 4096 and 2048), 32 MB of step memory (was 16). The heap of balls overflowed them, and Jolt drops what does not fit: the crates and the drum under the balls fell through the floor. A step that drops contacts now says so, once, as a warning.
- **The editor** has the new cue: its fields, a line from the gun to the target, and the map check for the entity it names.

## Consequences

- A sequence can now fire a weapon only the game knows. Another game would read `TakeShots` its own way, or ignore it.
- The other rooms' objects all wait for their cues already: held, standing or asleep.
