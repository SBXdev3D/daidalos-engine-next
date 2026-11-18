#!/usr/bin/env python3
"""Checks a release archive the way a player gets it (ADR-0081).

    python scripts/check_package.py build/packages/daidalos-engine-next-0.1.0-windows-x64.zip [--seconds 25]

Unpacks the archive into an empty temporary folder, checks that what a player needs is in it (the game, the
settings, assets/, the licences) and that development files are not (the editor, the tools, PDBs), then starts
the game from that folder, with no build tree to fall back on. The game must still be running after the given
seconds, leave no crash dump, and write no error to its log. Warnings are listed. The folder is deleted after,
unless --keep is given.
"""

import argparse
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
import zipfile
from pathlib import Path

REQUIRED = ["assets/Factory/Factory.level.json", "assets/Text/en.json", "daidalos.cfg", "sandbox.cfg",
            "LICENSE-MIT", "LICENSE-APACHE", "THIRD_PARTY_NOTICES", "README.txt"]
FORBIDDEN_NAMES = {"editor", "editor.exe", "texture-tool", "texture-tool.exe", "crash-report", "crash-report.exe",
                   "level-viewer", "level-viewer.exe"}
FORBIDDEN_SUFFIXES = {".pdb", ".ilk", ".lib", ".exp"}


def unpack(archive: Path, into: Path) -> Path:
    if archive.suffix == ".zip":
        with zipfile.ZipFile(archive) as opened:
            opened.extractall(into)
    else:
        with tarfile.open(archive) as opened:
            opened.extractall(into, filter="data")
    # CPack puts everything in one folder named after the archive.
    entries = list(into.iterdir())
    return entries[0] if len(entries) == 1 and entries[0].is_dir() else into


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--seconds", type=float, default=25.0, help="how long the game must keep running")
    parser.add_argument("--keep", action="store_true", help="keep the unpacked folder")
    arguments = parser.parse_args()

    folder = Path(tempfile.mkdtemp(prefix="daidalos_package_"))
    problems = []
    try:
        root = unpack(arguments.archive, folder)
        print(f"Unpacked into {root}")
        for required in REQUIRED:
            if not (root / required).exists():
                problems.append(f"missing: {required}")
        for path in root.rglob("*"):
            if path.name in FORBIDDEN_NAMES or path.suffix.lower() in FORBIDDEN_SUFFIXES:
                problems.append(f"should not ship: {path.relative_to(root)}")
        game = root / ("sandbox.exe" if os.name == "nt" else "sandbox")
        if not game.exists():
            problems.append("missing: the game's executable")
        else:
            process = subprocess.Popen([str(game), "+window.fullscreen", "false", "+window.width", "1280",
                                        "+window.height", "720", "+sandbox.menu", "false"], cwd=root)
            time.sleep(arguments.seconds)
            if process.poll() is not None:
                problems.append(f"the game stopped by itself, exit code {process.returncode}")
            else:
                process.kill()
                process.wait()
            dumps = list((root / "crashdumps").glob("*.dmp")) if (root / "crashdumps").exists() else []
            if dumps:
                problems.append(f"crash dumps: {[dump.name for dump in dumps]}")
            log = root / "sandbox.log"
            if not log.exists():
                problems.append("no sandbox.log")
            else:
                lines = log.read_text(encoding="utf-8", errors="replace").splitlines()
                errors = [line for line in lines if "[error]" in line or "[critical]" in line]
                warnings = [line for line in lines if "[warning]" in line]
                problems += [f"log: {line}" for line in errors]
                print(f"{len(lines)} log lines, {len(warnings)} warnings")
                for line in warnings:
                    print(f"  {line}")
    finally:
        if arguments.keep:
            print(f"Kept {folder}")
        else:
            shutil.rmtree(folder, ignore_errors=True)
    for problem in problems:
        print(f"PROBLEM: {problem}")
    print("The package is fine." if not problems else f"{len(problems)} problem(s).")
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main())
