#!/usr/bin/env python3
"""Checks that every text key the game names is in the English text table (ADR-0075).

    python scripts/check_text_keys.py

A key the code or a sequence names but assets/Text/en.json lacks shows as the key itself on screen. This
collects the string literals of the apps' sources and the sequences' text and caption fields that look like
keys (menu., options., hud., message., caption., subtitle., speaker., language., photo.) and reports the missing ones.
Exits 1 when it finds any.
"""

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
KEY = re.compile(r'"((?:menu|options|hud|message|caption|subtitle|speaker|language|photo)\.[a-z0-9_.]+)"')


def main():
    english = json.loads((ROOT / "assets" / "Text" / "en.json").read_text(encoding="utf-8"))
    used = {}
    files = list((ROOT / "apps" / "sandbox").rglob("*.cpp")) + list((ROOT / "apps" / "sandbox").rglob("*.hpp"))
    files += list((ROOT / "assets").rglob("*.sequence.json"))
    for file in files:
        for key in KEY.findall(file.read_text(encoding="utf-8")):
            used.setdefault(key, file.relative_to(ROOT))
    missing = sorted(key for key in used if key not in english)
    for key in missing:
        print(f"{used[key]}: '{key}' is not in assets/Text/en.json")
    print(f"{len(used)} keys used, {len(missing)} missing")
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
