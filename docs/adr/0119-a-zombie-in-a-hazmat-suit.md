# ADR-0119: A zombie in a hazmat suit, with free animations

- **Status:** Accepted
- **Date:** 2026-11-21

## Context

The factory's zombie was the legacy demo's model: 19 joints, a low-resolution texture, four clips taken from the frame ranges the 2008 game played. Beside the Poly Haven props (ADR-0082) and the new weapons and items (ADR-0103), it looked its age. No realistic, rigged and animated zombie is CC0. The owner accepted CC-BY with credit, and the repository is public, which rules out files that may not be passed on.

From a shortlist of CC-BY zombies on Sketchfab, the owner chose [Zombie Hazmat](https://sketchfab.com/3d-models/zombie-hazmat-49b3b4307f6a4d2386fdb02354158d04) by LxNazarov: 15,500 faces, 2k PBR textures, a Mixamo rig. Its four clips come from Mixamo, whose files Adobe's terms do not let anyone pass on by themselves. A suit for toxic work fits a factory.

## Decision

- **The mesh, rig and textures are kept; the clips are replaced** by Quaternius' Universal Animation Library 1 and 2 (CC0):
  - `walk`: `Zombie_Walk_Fwd_Loop`;
  - `wait`: `Zombie_Idle_Loop`;
  - `attack`: `Zombie_Scratch`;
  - `die`: `Death01`;
  - `hit`: `Hit_Chest`, not played yet.
- **Retargeted once, in Blender** (`scripts/retarget_zombie_in_blender.py`), not in the engine:
  - Both rigs rest in a T-pose facing the same way. The 52 Mixamo bones map by name onto Quaternius' (an Unreal-style skeleton).
  - Each bone takes the source bone's rotation away from its rest pose, in world space.
  - The limbs are first turned to point where the source's point at rest, because the hazmat's legs are splayed and its toes point out.
  - The hips also take the source's movement, scaled by the ratio of leg lengths (1.1).
  - Every frame is baked at 30 per second.
- `scripts/make_hazmat_zombie.py` runs it from the three downloaded archives, then:
  - stands the model on the ground and turns it to face -Z, as the game's models do;
  - drops the head's vertex colors;
  - encodes the textures to KTX2 at 1024 texels;
  - writes `assets/Factory/Monsters/zombie_hazmat/` (6.3 MB).

  The archives themselves are not committed.
- **The level's four zombies use it.** The game's walk rate now comes from the new clip's stride: 1.4 m in 1.33 s, times 1.1 (`kZombieWalkClipSpeed`).
- **Credit** is in `assets/Factory/Monsters/LICENSE.md`, the glTF's `copyright` field and the README. The legacy `zombie.gltf` stays for the legacy importer.

## Consequences

- The zombie is taller (1.75 m) and heavier on the GPU: 15,500 faces and 53 joints against the legacy one's 19 joints.
- The attack clip is 1.83 s long, played in the game's 1.5 s swing. The death falls backwards.
- Retargeting is not perfect:
  - the suit's bulk may let the arms pass into the body in some poses;
  - fingers follow the source's, which barely move.
- Another Mixamo-rigged model can be made the same way by changing the paths and the clip table.

## Alternatives considered

- **Keeping Mixamo's clips:** they may not be committed to a public repository.
- **Retargeting in the engine:** a runtime cost and code for something done once per model.
- **Re-rigging the mesh with automatic weights:** worse deformation at the shoulders and hips, and the rig comes with the CC-BY model.
