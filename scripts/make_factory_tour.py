"""Makes the factory tour (ADR-0120): the legacy demo's camera flight over its whole level, Resources/Cinematics/
intro.dgc of the legacy engine, as a sequence of this one.

    python scripts/make_factory_tour.py [--legacy <daidalos-engine folder>] [--check]

Each legacy step is a camera position and rotation (pitch down, yaw from +Z to +X, in degrees, left-handed, in
the legacy units) and the seconds to the next step. They become keys of a smooth camera cue: positions in
meters, Z mirrored, as scripts/import_legacy_level.py converted the level, and the rotation as a point the
camera looks at. Steps that pass through what the level has gained since (props, a crane) are moved, by hand,
in FIXES; the flight ends with keys that bring it to the player's eyes.

--check samples the flight as the engine does (scene::CameraAt, smooth path) and lists where it comes closer
than CLEARANCE to the level, a door or a prop: what to move next. The sequence is written either way.
"""
import argparse
import json
import math
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import place_items as items  # noqa: E402  (the level's triangles)

ROOT = Path(__file__).resolve().parent.parent
OUTPUT = ROOT / "assets" / "Factory" / "Sequences" / "tour.sequence.json"
METERS_PER_UNIT = 0.35  # As scripts/import_legacy_level.py.
LOOK = 4.0  # Meters from the camera to the point a key looks at.
CLEARANCE = 0.3  # Meters the camera keeps from every surface.

# Keys by their legacy step index: a new position or look, in this engine's meters and degrees, where the
# curve through the legacy keys now passes too close to something (found with --check).
FIXES = {
    # Over the loading floor, inside its east wall (x 15.75) rather than through it.
    2: {"position": [12.5, 4.9, -28.0]},
    3: {"position": [12.5, 6.3, -45.5]},
    # Into the lockers' annex by the middle of its opening, and level with the crawl duct behind it.
    13: {"position": [45.9, 3.3, -67.4]},
    14: {"position": [45.5, 3.3, -77.0]},
    19: {"position": [45.5, 3.3, -77.0]},
    20: {"position": [45.9, 3.3, -67.4]},
    # Over the lockers (4.55 m high), not through their tops.
    21: {"position": [52.5, 5.0, -63.0]},
    # Under the east door's lintel (2.1 m) on the way back.
    25: {"position": [24.5, 1.55, -38.5]},
}

# Keys added between legacy steps, where the curve would cut a corner or a door frame: after the step of the
# index, at a fraction of the way to the next, a position; the look is the steps' own, in between.
INSERTS = {
    # The start room's door (2.1 m high), straight through its middle.
    0: [(0.45, [0.0, 1.6, -12.6])],
    # The east door, under its lintel.
    6: [(0.4, [24.5, 1.55, -38.5])],
    # Down the middle of the corridor to the lift, clear of the barrels.
    9: [(0.5, [31.5, 3.85, -56.0])],
    # The crawl duct (1.06 m wide, from 2.8 to 3.85 m high), straight through, both ways.
    13: [(0.15, [45.5, 3.3, -68.8]), (0.85, [45.5, 3.3, -75.4])],
    19: [(0.15, [45.5, 3.3, -75.4]), (0.85, [45.5, 3.3, -68.8])],
    # Past the corner of the lockers on the way to the west door.
    21: [(0.4, [46.8, 4.3, -60.4])],
    # Into the shaft from above its middle, not along the edge of the floor, half turned and half looking down,
    # so that the legacy half turn happens on the way down, as it did, and not when looking straight down.
    29: [(0.5, [-35.0, 0.6, -35.6])],
    # Round the corner of the upper corridor, not through it.
    36: [(0.6, [-34.6, 4.2, -76.4])],
}

# After the last legacy step, over the yard: back to the start room along the axis of the level, through the
# north corridor, its door, the hall and the start room's door, then a turn into the player's eyes, looking
# where the player looks, as the opening ends (ADR-0086). (seconds after the previous key, position, yaw, pitch)
ENDING = [
    (3.5, [0.0, 2.2, -78.0], 180.0, -5.0),
    (3.0, [0.0, 1.8, -67.0], 180.0, 0.0),
    (1.2, [0.0, 1.55, -63.0], 180.0, 0.0),
    (2.5, [0.0, 3.0, -52.0], 180.0, -5.0),
    (5.0, [0.0, 3.2, -30.0], 180.0, -5.0),
    (4.0, [0.0, 1.6, -12.6], 180.0, 0.0),
    (1.8, [0.0, 1.8, -6.0], 180.0, 0.0),
    (1.6, [1.2, 1.8, -1.5], 90.0, 0.0),
    (2.0, [0.0, 1.9, 0.3], 0.0, -1.0),
]

