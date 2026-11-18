#!/usr/bin/env python3
"""Turns the showcase kit's lamps into prefabs with their lights (ADR-0082).

    python -I scripts/make_showcase_prefabs.py

Run after fetch_showcase_props.py. Each lamp's glowing material gets a luminance a real bulb has, through
KHR_materials_emissive_strength, and the extras {"light": false}: its glow is seen, while the prefab's own
light lights the room, with a shape, shadows and a power circuit (ADR-0044, ADR-0057). Each prefab is
assets/Factory/Prefabs/<lamp>.prefab.json: the lamp's model and its light under it, where the bulb is.
"""

import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FACTORY = ROOT / "assets" / "Factory"
KIT = FACTORY / "Showcase"
PREFABS = FACTORY / "Prefabs"
STRENGTH = "KHR_materials_emissive_strength"
WARM = [1.0, 0.85, 0.65]


def pitch(degrees):
    """A rotation about X, as a glTF quaternion [x, y, z, w]: a light's -Z turned down by that angle."""
    half = math.radians(degrees) / 2
    return [round(math.sin(half), 7), 0.0, 0.0, round(math.cos(half), 7)]


def turn_then_pitch(yaw, down):
    """Turned yaw degrees about Y, then tipped down: a floodlight facing out and down."""
    y = [0.0, math.sin(math.radians(yaw) / 2), 0.0, math.cos(math.radians(yaw) / 2)]
    x = pitch(-down)
    # y * x (Hamilton product, [x, y, z, w]).
    return [round(v, 7) for v in (
        y[3] * x[0] + y[0] * x[3] + y[1] * x[2] - y[2] * x[1],
        y[3] * x[1] - y[0] * x[2] + y[1] * x[3] + y[2] * x[0],
        y[3] * x[2] + y[0] * x[1] - y[1] * x[0] + y[2] * x[3],
        y[3] * x[3] - y[0] * x[0] - y[1] * x[1] - y[2] * x[2])]


# Lamp: (nits of its glow, its light: translation, rotation, light component).
LAMPS = {
    "hanging_industrial_lamp": (2500, [0, -1.22, 0], pitch(-90),
                                {"type": "spot", "color": WARM, "intensity": 900, "range": 14,
                                 "inner_angle": 30, "outer_angle": 60}),
    "industrial_wall_lamp": (1800, [0, -0.02, 0.1], [0, 0, 0, 1],
                             {"type": "point", "color": WARM, "intensity": 110, "range": 8}),
    "caged_hanging_light": (1800, [0, -0.55, 0], [0, 0, 0, 1],
                            {"type": "point", "color": WARM, "intensity": 140, "range": 9}),
    "security_light": (6000, [0, 0, 0.3], turn_then_pitch(180, 30),
                       {"type": "spot", "color": [0.9, 0.95, 1.0], "intensity": 2500, "range": 25,
                        "inner_angle": 20, "outer_angle": 45}),
}


def light_up(model, nits):
    path = KIT / model / f"{model}.gltf"
    gltf = json.loads(path.read_text(encoding="utf-8"))
    for material in gltf["materials"]:
        if "emissiveTexture" in material or material.get("emissiveFactor"):
            material["emissiveFactor"] = [1.0, 1.0, 1.0]
            material.setdefault("extensions", {})[STRENGTH] = {"emissiveStrength": nits}
            material.setdefault("extras", {})["light"] = False
    if any(STRENGTH in material.get("extensions", {}) for material in gltf["materials"]):
        gltf["extensionsUsed"] = sorted(set(gltf.get("extensionsUsed", [])) | {STRENGTH})
    path.write_text(json.dumps(gltf, indent=1), encoding="utf-8")


def prefab(model, translation, rotation, light):
    name = model.replace("_", " ").capitalize()
    return {
        "entities": [
            {"id": 1, "name": name, "model": f"../Showcase/{model}/{model}.gltf"},
            {"id": 2, "name": f"{name} light", "parent": 1, "light": light,
             "transform": {"translation": translation, "rotation": rotation}},
        ],
        "environment": {},
        "format": "daidalos-level",
        "version": 1,
    }


def main():
    PREFABS.mkdir(exist_ok=True)
    for model, (nits, translation, rotation, light) in LAMPS.items():
        light_up(model, nits)
        path = PREFABS / f"{model}.prefab.json"
        path.write_text(json.dumps(prefab(model, translation, rotation, light), indent=2, sort_keys=True) + "\n",
                        encoding="utf-8")
        print(path.relative_to(ROOT))


if __name__ == "__main__":
    main()
