#!/usr/bin/env python3
"""Checks that every asset path the project names matches the file's name exactly, case included (ADR-0074).

    python scripts/check_asset_case.py

Windows and macOS find `Props/Crate.gltf` when the file is `props/crate.gltf`; Linux does not. This walks the
references a level, a model, an effect or a sequence makes (glTF buffers and images, level models, sequences,
effect textures and flipbooks, sound files) and the asset paths written in the sandbox's and editor's code, and
reports each one whose spelling differs from the file on disk. Exits 1 when it finds any.
"""

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "assets"


def exact(base: Path, relative: str):
    """Returns None when base/relative exists with this exact spelling, else what is wrong."""
    path = base
    for part in Path(relative.replace("\\", "/")).parts:
        if part in ("", "."):
            continue
        if part == "..":
            path = path.parent
            continue
        if not path.is_dir():
            return f"'{path}' is not a folder"
        names = {child.name for child in path.iterdir()}
        if part not in names:
            lower = {name.lower(): name for name in names}
            if part.lower() in lower:
                return f"'{part}' is spelled '{lower[part.lower()]}' on disk"
            return f"'{part}' does not exist in '{path.relative_to(ROOT)}'"
        path = path / part
    return None


def strings(value):
    if isinstance(value, str):
        yield value
    elif isinstance(value, dict):
        # Names are labels, not files (glTF materials are often named after their texture).
        for key, item in value.items():
            if key != "name":
                yield from strings(item)
    elif isinstance(value, list):
        for item in value:
            yield from strings(item)


FILE_LIKE = re.compile(r"^[\w./\\ -]+\.(gltf|glb|bin|png|jpg|ktx2|json|wav|ogg|mp3|flac|slang|ttf|otf)$", re.I)


def main():
    problems = []
    # References inside asset files resolve from the file's folder, or from the assets folder it lives in.
    for file in sorted(ASSETS.rglob("*")):
        if file.suffix.lower() not in (".gltf", ".json"):
            continue
        try:
            data = json.loads(file.read_text(encoding="utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            continue
        for text in strings(data):
            if text.startswith("data:") or not FILE_LIKE.match(text):
                continue
            # Tried from the file's folder, then up its folders to the assets root.
            errors = []
            folder = file.parent
            while True:
                error = exact(folder, text)
                if error is None:
                    errors = []
                    break
                errors.append(error)
                if folder == ASSETS or folder == ROOT:
                    break
                folder = folder.parent
            if errors:
                case = [e for e in errors if "spelled" in e]
                problems.append(f"{file.relative_to(ROOT)}: '{text}': {(case or errors)[0]}")
    # Asset paths written in code: string literals that look like files under assets/.
    literal = re.compile(r'"([\w./-]+\.(?:gltf|glb|ktx2|json|wav|ogg|png|ttf))"')
    for file in sorted((ROOT / "apps").rglob("*.cpp")) + sorted((ROOT / "engine").rglob("*.cpp")):
        for text in literal.findall(file.read_text(encoding="utf-8", errors="replace")):
            candidates = [ASSETS / sub for sub in ("", "Factory", "Sandbox", "Editor")]
            results = [exact(base, text) for base in candidates if base.is_dir()]
            if all(r is not None for r in results):
                case = [r for r in results if r and "spelled" in r]
                if case:
                    problems.append(f"{file.relative_to(ROOT)}: '{text}': {case[0]}")
    for problem in problems:
        print(problem)
    print(f"{len(problems)} problem(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