# A focus a little ahead, the lens as the opening's: a shallow depth of field only where it reads.
APERTURE = 0.0


def legacy_steps(legacy):
    text = (legacy / "Resources" / "Cinematics" / "intro.dgc").read_text(encoding="latin-1")
    steps = []
    for position, rotation, duration in re.findall(
            r"<position>(.*?)</position>\s*<rotation>(.*?)</rotation>\s*<duration>(\d+)</duration>", text):
        x, y, z = (float(value) for value in position.split(":"))
        pitch, yaw, _roll = (float(value) for value in rotation.split(":"))
        steps.append({"position": [x * METERS_PER_UNIT, y * METERS_PER_UNIT, -z * METERS_PER_UNIT],
                      # Yaw in this engine: 0 towards -Z, growing to the left. Legacy yaw 0 looks along its +Z,
                      # which is -Z here, and grows towards +X, which is to the right here.
                      "yaw": -yaw, "pitch": -pitch, "seconds": int(duration) / 1000.0})
    return steps


def direction(yaw, pitch):
    y, p = math.radians(yaw), math.radians(pitch)
    return [-math.sin(y) * math.cos(p), math.sin(p), -math.cos(y) * math.cos(p)]


def keys_of(steps):
    """Keys at the legacy times. Straight up or down becomes two degrees short of it, so that the key still says
    which way it faces; a turn of half a circle or more gets a key half way, so that it turns the way it did."""
    keys = []
    time = 0.0
    previous = None
    for index, step in enumerate(steps):
        fix = FIXES.get(index, {})
        position = fix.get("position", step["position"])
        yaw = fix.get("yaw", step["yaw"])
        pitch = max(-88.0, min(88.0, fix.get("pitch", step["pitch"])))
        if previous is not None and abs(yaw - previous["yaw"]) >= 170.0 and index - 1 not in INSERTS:
            middle = [(a + b) / 2 for a, b in zip(previous["position"], position)]
            keys.append(key(previous["at"] + (time - previous["at"]) / 2, middle, (yaw + previous["yaw"]) / 2,
                            (pitch + previous["pitch"]) / 2))
        keys.append(key(time, position, yaw, pitch))
        previous = {"at": time, "position": position, "yaw": yaw, "pitch": pitch}
        seconds = fix.get("seconds", step["seconds"])
        if index + 1 < len(steps):
            following = steps[index + 1]
            following_fix = FIXES.get(index + 1, {})
            next_yaw = following_fix.get("yaw", following["yaw"])
            next_pitch = max(-88.0, min(88.0, following_fix.get("pitch", following["pitch"])))
            for fraction, inserted in INSERTS.get(index, []):
                keys.append(key(time + seconds * fraction, inserted, yaw + (next_yaw - yaw) * fraction,
                                pitch + (next_pitch - pitch) * fraction))
        time += seconds
    # The last legacy step held its picture for its seconds; the ending flies on from there instead.
    time = keys[-1]["at"]
    for seconds, position, yaw, pitch in ENDING:
        time += seconds
        keys.append(key(time, position, yaw, pitch))
    return keys


def key(at, position, yaw, pitch, focus=LOOK):
    look = direction(yaw, pitch)
    return {"at": round(at, 3), "position": [round(v, 3) for v in position],
            "target": [round(p + d * focus, 3) for p, d in zip(position, look)],
            "aperture": APERTURE, "focus": focus, "yaw": yaw, "pitch": pitch}


def sequence(keys):
    written = [{name: value for name, value in k.items() if name not in ("yaw", "pitch")} for k in keys]
    return {
        "comment": "The factory tour (ADR-0120): the legacy demo's flight over its whole level, made by "
                   "scripts/make_factory_tour.py from the legacy intro.dgc, ending at the player's eyes. "
                   "sandbox.tour plays it instead of the menu.",
        "cues": [
            {"at": 0.0, "camera": {"path": "smooth", "keys": written}},
        ],
    }


