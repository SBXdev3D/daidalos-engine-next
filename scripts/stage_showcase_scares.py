#!/usr/bin/env python3
"""Stages the factory showcase's scares and its way out (ADR-0084).

    python -I scripts/stage_showcase_scares.py

Writes a sequence file per scare into assets/Factory/Sequences/ and a trigger box per scare into
Factory.level.json, all under an entity named "Scares: showcase" that the next run replaces. Run after
dress_showcase.py, whose rooms' power circuits and props the sequences use. The editor rewrites the level in
the engine's layout afterwards (editor +editor.resave true), and opens each sequence as a timeline.

The route, in order: the hall goes dark as the player enters it; something answers from behind the workshop's
wall and the zombies come; the yard's alarm calls them again; the pumps go dark for good, flashlight only;
the far end of the pumps is the way out, under a floodlight.
"""

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FACTORY = ROOT / "assets" / "Factory"
LEVEL = FACTORY / "Factory.level.json"
SEQUENCES = FACTORY / "Sequences"
GROUP = "Scares: showcase"
SOUNDS = "Factory/Sounds"


def power(at, circuit, on):
    return {"at": at, "power": {"circuit": circuit, "on": on}}


def sound(at, clip, entity=None, volume=1.0, ducking=0.3, caption=None):
    body = {"clip": f"{SOUNDS}/{clip}", "volume": volume, "ducking": ducking}
    if entity:
        body["entity"] = entity
    if caption:
        body["caption"] = caption
    return {"at": at, "sound": body}


def subtitle(at, text, seconds=3.5):
    return {"at": at, "subtitle": {"speaker": "speaker.you", "text": text, "seconds": seconds}}


def event(at, name):
    return {"at": at, "event": name}


def shake(at, strength, seconds):
    return {"at": at, "shake": {"strength": strength, "seconds": seconds}}


def mix(at, lowpass, seconds):
    return {"at": at, "mix": {"lowpass": lowpass, "seconds": seconds}}


# Name: (comment, trigger center, trigger size, cues).
SCARES = {
    "hall_blackout": (
        "Into the loading floor: its power dies, something groans in the middle of the floor, and comes back stuttering.",
        [0.5, 1.0, -21.5], [4, 3, 2], [
            event(0.0, "checkpoint"), power(0.0, "loading_floor", False),
            sound(0.3, "zombie_groan2.wav", "Fog 83", caption="caption.zombie_groan"),
            shake(0.5, 0.3, 0.6), subtitle(1.0, "subtitle.hall.you"), event(1.2, "stinger"),
            power(7.5, "loading_floor", True)]),
    "workshop_wall": (
        "Into the workshop: a groan behind the wall, the lights cut, and every zombie near hears the player.",
        [27.5, 1.0, -38.5], [2, 3, 4], [
            sound(0.0, "zombie_groan1.wav", "Drill press 01", caption="caption.zombie_groan"),
            power(0.6, "workshop", False), subtitle(0.8, "subtitle.workshop.you"), event(1.5, "ambush"),
            event(2.0, "stinger"), power(4.0, "workshop", True)]),
    "yard_alarm": (
        "Out into the yard: the alarm goes off, the floodlights die, and the zombies come.",
        [0.5, 1.0, -80.0], [3, 3, 2], [
            power(0.0, "yard", False), sound(0.0, "alarm.wav", volume=0.8, caption="caption.power_out"),
            event(0.3, "ambush"), shake(0.5, 0.2, 0.4), event(1.0, "stinger"), power(5.0, "yard", True)]),
    "pumps_dark": (
        "Down to the pumps: their lights go out for good. The flashlight is all there is.",
        [-37.0, -7.4, -37.0], [6, 3, 2], [
            event(0.0, "checkpoint"), power(0.0, "pumps", False), mix(0.2, 1500, 1.0),
            subtitle(0.8, "subtitle.pumps.you"), sound(2.0, "zombie_moan.wav", volume=0.7),
            mix(4.0, 20000, 2.0)]),
    "way_out": (
        "The far end of the pumps: a shaft going up, the way out.",
        [-37.0, -7.4, -59.5], [6, 3, 2], [
            subtitle(0.0, "subtitle.exit.you", 2.5), event(2.5, "exit")]),
}


def main():
    SEQUENCES.mkdir(exist_ok=True)
    level = json.loads(LEVEL.read_text(encoding="utf-8"))
    gone = {e["id"] for e in level["entities"] if e.get("name") == GROUP}
    level["entities"] = [e for e in level["entities"] if e["id"] not in gone and e.get("parent", 0) not in gone]
    next_id = max(e["id"] for e in level["entities"]) + 1
    group = next_id
    level["entities"].append({"id": group, "name": GROUP})
    next_id += 1
    for name, (comment, center, size, cues) in SCARES.items():
        sequence = {"comment": f"{comment} Staged by scripts/stage_showcase_scares.py (ADR-0084).", "cues": cues}
        (SEQUENCES / f"{name}.sequence.json").write_text(json.dumps(sequence, indent=2) + "\n", encoding="utf-8")
        level["entities"].append({"id": next_id, "parent": group, "name": f"Trigger {name.replace('_', ' ')}",
                                  "transform": {"translation": center, "scale": size},
                                  "trigger": {"sequence": f"Sequences/{name}.sequence.json"}})
        next_id += 1
    # The way out shows: a floodlight over it, facing back along the pumps.
    level["entities"].append({"id": next_id, "parent": group, "name": "Way out floodlight",
                              "model": "Showcase/security_light/security_light.gltf",
                              "transform": {"translation": [-37.0, -5.9, -60.6]}})
    level["entities"].append({"id": next_id + 1, "parent": next_id, "name": "Way out floodlight light",
                              "light": {"type": "spot", "color": [0.9, 0.95, 1.0], "intensity": 2500, "range": 25,
                                        "inner_angle": 20, "outer_angle": 45},
                              "transform": {"translation": [0, 0, 0.3], "rotation": [0, 0.9659259, 0.258819, 0]}})
    LEVEL.write_text(json.dumps(level, indent=2, sort_keys=True), encoding="utf-8")
    print(f"{len(SCARES)} scares staged")


if __name__ == "__main__":
    main()
