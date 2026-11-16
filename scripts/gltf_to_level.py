#!/usr/bin/env python3
# Daidalos Engine Next
# Copyright (C) 2011-2026 Stéphane Baudoux
# SPDX-License-Identifier: MIT OR Apache-2.0
"""Converts a glTF level whose node names mark what the game builds ("kind;key=value") into a level file
(ADR-0054), once.

- Nodes without a mesh that mark a place (lights, emitters, fog, wind, attractors, weather, ribbons, lightning,
  the sky, the player's start, zombies and fans) become level entities and leave the glTF file.
- Props (lamps with their light, alarms, pickups, the security camera, the card reader) go to glTF files of
  their own under Props/, one per mesh, and become entities that place them.
- Walls, floors, doors, ladders and glass stay in the glTF file, with their names.
- What the factory built in code becomes data: the moon, the sky's yaw, the exposure, the dust of the start room,
  the steam of the leak and the alarms' beams.

Usage: gltf_to_level.py assets/Factory/Factory.gltf [--factory]
--factory adds what the sandbox's code used to build for the factory.
Afterwards, `editor +editor.resave true +editor.level <level>` rewrites the file in the engine's own layout.
"""

import argparse
import copy
import json
import math
import os
import struct
import sys

PROP_KINDS = {"spot", "alarm", "health", "armor", "handgun_ammo", "shotgun_ammo", "flashlight", "card", "shotgun",
              "security_camera", "card_reader"}
GAME_MARKERS = {"player", "zombie", "fan", "teleporter", "area_trigger", "cable_arc_end"}
ENGINE_MARKERS = {"particles", "fog", "attractor", "weather", "wind", "ribbon", "lightning", "sky"}


# Quaternions as [x, y, z, w], as glTF writes them.
def quat_mul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return [aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz]


def quat_rotate(q, v):
    x, y, z, w = q
    # v' = q v q*
    qv = [v[0], v[1], v[2], 0.0]
    r = quat_mul(quat_mul(q, qv), [-x, -y, -z, w])
    return r[:3]


def axis_angle(axis, angle):
    s = math.sin(angle / 2)
    return [axis[0] * s, axis[1] * s, axis[2] * s, math.cos(angle / 2)]


def look_at(direction, up=(0.0, 1.0, 0.0)):
    """The rotation whose -Z looks along direction, as glm::quatLookAtRH."""
    f = normalize(direction)
    s = normalize(cross(f, up))
    u = cross(s, f)
    # Columns: right = s, up = u, back = -f.
    m = [[s[0], u[0], -f[0]], [s[1], u[1], -f[1]], [s[2], u[2], -f[2]]]
    trace = m[0][0] + m[1][1] + m[2][2]
    if trace > 0:
        t = math.sqrt(trace + 1.0) * 2
        return [(m[2][1] - m[1][2]) / t, (m[0][2] - m[2][0]) / t, (m[1][0] - m[0][1]) / t, 0.25 * t]
    if m[0][0] > m[1][1] and m[0][0] > m[2][2]:
        t = math.sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]) * 2
        return [0.25 * t, (m[0][1] + m[1][0]) / t, (m[0][2] + m[2][0]) / t, (m[2][1] - m[1][2]) / t]
    if m[1][1] > m[2][2]:
        t = math.sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]) * 2
        return [(m[0][1] + m[1][0]) / t, 0.25 * t, (m[1][2] + m[2][1]) / t, (m[0][2] - m[2][0]) / t]
    t = math.sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]) * 2
    return [(m[0][2] + m[2][0]) / t, (m[1][2] + m[2][1]) / t, 0.25 * t, (m[1][0] - m[0][1]) / t]


def normalize(v):
    length = math.sqrt(sum(c * c for c in v))
    return [c / length for c in v]


def cross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def trs(node):
    return (list(node.get("translation", [0.0, 0.0, 0.0])), list(node.get("rotation", [0.0, 0.0, 0.0, 1.0])),
            list(node.get("scale", [1.0, 1.0, 1.0])))


