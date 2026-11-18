#!/usr/bin/env python3
"""Parses every C++ source of the project as Clang does on Linux, from Windows (ADR-0074).

    python scripts/check_linux_compile.py sysroot --zstd <zstd.exe> --out <dir>
    python scripts/check_linux_compile.py check --clang <clang++.exe> --sysroot <dir> [name ...]

`sysroot` downloads Ubuntu 24.04's C and C++ development headers (glibc, libstdc++ 14, the kernel's, X11,
Wayland, xkbcommon) from archive.ubuntu.com and unpacks their headers into a folder; their packages are
zstd-compressed, so it needs zstd's command-line tool. `check` runs `clang++ -fsyntax-only
--target=x86_64-linux-gnu` on each source the Windows build compiles, with that target's include folders and
defines read from the Visual Studio projects in build/windows-msvc (Windows-only defines left out) and the
headers of that sysroot, and lists the errors by file. Names given after `check` keep only the sources whose
path contains one of them.

It finds what MSVC accepts and Clang or libstdc++ do not (missing standard includes, name lookup, two-phase
templates). It does not link, and code under `#ifdef _WIN32`-like defines that CMake sets only on Windows is
checked as on Windows: those reports are to be read with the CMake file at hand.
"""

import argparse
import concurrent.futures
import gzip
import io
import pathlib
import re
import subprocess
import sys
import tarfile
import urllib.request
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parent.parent
BUILD = ROOT / "build" / "windows-msvc"
TRIPLET = "x64-windows-static-md"
NS = {"m": "http://schemas.microsoft.com/developer/msbuild/2003"}
WINDOWS_DEFINES = re.compile(
    r"^(WIN32|_WINDOWS|_WIN32|NDEBUG|_CRT_|NOMINMAX|WIN32_LEAN_AND_MEAN|UNICODE|_UNICODE|CMAKE_INTDIR|_MBCS|%)")
MIRROR = "http://archive.ubuntu.com/ubuntu/"
PACKAGES = ["libstdc++-14-dev", "libgcc-14-dev", "libc6-dev", "linux-libc-dev", "libx11-dev", "x11proto-dev",
            "libwayland-dev", "libxkbcommon-dev"]


def fetch(url):
    with urllib.request.urlopen(url, timeout=120) as response:
        return response.read()


def ar_members(data):
    if data[:8] != b"!<arch>\n":
        raise ValueError("not a .deb")
    offset = 8
    while offset < len(data):
        header = data[offset:offset + 60]
        size = int(header[48:58].decode().strip())
        yield header[:16].decode().strip().rstrip("/"), data[offset + 60:offset + 60 + size]
        offset += 60 + size + (size & 1)


def make_sysroot(zstd, out):
    files = {}
    for suite in ("noble", "noble-updates"):
        for component in ("main", "universe"):
            text = gzip.decompress(fetch(f"{MIRROR}dists/{suite}/{component}/binary-amd64/Packages.gz")).decode()
            for stanza in text.split("\n\n"):
                fields = dict(line.split(": ", 1) for line in stanza.splitlines()
                              if ": " in line and not line.startswith(" "))
                if "Package" in fields:
                    files[fields["Package"]] = fields["Filename"]
    out.mkdir(parents=True, exist_ok=True)
    for name in PACKAGES:
        print("Fetching", files[name])
        for member, payload in ar_members(fetch(MIRROR + files[name])):
            if not member.startswith("data.tar"):
                continue
            if member.endswith(".zst"):
                payload = subprocess.run([str(zstd), "-d", "-c"], input=payload, capture_output=True,
                                         check=True).stdout
            with tarfile.open(fileobj=io.BytesIO(payload), mode="r:*") as archive:
                for entry in archive.getmembers():
                    if entry.isfile() and "/include" in entry.name and entry.name.startswith("./usr/"):
                        target = out / entry.name[2:]
                        target.parent.mkdir(parents=True, exist_ok=True)
                        target.write_bytes(archive.extractfile(entry).read())
    print(f"{sum(1 for _ in out.rglob('*.h'))} headers in {out}")


