# ADR-0115: Geometry for blocking out rooms

- **Status:** Accepted
- **Date:** 2026-11-20

## Context

Step 20.11 of Phase 20 (ADR-0104): proposal item 10. Every surface of a level came from a model made in Blender. Trying a room, a stair or a ledge meant a round trip there before the editor or the game could see it. Godot's CSG, Unity's ProBuilder and Unreal's modelling tools let a designer block a room out first.

## Decision

- **A `block` component** (`scene::LevelBlock`): a box, stairs, a ramp or a cylinder, with its size in meters (across, high, deep), its steps or sides, and a plain color. It stands on the entity's origin; stairs and ramps rise towards -Z. It is written as `"block": {"shape": "stairs", "size": [...], ...}`.
- **Its geometry is a model made in code.** `MakeBlockModel` builds the mesh, with normals, tangents and texture coordinates in meters, and a rough material of the block's color. `AssetLibrary::AddModel` builds it as a loaded model is built, once for each shape, size and color. The entity then gets a `ModelInstance` like any model, so drawing, ray tracing, shadows, picking, snapping and the depth of overlays need nothing new.
- **The game collides with it** without new code: the model's node is named `level`, the tag the game already makes walls and floors of (ADR-0083). It also reaches sound's acoustic meshes and navigation through the same path.
- **In the editor:** Create > Block out offers box, wall, floor, stairs, ramp and pillar, also from the view's right-click "Create here" (ADR-0112). The inspector edits a block's shape, size, steps, sides and color; any entity can get one with "Add a component". The hierarchy marks blocks `B`.

## Consequences

- Blocks are plain color, without a texture: an embedded image in a model built in code would need encoding. One repeat a meter in their texture coordinates is ready for a grid texture later.
- A block is not a model file: Blender cannot open it, and turning a blockout into art is still a modelling job, with the block as a guide of the right size.
- Stairs are boxes from the floor up, so their insides hold faces no one sees: a few dozen triangles.

## Alternatives considered

- **CSG (unions and subtractions) of brushes,** as Unreal's BSP and Godot's CSG: doors cut into walls, but a mesh boolean library and its robustness to maintain. Separate blocks around a doorway cover blocking out.
- **Blocks written as glTF files** when created: Blender could open them, but every size change would write a file, and the folder would fill with throwaway models.