def number(value):
    value = round(value, 3) + 0.0  # No -0.
    return str(int(value)) if value == int(value) else repr(value)


def compact(value):
    if isinstance(value, dict):
        return "{" + ", ".join(f"{json.dumps(k)}: {compact(v)}" for k, v in value.items()) + "}"
    if isinstance(value, list):
        return "[" + ", ".join(compact(v) for v in value) + "]"
    if isinstance(value, float):
        return number(value)
    return json.dumps(value)


def written(data):
    """As the other sequence files are written by hand: a cue per line, a camera key per line."""
    cues = []
    for cue in data["cues"]:
        if "camera" in cue:
            camera = cue["camera"]
            keys = ",\n".join("      " + compact(k) for k in camera["keys"])
            cues.append(f'    {{"at": {number(cue["at"])}, "camera": {{"path": "{camera["path"]}", "keys": [\n'
                        f"{keys}\n    ]}}}}")
        else:
            cues.append("    " + compact(cue))
    return "\n".join(["{", f'  "comment": {json.dumps(data["comment"])},', '  "cues": [', ",\n".join(cues),
                      "  ]", "}"]) + "\n"


# The engine's smooth path (scene::CameraAt), to check what the camera passes.
def hermite(a, b, ta, tb, span, t):
    t2, t3 = t * t, t * t * t
    return [a[i] * (2 * t3 - 3 * t2 + 1) + ta[i] * span * (t3 - 2 * t2 + t) + b[i] * (-2 * t3 + 3 * t2) +
            tb[i] * span * (t3 - t2) for i in range(len(a))]


def tangent(times, values, index):
    if index == 0 or index + 1 >= len(values):
        return [0.0] * len(values[index])
    span = max(times[index + 1] - times[index - 1], 1e-4)
    return [(values[index + 1][i] - values[index - 1][i]) / span for i in range(len(values[index]))]


def position_at(keys, time):
    times = [k["at"] for k in keys]
    positions = [k["position"] for k in keys]
    index = 1
    while index + 1 < len(keys) and time > times[index]:
        index += 1
    span = max(times[index] - times[index - 1], 1e-4)
    t = min(max((time - times[index - 1]) / span, 0.0), 1.0)
    return hermite(positions[index - 1], positions[index], tangent(times, positions, index - 1),
                   tangent(times, positions, index), span, t), index - 1


def closest_on_triangle(p, a, b, c):
    """The point of a triangle closest to p (Ericson, Real-Time Collision Detection, 5.1.5)."""
    def sub(u, v):
        return [u[0] - v[0], u[1] - v[1], u[2] - v[2]]

    def dot(u, v):
        return u[0] * v[0] + u[1] * v[1] + u[2] * v[2]

    def along(o, u, s):
        return [o[0] + u[0] * s, o[1] + u[1] * s, o[2] + u[2] * s]

    ab, ac, ap = sub(b, a), sub(c, a), sub(p, a)
    d1, d2 = dot(ab, ap), dot(ac, ap)
    if d1 <= 0 and d2 <= 0:
        return a
    bp = sub(p, b)
    d3, d4 = dot(ab, bp), dot(ac, bp)
    if d3 >= 0 and d4 <= d3:
        return b
    vc = d1 * d4 - d3 * d2
    if vc <= 0 and d1 >= 0 and d3 <= 0:
        return along(a, ab, d1 / (d1 - d3))
    cp = sub(p, c)
    d5, d6 = dot(ab, cp), dot(ac, cp)
    if d6 >= 0 and d5 <= d6:
        return c
    vb = d5 * d2 - d1 * d6
    if vb <= 0 and d2 >= 0 and d6 <= 0:
        return along(a, ac, d2 / (d2 - d6))
    va = d3 * d6 - d5 * d4
    if va <= 0 and (d4 - d3) >= 0 and (d5 - d6) >= 0:
        return along(b, sub(c, b), (d4 - d3) / ((d4 - d3) + (d5 - d6)))
    denominator = 1.0 / (va + vb + vc)
    v, w = vb * denominator, vc * denominator
    return [a[i] + ab[i] * v + ac[i] * w for i in range(3)]


