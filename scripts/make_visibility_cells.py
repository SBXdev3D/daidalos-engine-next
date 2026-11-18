#!/usr/bin/env python3
"""Makes the factory's visibility cells (ADR-0089).

    python -I scripts/make_visibility_cells.py

Each room below is a seed point on a floor and a rectangle it stays within. The script fills the free floor
from the seed (floor at the level, no wall) on dress_showcase.py's grid of Factory.gltf, within the
rectangle, and makes the box around what it reached a cell: from just under the floor to just under the
room's usual ceiling (twelve meters up under the sky). Boxes grow by a quarter of a meter on their sides, so
that two rooms joined by an opening overlap there, while two rooms with a wall between them do not. The
cells go under an entity named "Cells: factory" in Factory.level.json, which the next run replaces; the
editor rewrites the level in the engine's layout afterwards.

What no cell holds (the upper floor, the underground but the pit) is always drawn: a room left out loses
culling, never visibility.
"""

import json
import math
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import dress_showcase as dressing  # noqa: E402  The level's geometry on a grid.

ROOT = Path(__file__).resolve().parent.parent
LEVEL = ROOT / "assets" / "Factory" / "Factory.level.json"
GROUP = "Cells: factory"
GROW = 0.25
SKY = 12.0

# Room: (floor level, seed x, z, rectangle x0, x1, z0, z1).
ROOMS = {
    "start room": (0.0, 0.0, -4.0, (-6, 6, -8.5, 1.5)),
    "south corridor": (0.0, 0.0, -12.0, (-2, 2, -16.5, -7.5)),
    "airlock": (0.0, 0.0, -17.5, (-6, 6, -20.5, -15.5)),
    "loading floor": (0.0, -10.0, -30.0, (-15, 26, -58, -19)),
    "east passage": (0.0, 27.5, -38.5, (24, 31, -41, -36)),
    "workshop": (0.0, 40.0, -40.0, (29, 51, -49, -29)),
    "west corridor": (0.0, -30.0, -38.5, (-45, -13, -41, -36)),
    "north corridor": (0.0, 0.5, -70.0, (-2, 3, -79, -56.5)),
    "yard": (0.0, 4.0, -80.0, (-9, 18, -100, -78)),
    "pit": (-2.8, -5.0, -90.0, (-9, 12, -100, -78)),
}


def fill(grid, level, seed, rect):
    x0, x1, z0, z1 = rect
    cell = dressing.cell_of(*seed)
    if not grid.floor(cell, level) or grid.wall(cell, level):
        raise SystemExit(f"seed {seed} is not on a free floor at {level} m")
    seen, todo = {cell}, [cell]
    while todo:
        ix, iz = todo.pop()
        for dx, dz in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nxt = (ix + dx, iz + dz)
            x, z = (nxt[0] + 0.5) * dressing.CELL, (nxt[1] + 0.5) * dressing.CELL
            if nxt in seen or not (x0 <= x <= x1 and z0 <= z <= z1):
                continue
            if grid.floor(nxt, level) and not grid.wall(nxt, level):
                seen.add(nxt)
                todo.append(nxt)
    return seen


def main():
    triangles, _ = dressing.read_gltf(dressing.FACTORY / "Factory.gltf", True)
    grid = dressing.Grid(triangles)
    level = json.loads(LEVEL.read_text(encoding="utf-8"))
    gone = {e["id"] for e in level["entities"] if e.get("name") == GROUP}
    level["entities"] = [e for e in level["entities"] if e["id"] not in gone and e.get("parent", 0) not in gone]
    next_id = max(e["id"] for e in level["entities"]) + 1
    group = next_id
    level["entities"].append({"id": group, "name": GROUP})
    next_id += 1
    for name, (floor, sx, sz, rect) in ROOMS.items():
        cells = fill(grid, floor, (sx, sz), rect)
        xs = [c[0] * dressing.CELL for c in cells]
        zs = [c[1] * dressing.CELL for c in cells]
        ceilings = [grid.ceiling(c, floor) for c in cells]
        roofed = [c for c in ceilings if c is not None]
        top = statistics.median(roofed) - 0.05 if len(roofed) > len(cells) / 2 else floor + SKY
        low = [min(xs) - GROW, floor - 0.3, min(zs) - GROW]
        high = [max(xs) + dressing.CELL + GROW, top, max(zs) + dressing.CELL + GROW]
        center = [round((a + b) / 2, 3) for a, b in zip(low, high)]
        size = [round(b - a, 3) for a, b in zip(low, high)]
        level["entities"].append({"id": next_id, "parent": group, "name": f"Cell {name}", "cell": {},
                                  "transform": {"translation": center, "scale": size}})
        next_id += 1
        print(f"{name}: x {low[0]:.2f} to {high[0]:.2f}, y {low[1]:.2f} to {high[1]:.2f}, "
              f"z {low[2]:.2f} to {high[2]:.2f}, {len(cells)} floor cells")
    LEVEL.write_text(json.dumps(level, indent=2, sort_keys=True), encoding="utf-8")


if __name__ == "__main__":
    main()