def compose(parent, local):
    pt, pr, ps = parent
    lt, lr, ls = local
    scaled = [lt[i] * ps[i] for i in range(3)]
    rotated = quat_rotate(pr, scaled)
    return ([pt[i] + rotated[i] for i in range(3)], quat_mul(pr, lr), [ps[i] * ls[i] for i in range(3)])


def clean(value):
    """Rounds away float noise, such as 8.399999999999999."""
    if isinstance(value, float):
        rounded = round(value, 5)
        return 0.0 if rounded == 0 else rounded
    if isinstance(value, list):
        return [clean(item) for item in value]
    return value


def transform_json(t):
    translation, rotation, scale = t
    out = {}
    if any(abs(c) > 1e-6 for c in translation):
        out["translation"] = clean(translation)
    if any(abs(rotation[i] - [0, 0, 0, 1][i]) > 1e-6 for i in range(4)):
        out["rotation"] = clean(rotation)
    if any(abs(c - 1) > 1e-6 for c in scale):
        out["scale"] = clean(scale)
    return out


def parse_name(name):
    parts = name.split(";")
    values = {}
    for field in parts[1:]:
        if "=" in field:
            key, value = field.split("=", 1)
            values[key] = value
    return parts[0], values


def number(values, key, fallback):
    return float(values[key]) if key in values else fallback


def vector(text):
    return [float(c) for c in text.split(",")]


def color(text):
    return [float(c) for c in text.split(":")[:3]]