def sources(clang_flags, only):
    jobs = {}
    for project in BUILD.rglob("*.vcxproj"):
        if "vcpkg_installed" in project.parts or "_deps" in project.parts:
            continue
        tree = ET.parse(project)
        group = None
        for definition in tree.getroot().iterfind("m:ItemDefinitionGroup", NS):
            if "Release|x64" in definition.get("Condition", ""):
                group = definition
        compile_node = group.find("m:ClCompile", NS) if group is not None else None
        if compile_node is None:
            continue
        flags = list(clang_flags)
        for include in (compile_node.findtext("m:AdditionalIncludeDirectories", "", NS) or "").split(";"):
            include = include.strip()
            if include and not include.startswith("%"):
                flags += ["-isystem" if "vcpkg_installed" in include else "-I", include.replace("\\", "/")]
        options = compile_node.findtext("m:AdditionalOptions", "", NS) or ""
        for external in re.findall(r'/external:I\s*"?([^"\s]+)"?', options):
            flags += ["-isystem", external.replace("\\", "/")]
        for define in (compile_node.findtext("m:PreprocessorDefinitions", "", NS) or "").split(";"):
            define = define.strip()
            if define and not WINDOWS_DEFINES.match(define):
                flags.append("-D" + define)
        for item in tree.getroot().iterfind("m:ItemGroup/m:ClCompile", NS):
            source = pathlib.Path(item.get("Include"))
            # Projects of modules since removed linger in the build folder.
            if source.suffix == ".c" or not source.exists():
                continue
            if only and not any(name in source.as_posix() for name in only):
                continue
            jobs.setdefault(source, flags)
    return jobs


def check(clang, sysroot, only):
    flags = ["-std=c++20", "--target=x86_64-linux-gnu", f"--sysroot={sysroot}", "-fsyntax-only", "-ferror-limit=8",
             "-Wno-everything", "-fno-ms-compatibility", "-fno-ms-extensions", "-DNDEBUG", "-nostdinc++",
             "-isystem", f"{sysroot}/usr/include/c++/14",
             "-isystem", f"{sysroot}/usr/include/x86_64-linux-gnu/c++/14",
             "-isystem", f"{sysroot}/usr/include/c++/14/backward",
             "-isystem", str(BUILD / "vcpkg_installed" / TRIPLET / "include")]
    jobs = sources(flags, only)
    print(f"{len(jobs)} sources")

    def run(job):
        source, command = job
        result = subprocess.run([str(clang), *command, str(source)], capture_output=True, text=True)
        return source, [line for line in result.stderr.splitlines() if "error:" in line]

    failed = 0
    with concurrent.futures.ThreadPoolExecutor(12) as pool:
        for source, errors in pool.map(run, jobs.items()):
            if errors:
                failed += 1
                print("==", source.relative_to(ROOT) if source.is_relative_to(ROOT) else source)
                for line in errors[:6]:
                    print("  ", line.replace(ROOT.as_posix() + "/", "")[:300])
    print(f"{failed} source(s) with errors")
    return 1 if failed else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    make = commands.add_parser("sysroot")
    make.add_argument("--zstd", type=pathlib.Path, required=True)
    make.add_argument("--out", type=pathlib.Path, required=True)
    run = commands.add_parser("check")
    run.add_argument("--clang", type=pathlib.Path, required=True)
    run.add_argument("--sysroot", type=pathlib.Path, required=True)
    run.add_argument("only", nargs="*")
    arguments = parser.parse_args()
    if arguments.command == "sysroot":
        make_sysroot(arguments.zstd, arguments.out)
        return 0
    return check(arguments.clang, arguments.sysroot, arguments.only)


if __name__ == "__main__":
    sys.exit(main())
