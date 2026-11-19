# ADR-0103: Realistic items and weapons, resting where they would be

- **Status:** Accepted
- **Date:** 2026-11-19

## Context

The factory's pickups were the legacy demo's models: a floating, turning health kit, armor, ammunition, a flashlight and a red card. The handgun was the legacy model, and the shotgun a shape built by `scripts/make_shotgun.py`. Next to the Poly Haven props of the dressed factory (ADR-0082), they looked like a game from 2008. On 2026-11-19 the owner asked for new items and weapons that are realistic and high quality, taken from the same places as the props rather than generated. The owner also wanted them placed realistically, not floating and turning.

What Poly Haven (CC0) has: a service pistol, a bolt-action rifle, a metal ammunition can, a medical box, several flashlights and an old gas mask. What it lacks: a shotgun, shotgun shells, a pistol magazine or ammunition box, body armor and a keycard. A search of Sketchfab's CC0 models found no realistic shotgun either: a 19th-century museum scan of a muzzle-loader (326,000 triangles) and low-poly toys, and no shells.

## Decision

The owner chose each of the following.

- **Weapons:**
  - The pistol is Poly Haven's service pistol, with its wooden grips.
  - The **bolt-action rifle replaces the shotgun:**
    - five rounds;
    - 50 damage, so one hit kills a zombie;
    - a shot every 1.3 s, a reload in 3 s;
    - up to 30 rounds carried.
  - Both are at their real size, with 2k textures, since they are seen a hand's width from the eye. The legacy shotgun's sounds stay until better ones are found.
  - The models have no clips. A shot kicks the weapon back and up, and it settles; a reload takes it down and turns it.
- **Items:**
  - health is the medical box;
  - armor is the old gas mask (protection, in a factory full of gas);
  - all ammunition is the metal can;
  - the flashlight is Poly Haven's vintage one;
  - the red card keeps its model, at a keycard's size.
- **Kinds renamed:**
  - `shotgun` becomes `rifle`, and `shotgun_ammo` becomes `rifle_ammo`, in level files and text;
  - the control `shotgun` becomes `rifle`, still on 2 and the pad's right.
- **Placement:**
  - The items do not turn or float. `scripts/place_items.py` puts each one on a desk, a shelf, a crate or the floor, near where it was, beside what the room's dressing has.
  - It finds the surface with a ray down through the level's and the props' triangles, and lowers the item until its lowest vertex touches it.
  - The rifle leans against a wall. The gas mask lies on its back, tilted so that the mask and its filter both touch the floor.
- **Finding them in the dark:**
  - Items do not glow. Where the flashlight's beam reaches an item with nothing in between, a small star of light glints on it now and then, the nearer the more often (`glint.effect.json`, up to 18 m).
  - The level file gives each item's glint height (`glint`).
- **Sources:** `scripts/fetch_showcase_props.py` fetches the models with the kit and records their authors in `assets/Factory/Showcase/LICENSE.md`. It keeps one weapon out of each catalogue file: Poly Haven lays variants and loose parts side by side.

## Consequences

- The repository grows by about 45 MB, 42 of them the two weapons' 2k textures.
- `shotgun.gltf` and `scripts/make_shotgun.py` are gone. The legacy item models and textures stay, since they are the owner's own and `Factory.gltf` still lists the textures, but nothing uses them.
- A saved game or a player's bindings from before keep the old names: a saved shotgun is lost, and a rebound shotgun key is back on 2.
- The medical box is plain green with a small emblem. Its message ("First aid kit") says what it is.

## Alternatives considered

- **Generating the missing models in Blender:** the owner asked for models taken from the same places, not generated.
- **CC BY models from Sketchfab** (realistic shotguns and shell boxes exist): this would break the CC0-only rule for the showcase (ADR-0082). The owner chose the rifle.
- **A pickup prompt and a use key:** this would slow the Doom pace of the demo. The glint keeps walking over items.