class Converter:
    def __init__(self, path, factory):
        self.path = path
        self.directory = os.path.dirname(path)
        self.stem = os.path.splitext(os.path.basename(path))[0]
        self.factory = factory
        with open(path, encoding="utf-8") as f:
            self.gltf = json.load(f)
        self.buffers = []
        for buffer in self.gltf["buffers"]:
            with open(os.path.join(self.directory, buffer["uri"]), "rb") as f:
                self.buffers.append(f.read())
        self.entities = []
        self.next_id = 2  # 1 is the model itself.
        self.ids_by_name = {}
        self.props = {}  # (kind, mesh) -> relative path

    def new_id(self):
        value = self.next_id
        self.next_id += 1
        return value

    def add(self, entity):
        self.entities.append(entity)
        return entity["id"]

    # Props: a mesh of the level written to a glTF file of its own.
    def extract_mesh(self, kind, mesh_index):
        key = (kind, mesh_index)
        if key in self.props:
            return self.props[key]
        name = "lamp" if kind == "spot" else kind
        same_kind = [k for k in self.props if k[0] == kind]
        file_name = f"{name}.gltf" if not same_kind else f"{name}_{len(same_kind) + 1}.gltf"
        relative = f"Props/{file_name}"
        self.props[key] = relative
        self.write_prop(os.path.join(self.directory, "Props", file_name), mesh_index)
        return relative

    def write_prop(self, path, mesh_index):
        g = self.gltf
        out = {"asset": copy.deepcopy(g["asset"]), "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0}],
               "meshes": [], "accessors": [], "bufferViews": [], "materials": [], "textures": [], "images": [],
               "samplers": []}
        blob = bytearray()
        accessor_map, view_map, material_map, texture_map, image_map, sampler_map = {}, {}, {}, {}, {}, {}

        def view(index):
            if index not in view_map:
                source = g["bufferViews"][index]
                data = self.buffers[source["buffer"]][source.get("byteOffset", 0):
                                                      source.get("byteOffset", 0) + source["byteLength"]]
                while len(blob) % 4:
                    blob.append(0)
                new = {k: v for k, v in source.items() if k not in ("buffer", "byteOffset")}
                new["buffer"] = 0
                new["byteOffset"] = len(blob)
                blob.extend(data)
                view_map[index] = len(out["bufferViews"])
                out["bufferViews"].append(new)
            return view_map[index]

        def accessor(index):
            if index not in accessor_map:
                new = copy.deepcopy(g["accessors"][index])
                if "bufferView" in new:
                    new["bufferView"] = view(new["bufferView"])
                accessor_map[index] = len(out["accessors"])
                out["accessors"].append(new)
            return accessor_map[index]

        def image(index):
            if index not in image_map:
                new = copy.deepcopy(g["images"][index])
                if "uri" in new:
                    new["uri"] = "../" + new["uri"]
                else:
                    new["bufferView"] = view(new["bufferView"])
                image_map[index] = len(out["images"])
                out["images"].append(new)
            return image_map[index]

        def sampler(index):
            if index not in sampler_map:
                sampler_map[index] = len(out["samplers"])
                out["samplers"].append(copy.deepcopy(g["samplers"][index]))
            return sampler_map[index]

        def texture(index):
            if index not in texture_map:
                new = copy.deepcopy(g["textures"][index])
                if "source" in new:
                    new["source"] = image(new["source"])
                if "sampler" in new:
                    new["sampler"] = sampler(new["sampler"])
                for extension in new.get("extensions", {}).values():
                    if "source" in extension:
                        extension["source"] = image(extension["source"])
                texture_map[index] = len(out["textures"])
                out["textures"].append(new)
            return texture_map[index]

        def remap_textures(value):
            if isinstance(value, dict):
                for key, item in value.items():
                    if key.endswith("Texture") and isinstance(item, dict) and "index" in item:
                        item["index"] = texture(item["index"])
                    remap_textures(item)
            elif isinstance(value, list):
                for item in value:
                    remap_textures(item)

        def material(index):
            if index not in material_map:
                new = copy.deepcopy(g["materials"][index])
                remap_textures(new)
                material_map[index] = len(out["materials"])
                out["materials"].append(new)
            return material_map[index]

        mesh = copy.deepcopy(g["meshes"][mesh_index])
        for primitive in mesh["primitives"]:
            primitive["attributes"] = {k: accessor(v) for k, v in primitive["attributes"].items()}
            if "indices" in primitive:
                primitive["indices"] = accessor(primitive["indices"])
            if "material" in primitive:
                primitive["material"] = material(primitive["material"])
            for target in primitive.get("targets", []):
                for k in target:
                    target[k] = accessor(target[k])
        out["meshes"].append(mesh)
        bin_name = os.path.splitext(os.path.basename(path))[0] + ".bin"
        out["buffers"] = [{"uri": bin_name, "byteLength": len(blob)}]
        used = set()
        for m in out["materials"]:
            used.update(m.get("extensions", {}).keys())
        for t in out["textures"]:
            used.update(t.get("extensions", {}).keys())
        if used:
            out["extensionsUsed"] = sorted(used)
            required = [e for e in g.get("extensionsRequired", []) if e in used]
            if required:
                out["extensionsRequired"] = required
        for key in ("textures", "images", "samplers", "materials"):
            if not out[key]:
                del out[key]
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            json.dump(out, f, indent=1)
            f.write("\n")
        with open(os.path.join(os.path.dirname(path), bin_name), "wb") as f:
            f.write(blob)

    def light_json(self, light_index):
        light = self.gltf["extensions"]["KHR_lights_punctual"]["lights"][light_index]
        out = {"type": light["type"], "intensity": light["intensity"]}
        if light.get("color", [1, 1, 1]) != [1, 1, 1]:
            out["color"] = light["color"]
        if "range" in light:
            out["range"] = light["range"]
        if light["type"] == "spot":
            spot = light.get("spot", {})
            out["inner_angle"] = round(math.degrees(spot.get("innerConeAngle", 0.0)), 4)
            out["outer_angle"] = round(math.degrees(spot.get("outerConeAngle", math.pi / 4)), 4)
        return out

    def effect(self, name, suffix="effect"):
        return f"Effects/{name}.{suffix}.json"

    def convert(self):
        g = self.gltf
        nodes = g["nodes"]
        parents = {}
        for index, node in enumerate(nodes):
            for child in node.get("children", []):
                parents[child] = index
        world = {}

        def world_of(index):
            if index not in world:
                local = trs(nodes[index])
                world[index] = compose(world_of(parents[index]), local) if index in parents else local
            return world[index]

        removed = set()
        pending_refs = []  # (entity, key, node name) resolved once every entity has an id

        # The nodes that ribbons and lightning point at become entities, whatever their kind.
        referenced = set()
        for node in nodes:
            kind, values = parse_name(node.get("name", ""))
            if kind == "ribbon" and "target" in values:
                referenced.add(values["target"])
            if kind == "lightning" and "light" in values:
                referenced.add(values["light"])

        for index, node in enumerate(nodes):
            name = node.get("name", "")
            kind, values = parse_name(name)
            has_mesh = "mesh" in node
            light = node.get("extensions", {}).get("KHR_lights_punctual", {}).get("light")
            if has_mesh and kind in PROP_KINDS:
                entity = {"id": self.new_id(), "name": self.pretty(kind, index),
                          "transform": transform_json(world_of(index)),
                          "model": self.extract_mesh(kind, node["mesh"])}
                game = {k: v for k, v in values.items()}
                if kind != "spot":
                    entity["game"] = {"kind": kind, **game}
                self.add(entity)
                removed.add(index)
                # Its light, if a child holds one, comes along as a child entity.
                for child in node.get("children", []):
                    child_node = nodes[child]
                    child_light = child_node.get("extensions", {}).get("KHR_lights_punctual", {}).get("light")
                    if child_light is not None and "mesh" not in child_node:
                        self.add({"id": self.new_id(), "name": entity["name"] + " light", "parent": entity["id"],
                                  "transform": transform_json(trs(child_node)), "light": self.light_json(child_light)})
                        removed.add(child)
                if kind == "alarm":
                    self.add_alarm_beams(entity)
                continue
            if has_mesh:
                continue
            if index in removed:
                continue
            transform = transform_json(world_of(index))
            if light is not None:
                entity_id = self.add({"id": self.new_id(), "name": name or "Light", "transform": transform,
                                      "light": self.light_json(light)})
                self.ids_by_name[name] = entity_id
                removed.add(index)
                continue
            if kind not in GAME_MARKERS | ENGINE_MARKERS and "model" not in values and name not in referenced:
                continue
            removed.add(index)
            entity = {"id": self.new_id(), "name": self.pretty(kind, index, values), "transform": transform}
            self.ids_by_name[name] = entity["id"]
            if kind == "sky":
                self.sky = values.get("name")
                continue
            if kind == "particles":
                particles = {"effect": self.effect(values["effect"])}
                if "burst" in values:
                    count, _, interval = values["burst"].partition(":")
                    particles["burst"] = {"count": int(count), "interval": float(interval or 1)}
                if "surface" in values:
                    particles["surface"] = {"entity": 1, "node": values["surface"]}
                entity["particles"] = particles
                if values["effect"] == "smoke_leak" and self.factory:
                    self.add(entity)
                    self.add_steam(entity)
                    continue
            elif kind == "fog":
                fog = {}
                for key, field in (("density", "density"), ("edge", "edge"), ("falloff", "falloff_height"),
                                   ("noise", "noise"), ("noise_frequency", "noise_frequency")):
                    if key in values:
                        fog[field] = float(values[key])
                if "color" in values:
                    fog["albedo"] = color(values["color"])
                if "noise_velocity" in values:
                    fog["noise_velocity"] = vector(values["noise_velocity"])
                if "glow" in values:
                    fog["glow"] = color(values["glow"])
                entity["fog"] = fog
            elif kind == "attractor":
                attractor = {}
                if "effect" in values:
                    attractor["effect"] = self.effect(values["effect"])
                for key in ("strength", "vortex", "radius"):
                    if key in values:
                        attractor[key] = float(values[key])
                entity["attractor"] = attractor
            elif kind == "weather":
                weather = {"effect": self.effect(values["effect"])}
                for key in ("rate", "reach"):
                    if key in values:
                        weather[key] = float(values[key])
                entity["weather"] = weather
            elif kind == "wind":
                wind = {}
                if "velocity" in values:
                    wind["velocity"] = vector(values["velocity"])
                for key, field in (("gust", "gust"), ("gust_frequency", "gust_frequency"), ("edge", "edge")):
                    if key in values:
                        wind[field] = float(values[key])
                entity["wind"] = wind
            elif kind == "ribbon":
                entity["ribbon"] = {"effect": self.effect(values["effect"], "ribbon")}
                if "target" in values:
                    pending_refs.append((entity["ribbon"], "target", values["target"]))
            elif kind == "lightning":
                lightning = {"bolt": self.effect(values.get("bolt", "lightning"), "ribbon")}
                for key in ("flash", "duration"):
                    if key in values:
                        lightning[key] = float(values[key])
                if "interval" in values:
                    low, _, high = values["interval"].partition(":")
                    lightning["interval"] = [float(low), float(high or low)]
                if "light" in values:
                    pending_refs.append((lightning, "light", values["light"]))
                entity["lightning"] = lightning
            elif kind in GAME_MARKERS or "model" in values:
                game = {"kind": kind}
                for key, value in values.items():
                    if key == "model":
                        entity["model"] = value
                    else:
                        game[key] = value
                entity["game"] = game
                if kind == "player" and self.factory:
                    self.add(entity)
                    self.add_dust(world_of(index)[0])
                    continue
            self.add(entity)

        for target, key, name in pending_refs:
            if self.ids_by_name.get(name):
                target[key] = self.ids_by_name[name]
            else:
                print(f"warning: no marker named '{name}' for a reference", file=sys.stderr)

        environment = {}
        if getattr(self, "sky", None):
            environment["sky"] = {"faces": f"Sky/{self.sky}"}
        if self.factory:
            self.add_factory_environment(environment)
        self.write_level(environment)
        self.prune(removed)

    def pretty(self, kind, index, values=None):
        name = kind.replace("_", " ").capitalize()
        if kind == "spot":
            name = "Lamp"
        if values and "effect" in values:
            name = f"{name} {values['effect'].replace('_', ' ')}"
        return f"{name} {index}"

    # What the sandbox's code built for the factory (Factory.cpp before ADR-0054), as data.
    def add_alarm_beams(self, alarm):
        # Two red spots back to back, 0.7 m under the alarm, 25 degrees below the horizon.
        for number_, yaw in enumerate((0.0, math.pi)):
            rotation = quat_mul(axis_angle([0, 1, 0], yaw), axis_angle([1, 0, 0], -math.radians(25.0)))
            self.add({"id": self.new_id(), "name": f"{alarm['name']} beam {number_ + 1}", "parent": alarm["id"],
                      "transform": {"translation": [0.0, -0.7, 0.0], "rotation": clean(rotation)},
                      "light": {"type": "spot", "color": [1.0, 0.0, 0.0], "intensity": 965.0, "range": 28.0,
                                "inner_angle": 16.0, "outer_angle": 20.0}})

    def add_steam(self, leak):
        self.add({"id": self.new_id(), "name": "Steam", "parent": leak["id"],
                  "transform": {"translation": [0.0, 0.1, -1.8], "scale": [1.6, 1.6, 4.0]},
                  "fog": {"density": 0.8, "albedo": [0.95, 0.95, 0.95], "edge": 0.8, "noise": 0.8,
                          "noise_frequency": 1.5, "noise_velocity": [0.0, 0.1, -0.5]}})

    def add_dust(self, start):
        self.add({"id": self.new_id(), "name": "Dust of the start room",
                  "transform": {"translation": clean([start[0], start[1], start[2] - 3.5]),
                                "scale": [6.0, 3.0, 8.0]},
                  "fog": {"density": 0.1, "albedo": [0.85, 0.8, 0.7], "edge": 0.3, "noise": 0.7,
                          "noise_frequency": 0.8, "noise_velocity": [0.08, 0.03, 0.0]}})

    def add_factory_environment(self, environment):
        if getattr(self, "sky", None) == "sky1":
            yaw = math.pi
            environment["sky"] = {"faces": "Sky/sky1", "intensity": 9.6, "yaw": 180.0}
            # The moon painted on sky1, turned with the sky.
            towards = normalize([0.344, 0.424, -1.0])
            towards = quat_rotate(axis_angle([0, 1, 0], yaw), towards)
            self.add({"id": self.new_id(), "name": "Moon",
                      "transform": {"rotation": clean(look_at([-c for c in towards]))},
                      "light": {"type": "directional", "color": [0.88, 1.0, 1.26], "intensity": 24.0,
                                "angular_radius": 2.5}})
        environment["fog"] = {"albedo": [0.8, 0.82, 0.85], "anisotropy": 0.1}
        environment["exposure"] = {"ev100": 3.0, "auto": True, "min": 0.5, "max": 2.5}

    def write_level(self, environment):
        model = {"id": 1, "name": self.stem, "model": os.path.basename(self.path)}
        level = {"format": "daidalos-level", "version": 1, "environment": environment,
                 "entities": [model] + self.entities}
        path = os.path.join(self.directory, self.stem + ".level.json")
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            json.dump(level, f, indent=2, sort_keys=True)
            f.write("\n")
        print(f"{path}: {len(level['entities'])} entities, {len(self.props)} props")

    def prune(self, removed):
        """Removes the converted nodes, and the meshes and lights no node uses any more."""
        g = self.gltf
        nodes = g["nodes"]
        keep = [i for i in range(len(nodes)) if i not in removed]
        remap = {old: new for new, old in enumerate(keep)}
        new_nodes = []
        for old in keep:
            node = nodes[old]
            if "children" in node:
                children = [remap[c] for c in node["children"] if c in remap]
                if children:
                    node["children"] = children
                else:
                    del node["children"]
            new_nodes.append(node)
        g["nodes"] = new_nodes
        for scene in g["scenes"]:
            scene["nodes"] = [remap[n] for n in scene["nodes"] if n in remap]
        # Meshes no node draws.
        used_meshes = sorted({n["mesh"] for n in new_nodes if "mesh" in n})
        mesh_remap = {old: new for new, old in enumerate(used_meshes)}
        g["meshes"] = [g["meshes"][i] for i in used_meshes]
        for node in new_nodes:
            if "mesh" in node:
                node["mesh"] = mesh_remap[node["mesh"]]
        # Lights no node holds.
        lights = g.get("extensions", {}).get("KHR_lights_punctual", {}).get("lights")
        if lights is not None:
            used = sorted({n["extensions"]["KHR_lights_punctual"]["light"] for n in new_nodes
                           if "KHR_lights_punctual" in n.get("extensions", {})})
            light_remap = {old: new for new, old in enumerate(used)}
            g["extensions"]["KHR_lights_punctual"]["lights"] = [lights[i] for i in used]
            for node in new_nodes:
                ext = node.get("extensions", {}).get("KHR_lights_punctual")
                if ext is not None:
                    ext["light"] = light_remap[ext["light"]]
            if not used:
                del g["extensions"]["KHR_lights_punctual"]
                if not g["extensions"]:
                    del g["extensions"]
                g["extensionsUsed"] = [e for e in g["extensionsUsed"] if e != "KHR_lights_punctual"]
        with open(self.path, "w", encoding="utf-8", newline="\n") as f:
            json.dump(g, f, indent=1)
            f.write("\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("gltf")
    parser.add_argument("--factory", action="store_true")
    args = parser.parse_args()
    Converter(args.gltf, args.factory).convert()


if __name__ == "__main__":
    main()
