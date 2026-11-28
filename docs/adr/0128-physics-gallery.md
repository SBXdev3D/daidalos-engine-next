# ADR-0128: The physics gallery

- **Status:** Accepted
- **Date:** 2026-11-24
- **Amended by:** ADR-0129 (the lamps and the door are shot; the machines room waits for its turn; the Dominoes and Materials rooms make way for a boulder down stairs and a pool of balls)
- **Amends:** ADR-0122 (the galleries share `scripts/gallery.py`), ADR-0125 (body groups; heavy hits and dust go by reduced mass), ADR-0126 (a turning motor keeps its bodies awake)

## Context

Step 21.4 of Phase 21 (`docs/proposals/physics.md`). The owner wants a video of the physics, one theme per room, to record as they recorded the effects gallery (ADR-0122, ADR-0123): no titles, no music, the rooms' sounds and the bodies' own, black between rooms.

## Decision

- **`scripts/make_physics_gallery.py`** writes `Factory/PhysicsGallery` (`.gltf`, `.bin`, `.import.json`, `.level.json`) and `Sequences/physics_tour.sequence.json`:

      +sandbox.scene Factory/PhysicsGallery.level.json +sandbox.sequence Factory/Sequences/physics_tour.sequence.json

  The script is the source, as for the effects gallery; changes belong there, not in the editor.
- **The rooms**, 333 loose objects and 102 joints, 3 minutes in all:

  | Room | What happens |
  |---|---|
  | Swinging | Three lamps on chains, kicked one after another, sweep their light and the shadows of what stands under them; a hook on a heavier chain, let go from the side, swings across in front of them. |
  | Falling | Crates, sacks, a jerrycan and drums, held at the ceiling, drop one after another on three columns of crates, which topple. |
  | Rolling | Drums and gas bottles, held on a ramp, roll down through stools, a sign and boxes into a low wall of crates. |
  | Explosions | Two blasts among gas bottles and drums, then one behind a wall of crates that blows it toward the camera. |
  | Wrecking | A 400 kg ball on a chain of 26 heavy links, held out at 60 degrees, swings through a wall of crates. |
  | Dominoes | 66 planks around the room, the last ones growing, tip two shelves. |
  | Materials | The same drop for a plastic box, a steel tool chest, a rubber ball and a sack; the same push for a crate on a dry lane and one on an oily lane. |
  | Joints | A door on its hinge, kicked both ways; a sign on two chains, one of which snaps; a shelf glued together, blown apart; a see-saw that throws a crate. |
  | Machines | A conveyor carries boxes to a paddle turned by a motor, which bats some of them; a piston shoves crates off a ledge. |
  | Finale | A ball rolls down a ramp into small dominoes that grow; the last, a 100 kg beam, slams a see-saw, which throws a crate onto gas bottles, which blow. |

- **The props it lacked**, written by `scripts/make_physics_props.py` into `Props/`: a plank, boards for the see-saw, the paddle and the glued shelf, a door, a sign, a piston, a belt and lanes, a chain link, a hook, a wrecking ball and a rubber ball. They are their real size, so a joint's anchor is a point on the model. The ramps are part of the rooms' glTF.
- **Their textures are seamless ones, repeated at their real size.** The factory's crate and lift textures are pictures of a crate's face and a lift's wall, made for cubes: on a plank or a ball they showed cut-up crates. The script fetches eight CC0 textures from Poly Haven (rough wood, plywood, painted and rusted steel, painted machine plate, tread plate, black and red rubber), converts them to KTX2 at 512 texels into `Props/textures` (5.9 MB; the JPG sources stay in `Props/Source`, ignored by git), credits them in `Props/TEXTURES.md`, and maps each prop's texture coordinates in meters over the texture's size from Poly Haven. The door keeps the factory's door, made for a door; the oily lane is a plain dark gloss. The rooms' steel fittings and ramps take the rusted steel too (`gallery.TEXTURED`), whose 2.2 m matches the rooms' 2 m repeat.
- **`scripts/gallery.py`** holds what both galleries share: rooms of boxes and ramps, entities, shots, the glTF, level and tour writers, and `--check`. The effects gallery writes the same files through it as before.
- **Chains** are a helper of the script, not an engine type: links held by point joints, and a hidden rope from the top to the load (ADR-0126).
- **Body groups.** A body's `group` (a name in the level, a number in physics) keeps bodies of the same group from touching (`DynamicBodyDesc::group`, rejected in the contact listener). A chain's links and its load share one. Without it, a swinging lamp knocked on the links above it every step: a stream of clicks, and dust.
- **Heavy hits and dust go by the reduced mass** of the two bodies, `m1·m2/(m1+m2)`, or the body's own against the world: a light link on a heavy lamp is a light knock. The sound's pitch still goes by the heavier body.
- **A motor that turns keeps its bodies awake.** Stopped for a moment against a load, a body fell asleep and its motor never turned it again: the paddle wheel stopped for good. A joint whose motor goes off, breaks or goes lets them sleep again.
- **Timed to the physics.** The cues only start things: release, impulse, blast, break, motor. The finale's blast is timed to when the crate reaches the bottles. That holds because the same run gives the same result: the world steps on one thread, a recording's frames are each 1/`record.fps` s, and the blast's spin comes from a fixed seed (ADR-0127).
- **Tuned by eye.** `--only ROOM` writes `physics_preview.sequence.json` (not kept), and `--overview` films those rooms from a high corner, to see where things go.

## Consequences

- Repeatability, checked: each room recorded twice at 30 fps, compared every 2 s. The bodies end where they ended: PSNR 35 to 51 dB, render noise (a third of a second of motion gives 20 to 25 dB). The lowest, 26 to 31 dB, are frames during a blast, and their difference is only sparks: particles are not drawn from a fixed seed, so a blast's sparks differ from run to run while what it throws does not.
- A cue timed to a landing must be timed again when anything before it in the room changes.
- At 5 fps, fog and denoiser histories trail moving lights by seconds, so a slow recording is no way to judge a swinging lamp's light; at 30 fps and up it follows.
- The paddle bats only some of the boxes; the rest fall under it.
- Tested: bodies of a group pass through each other, a stopped motor stays awake and sleeps once off, a light body on a heavy one raises no dust, group names give the same number every time, and a body's group is kept in the level file.