def level_triangles():
    """Every triangle the camera could meet, named: the level's nodes (walls, doors, glass) and the props."""
    level = items.Level()
    named = []
    for entity in level.data["entities"]:
        model = entity.get("model")
        if not model:
            continue
        apply = level.world(entity)
        if model == "Factory.gltf":
            for name, triangles in factory_nodes():
                named.extend((name, tuple(apply(v) for v in t)) for t in triangles)
        else:
            label = entity.get("name", model)
            named.extend((label, tuple(apply(v) for v in t)) for t in items.model_triangles(model))
    return named


def factory_nodes():
    path = items.FACTORY / "Factory.gltf"
    gltf = json.loads(path.read_text(encoding="utf-8"))
    buffers = [(path.parent / b["uri"]).read_bytes() for b in gltf["buffers"]]
    for node in gltf["nodes"]:
        if "mesh" not in node or node.get("name", "").startswith("ladder"):
            continue
        t = node.get("translation", [0, 0, 0])
        triangles = []
        for primitive in gltf["meshes"][node["mesh"]]["primitives"]:
            points = [[v[0] + t[0], v[1] + t[1], v[2] + t[2]]
                      for v in items.read_accessor(gltf, buffers, primitive["attributes"]["POSITION"])]
            indices = (items.read_accessor(gltf, buffers, primitive["indices"]) if "indices" in primitive
                       else list(range(len(points))))
            triangles.extend((points[indices[i]], points[indices[i + 1]], points[indices[i + 2]])
                             for i in range(0, len(indices), 3))
        yield node["name"].split(";")[0] + (";" + node["name"].split(";")[1] if ";" in node["name"] else ""), triangles


def check(keys):
    cell = 2.0
    grid = {}
    for name, (a, b, c) in level_triangles():
        lo = [min(a[i], b[i], c[i]) - CLEARANCE for i in range(3)]
        hi = [max(a[i], b[i], c[i]) + CLEARANCE for i in range(3)]
        for gx in range(math.floor(lo[0] / cell), math.floor(hi[0] / cell) + 1):
            for gz in range(math.floor(lo[2] / cell), math.floor(hi[2] / cell) + 1):
                grid.setdefault((gx, gz), []).append((name, a, b, c, lo[1], hi[1]))
    length = keys[-1]["at"]
    step = 0.05
    problems = []
    time = 0.0
    while time <= length:
        p, segment = position_at(keys, time)
        nearest, what = CLEARANCE, None
        for name, a, b, c, low, high in grid.get((math.floor(p[0] / cell), math.floor(p[2] / cell)), []):
            if p[1] < low or p[1] > high:
                continue
            q = closest_on_triangle(p, a, b, c)
            distance = math.dist(p, q)
            if distance < nearest:
                nearest, what = distance, name
        if what is not None:
            problems.append((time, segment, p, nearest, what))
        time += step
    # One line per stretch of trouble.
    stretch = None
    for time, segment, p, distance, what in problems + [(None, None, None, None, None)]:
        if stretch and (time is None or time - stretch["end"] > step * 1.5 or what != stretch["what"]):
            print(f"{stretch['start']:7.2f}-{stretch['end']:7.2f} s  after key {stretch['segment']:2}  "
                  f"{stretch['closest']:.2f} m from {stretch['what']:24} at "
                  f"({stretch['p'][0]:.1f}, {stretch['p'][1]:.1f}, {stretch['p'][2]:.1f})")
            stretch = None
        if time is None:
            break
        if stretch is None:
            stretch = {"start": time, "end": time, "what": what, "segment": segment, "closest": distance, "p": p}
        else:
            stretch["end"] = time
            if distance < stretch["closest"]:
                stretch.update(closest=distance, p=p)
    print(f"{len(keys)} keys, {length:.1f} s; {len(problems)} samples closer than {CLEARANCE} m")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--legacy", type=Path, default=ROOT.parent / "daidalos-engine")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--list", action="store_true", help="print the keys")
    arguments = parser.parse_args()
    keys = keys_of(legacy_steps(arguments.legacy))
    if arguments.list:
        for index, k in enumerate(keys):
            print(f"{index:2} {k['at']:7.2f} s  ({k['position'][0]:6.2f}, {k['position'][1]:5.2f}, "
                  f"{k['position'][2]:7.2f})  yaw {k['yaw']:6.1f}  pitch {k['pitch']:5.1f}")
    OUTPUT.write_text(written(sequence(keys)), encoding="utf-8", newline="\n")
    if arguments.check:
        check(keys)


if __name__ == "__main__":
    main()
