#!/usr/bin/env python3
"""Writes the factory's light profiles and cookies (ADR-0057): IES files and grey PNG images, authored here
from simple formulas, so they carry no outside licence.

    python scripts/make_light_profiles.py

Profiles (assets/Factory/Lights/*.ies, IES LM-63, type C, candela by angle from the lamp's axis):
    downlight.ies   a ceiling lamp under a shade: bright below, dark past 75 degrees
    narrow.ies      a narrow reflector: a tight beam with a soft halo
    bare_bulb.ies   a bare bulb: even, but shadowed by its socket above
Cookies (assets/Factory/Lights/*.png, white lets light through):
    fan_cookie.png   four blades of a fan in front of a light
    cage_cookie.png  the wire cage of a work lamp
"""

import math
import struct
import zlib
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "assets" / "Factory" / "Lights"


def write_ies(name, description, candela_of, last_angle=180.0, step=2.5):
    angles = [a * step for a in range(int(last_angle / step) + 1)]
    values = [max(candela_of(a), 0.0) for a in angles]
    lines = [
        "IESNA:LM-63-2002",
        "[TEST] Daidalos Engine Next, authored from a formula",
        f"[LUMCAT] {name}",
        f"[LUMINAIRE] {description}",
        "TILT=NONE",
        # Lamps, lumens per lamp (-1: absolute photometry), multiplier, vertical angles, horizontal angles,
        # photometric type C, units (2: meters), width, length, height.
        f"1 -1 1 {len(angles)} 1 1 2 0 0 0",
        # Ballast factor, future use, input watts.
        "1 1 0",
        " ".join(f"{a:g}" for a in angles),
        "0",
        " ".join(f"{v:.2f}" for v in values),
    ]
    (OUT / f"{name}.ies").write_text("\n".join(lines) + "\n", encoding="ascii")


def write_png(name, size, value_of):
    rows = []
    for y in range(size):
        row = bytearray([0])  # No filter.
        for x in range(size):
            u = (x + 0.5) / size * 2.0 - 1.0
            v = (y + 0.5) / size * 2.0 - 1.0
            row.append(max(0, min(255, int(round(value_of(u, v) * 255.0)))))
        rows.append(bytes(row))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", size, size, 8, 0, 0, 0, 0)  # 8-bit greyscale.
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b"")
    (OUT / f"{name}.png").write_bytes(png)


def smooth(edge0, edge1, x):
    t = max(0.0, min(1.0, (x - edge0) / (edge1 - edge0)))
    return t * t * (3.0 - 2.0 * t)


def fan(u, v):
    r = math.hypot(u, v)
    angle = math.atan2(v, u)
    # Four blades, each a wedge that widens outwards, and a hub.
    blade = 0.5 + 0.5 * math.cos(4.0 * angle + 0.6 * r)
    shadow = smooth(0.55, 0.75, blade) * smooth(0.12, 0.9, r)
    hub = smooth(0.1, 0.14, r)
    return max(0.0, 1.0 - shadow) * hub * (1.0 - smooth(0.92, 1.0, r) * 0.6)


def cage(u, v):
    # Wires every quarter, soft: a grid, and a ring.
    def wire(x, spacing, width):
        d = abs((x / spacing) - round(x / spacing)) * spacing
        return smooth(width * 0.5, width, d)

    r = math.hypot(u, v)
    ring = smooth(0.02, 0.04, abs(r - 0.7))
    return wire(u, 0.5, 0.06) * wire(v, 0.5, 0.06) * ring


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    write_ies("downlight", "Ceiling lamp under a shade", lambda a: 1000.0 * math.cos(math.radians(a)) ** 1.5 * (1.0 - smooth(60.0, 78.0, a)), 90.0)
    write_ies("narrow", "Narrow reflector", lambda a: 4000.0 * math.cos(math.radians(min(a, 89.0))) ** 40 + 120.0 * (1.0 - smooth(20.0, 45.0, a)), 90.0)
    write_ies("bare_bulb", "Bare bulb under its socket", lambda a: 300.0 * (1.0 - 0.85 * smooth(130.0, 170.0, a)))
    write_png("fan_cookie", 128, fan)
    write_png("cage_cookie", 128, cage)
    print(f"Wrote profiles and cookies to {OUT}")


if __name__ == "__main__":
    main()
