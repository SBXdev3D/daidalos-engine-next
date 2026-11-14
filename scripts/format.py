#!/usr/bin/env python3
# Daidalos Engine Next
# Copyright (C) 2011-2026 Stéphane Baudoux
# SPDX-License-Identifier: MIT OR Apache-2.0
"""Formats the engine sources with clang-format, or checks them with --check (used by CI)."""

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE_DIRS = ("engine", "apps", "tests")
EXTENSIONS = {".hpp", ".cpp", ".h", ".inl"}
REQUIRED_MAJOR_VERSION = 19


def find_clang_format(explicit: str | None) -> str:
    if explicit:
        return explicit
    found = shutil.which("clang-format")
    if found:
        return found
    sys.exit("clang-format not found. Install clang-format 19 or pass --clang-format <path>.")


def check_version(executable: str) -> None:
    output = subprocess.run([executable, "--version"], capture_output=True, text=True, check=True).stdout
    version = output.split("version", 1)[-1].strip().split()[0]
    major = int(version.split(".")[0])
    if major != REQUIRED_MAJOR_VERSION:
        sys.exit(f"clang-format {REQUIRED_MAJOR_VERSION} is required, found {version}. Results would differ from CI.")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="report unformatted files without changing them")
    parser.add_argument("--clang-format", help="path to the clang-format executable")
    args = parser.parse_args()

    executable = find_clang_format(args.clang_format)
    check_version(executable)

    files = sorted(
        str(path) for directory in SOURCE_DIRS for path in (ROOT / directory).rglob("*") if path.suffix in EXTENSIONS
    )
    if not files:
        return 0

    command = [executable, "--style=file"]
    command += ["--dry-run", "--Werror"] if args.check else ["-i"]
    return subprocess.run(command + files, cwd=ROOT).returncode


if __name__ == "__main__":
    sys.exit(main())
