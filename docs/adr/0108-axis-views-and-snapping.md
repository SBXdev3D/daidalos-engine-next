# ADR-0108: Top, front and side views; snapping to surfaces

- **Status:** Accepted
- **Date:** 2026-11-20

## Context

Step 20.4 of Phase 20 (ADR-0104): proposal items 5 and 6. The editor had one perspective view. Placing props against walls, the most frequent action, meant dragging gizmo arrows and checking from several angles. There was no view along an axis to line things up, and no way to put a prop on a surface.

The renderer draws one perspective view a frame. Its shaders assume a perspective camera: linear depth from the near and far planes, view vectors from the camera's position, fog slices growing with distance. A true orthographic projection touches all of them.

## Decision

- **Axis views drawn nearly orthographic.**
  - The top, front and side views put the renderer's camera 400 m back along the axis, with a lens just wide enough for the view's height. A 10 m room is within 3% of one scale front to back, and every shader keeps its perspective.
  - The near plane is the view's **cut**: it stands `cut` meters in front of the view's focus, 0.5 m by default. The top view sees into the rooms of the camera's floor under their ceilings, and the front and side views into rooms behind their walls. Lighting is unchanged, since rays still meet the ceiling that was cut away.
  - `[` and `]` move the cut by a quarter meter (Shift: a meter).
  - In an axis view, the right or middle button pans and the wheel zooms about the mouse. F frames the selection, with the cut just in front of it.
  - The grid lies in the plane through the focus, with a step of 1, 10 or 100 m so lines stay 10 pixels apart.
  - Shift+1 to Shift+4 (or View, or the keypad's 5, 7, 1 and 3) choose perspective, top, front or side for the live pane.
- **Four views (Shift+5)**, two by two: perspective, top, front, side.
  - The renderer still draws one view a frame: the live pane, with an amber border.
  - A press in another pane makes it live. The frame of that press is captured (`Renderer::RequestCapture`), and the pane just left keeps that image, with its depth picture for its overlays.
  - Markers, boxes, the grid and the selection are drawn live over every pane, through each pane's own camera. The gizmo, picking, painting and measuring work in the live pane.
  - This is how Unreal Engine's viewports behave when real time is off: what is not live keeps its last image.
- **Snapping to surfaces.**
  - With View > Snap to surfaces while moving, or Ctrl+Shift held during a move, the selection goes where the mouse's ray meets a surface, its own parts passed through.
  - With View > Turn to the surface, it also turns its up to the surface's normal, keeping the rest of its turn.
  - Holding V snaps to the nearest corner of the triangle under the mouse (`PickHit::corners`).
  - The gizmo's pivot (ADR-0107) is what lands there, so Pivot: bottom puts a prop's base on the floor or against a wall.
- **Drop to the floor (End, or Edit):** each selected root moves straight down until the bottom of its box meets what is under it, within 100 m. This is one step of the history.

## Consequences

- In axis views the camera is far, so the denoiser's depth tolerances and ray offsets, which grow with distance, are looser: edges blur a little more and thin walls can leak light at grazing angles. Unlit (Alt+2) and Editor light (Alt+4) read cleanly.
- A pane's last image is a 1–2 frame capture readback of the window, copied into a texture, only when switching panes.
- The first click in another pane only makes it live; the next click selects.

## Alternatives considered

- **An orthographic projection in the renderer:** exact, but every pass that reconstructs view vectors or linear depth would need a second path, and the game would never use it.
- **Four views drawn every frame:** four times the frame, with temporal history per view that the renderer keeps once. Unreal itself draws non-real-time viewports only on change.
- **Inactive panes as wireframe drawn on the CPU:** the factory's half million triangles cost too much a frame in ImGui lines.
