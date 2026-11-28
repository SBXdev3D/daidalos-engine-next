#!/usr/bin/env python3
"""Synthesizes the sounds of loose objects hitting, rolling and scraping, per surface (ADR-0125).

    python scripts/make_physics_sounds.py

Writes assets/Factory/Sounds/Physics/hit_<surface>_<light|heavy>_<n>.wav, roll_<surface>.wav and
scrape_<surface>.wav, 48 kHz mono 16-bit. Hits are a few variations each; rolls and scrapes are seamless loops
whose volume the engine follows from the speed. Made here from noise, filters and decaying partials with a fixed
seed, as the footsteps are (scripts/make_surface_sounds.py): ours, under the project's licence, and the same on
every run.

What each surface sounds like, roughly:
    metal     light: a clank that rings; heavy: a hollow boom, as a drum or a gas bottle
    wood      light: a dry knock; heavy: a crate landing, a low thump and a hollow body
    plastic   light: a hollow tock; heavy: a dull, lower tock
    cloth     a sack landing: a dead thud and a little sand, no ring
    glass     a clink, higher and shorter when light
    rubber    a soft, low bump that dips in pitch
    concrete  a gritty thud, with bits of debris when heavy
    tile      a hard click over a thump
"""

import math
import random
from pathlib import Path

from make_surface_sounds import (RATE, bandpass, envelope, highpass, lowpass, mix, noise, normalize, partials,
                                 silence, thump, write)

OUT = Path(__file__).resolve().parent.parent / "assets" / "Factory" / "Sounds" / "Physics"


def hit(surface, heavy, rng):
    if surface == "metal":
        if heavy:
            base = rng.uniform(95, 140)
            ring = partials(rng, 1.6, [base, base * 1.63, base * 2.71, base * 4.1, base * 5.9],
                            [0.55, 0.4, 0.28, 0.16, 0.1], [0.7, 0.45, 0.3, 0.2, 0.12])
            return mix((thump(rng, 55, 0.06, 1.6), 0.8), (ring, 0.7),
                       (envelope(bandpass(noise(rng, 1.6), 800, 5000), 0.0005, 0.02), 0.35))
        base = rng.uniform(420, 760)
        ring = partials(rng, 0.9, [base, base * 2.32, base * 3.87, base * 5.4], [0.22, 0.14, 0.08, 0.05],
                        [0.55, 0.4, 0.25, 0.15])
        return mix((envelope(highpass(noise(rng, 0.9), 1800), 0.0003, 0.008), 0.6), (ring, 0.6),
                   (thump(rng, 110, 0.02, 0.9), 0.35))
    if surface == "wood":
        if heavy:
            base = rng.uniform(95, 135)
            body = partials(rng, 0.7, [base, base * 2.1, base * 3.3], [0.09, 0.05, 0.03], [0.7, 0.35, 0.2])
            return mix((thump(rng, rng.uniform(55, 70), 0.07, 0.7), 0.9), (body, 0.5),
                       (envelope(bandpass(noise(rng, 0.7), 250, 2200), 0.001, 0.04), 0.45))
        base = rng.uniform(260, 380)
        return mix((envelope(bandpass(noise(rng, 0.35), 400, 3500), 0.0005, 0.015), 0.8),
                   (partials(rng, 0.35, [base, base * 2.6], [0.045, 0.025], [0.55, 0.3]), 0.5),
                   (thump(rng, 120, 0.02, 0.35), 0.3))
    if surface == "plastic":
        base = rng.uniform(260, 340) if heavy else rng.uniform(620, 880)
        decay = 0.05 if heavy else 0.03
        return mix((envelope(bandpass(noise(rng, 0.4), base * 0.8, base * 4.0), 0.0005, decay * 0.5), 0.7),
                   (partials(rng, 0.4, [base, base * 1.9], [decay, decay * 0.6], [0.6, 0.25]), 0.6),
                   (thump(rng, 90 if heavy else 150, 0.03, 0.4), 0.4 if heavy else 0.2))
    if surface == "cloth":
        length = 0.6 if heavy else 0.35
        sand = envelope(bandpass(noise(rng, length), 1500, 6000), 0.01, 0.08 if heavy else 0.04)
        return mix((thump(rng, rng.uniform(45, 60) if heavy else 75, 0.07 if heavy else 0.04, length), 1.0),
                   (envelope(lowpass(noise(rng, length), 500), 0.004, 0.06), 0.7), (sand, 0.15))
    if surface == "glass":
        scale = 0.7 if heavy else 1.0
        frequencies = [rng.uniform(2000, 2500) * scale, rng.uniform(3300, 4000) * scale,
                       rng.uniform(5000, 6200) * scale, rng.uniform(7200, 8600) * scale]
        tinkle = partials(rng, 0.9, frequencies, [0.15, 0.1, 0.07, 0.05], [0.5, 0.4, 0.3, 0.2])
        return mix((envelope(highpass(noise(rng, 0.9), 2500), 0.0003, 0.01), 0.6), (tinkle, 0.5),
                   (thump(rng, 130, 0.02, 0.9), 0.3 if heavy else 0.1))
    if surface == "rubber":
        frequency = rng.uniform(70, 90) if heavy else rng.uniform(130, 170)
        return mix((thump(rng, frequency, 0.05, 0.4), 1.0),
                   (envelope(lowpass(noise(rng, 0.4), 900), 0.002, 0.02), 0.3))
    if surface == "concrete":
        length = 0.8 if heavy else 0.4
        debris = []
        if heavy:
            for _ in range(rng.randint(5, 9)):
                at = rng.uniform(0.04, 0.5)
                debris.append((silence(at) + envelope(bandpass(noise(rng, 0.05), 1500, 6000), 0.0005, 0.008),
                               rng.uniform(0.1, 0.25)))
        return mix((thump(rng, 60 if heavy else 95, 0.05 if heavy else 0.025, length), 0.9),
                   (envelope(bandpass(noise(rng, length), 300, 4000), 0.0008, 0.04 if heavy else 0.02), 0.7),
                   *debris)
    if surface == "tile":
        return mix((thump(rng, 70 if heavy else 95, 0.035 if heavy else 0.02, 0.4), 0.6),
                   (envelope(highpass(noise(rng, 0.4), 2500), 0.0003, 0.012), 0.8))
    raise ValueError(surface)


def seamless(samples, seconds, fade):
    """The first seconds of samples, whose end fades into their start: a loop without a click."""
    count = int(seconds * RATE)
    overlap = int(fade * RATE)
    out = samples[:count]
    for i in range(overlap):
        share = i / overlap
        out[i] = samples[i] * share + samples[count + i] * (1.0 - share)
    return out


def wobble(rng, samples, rate, depth):
    """A slow, random swell, as a surface that is not quite even."""
    out = []
    level = 1.0
    target = 1.0
    every = int(RATE / rate)
    for i, value in enumerate(samples):
        if i % every == 0:
            target = 1.0 - depth * rng.random()
        level += (target - level) * 0.0005
        out.append(value * level)
    return out


def roll(surface, rng):
    seconds, fade = 4.0, 0.5
    total = seconds + fade
    rumble = wobble(rng, lowpass(noise(rng, total), 180), 6.0, 0.6)
    if surface == "metal":
        # A hollow drum rolling: the rumble excites its body, which rings faintly.
        base = rng.uniform(110, 150)
        ring = [value * 0.5 for value in bandpass(noise(rng, total), base * 0.9, base * 1.15)]
        grit = bandpass(noise(rng, total), 900, 3500)
        samples = mix((rumble, 1.0), (ring, 0.8), (wobble(rng, grit, 14.0, 0.9), 0.12))
    elif surface == "rubber":
        samples = mix((rumble, 1.0), (lowpass(noise(rng, total), 400), 0.2))
    elif surface == "glass":
        samples = mix((rumble, 0.6), (wobble(rng, bandpass(noise(rng, total), 2500, 6000), 20.0, 0.95), 0.25))
    else:  # Wood, plastic, concrete and the rest: a rumble and some grit.
        grit = bandpass(noise(rng, total), 400, 2500 if surface == "concrete" else 1800)
        samples = mix((rumble, 1.0), (wobble(rng, grit, 12.0, 0.9), 0.25 if surface == "concrete" else 0.15))
    return seamless(samples, seconds, fade)


def scrape(surface, rng):
    seconds, fade = 3.0, 0.4
    total = seconds + fade
    if surface == "metal":
        band = bandpass(noise(rng, total), 1500, 7000)
        squeal = partials(rng, total, [rng.uniform(2400, 3100)], [1000.0], [1.0])
        samples = mix((wobble(rng, band, 25.0, 0.85), 1.0), (wobble(rng, squeal, 3.0, 1.0), 0.05),
                      (lowpass(noise(rng, total), 300), 0.3))
    elif surface == "cloth":
        samples = mix((wobble(rng, bandpass(noise(rng, total), 300, 2500), 8.0, 0.6), 1.0))
    elif surface == "concrete":
        samples = mix((wobble(rng, bandpass(noise(rng, total), 500, 5000), 30.0, 0.9), 1.0),
                      (lowpass(noise(rng, total), 250), 0.4))
    elif surface == "plastic":
        samples = mix((wobble(rng, bandpass(noise(rng, total), 700, 4000), 18.0, 0.8), 1.0))
    else:  # Wood and the rest: a judder of the grain.
        samples = mix((wobble(rng, bandpass(noise(rng, total), 250, 2200), 22.0, 0.9), 1.0),
                      (lowpass(noise(rng, total), 200), 0.3))
    return seamless(samples, seconds, fade)


def snap(surface, rng):
    """A joint giving way: a chain link or a bolt pinging off, wood cracking, cloth tearing, plastic cracking."""
    if surface == "metal":
        base = rng.uniform(1800, 2400)
        ping = partials(rng, 0.8, [base, base * 2.1, base * 3.4], [0.25, 0.15, 0.08], [0.6, 0.35, 0.2])
        return mix((envelope(highpass(noise(rng, 0.8), 3000), 0.0002, 0.006), 0.9), (ping, 0.5),
                   (thump(rng, 160, 0.015, 0.8), 0.3))
    if surface == "cloth":
        tear = []
        for _ in range(rng.randint(10, 16)):
            at = rng.uniform(0.0, 0.25)
            tear.append((silence(at) + envelope(bandpass(noise(rng, 0.04), 1200, 6000), 0.001, 0.01),
                         rng.uniform(0.3, 0.7)))
        return mix(*tear)
    # Wood, plastic and the rest: a sharp crack and a splintering tail.
    crack = envelope(bandpass(noise(rng, 0.6), 800, 7000), 0.0002, 0.012)
    splinters = []
    for _ in range(rng.randint(6, 10)):
        at = rng.uniform(0.01, 0.2)
        splinters.append((silence(at) + envelope(bandpass(noise(rng, 0.03), 2000, 7000), 0.0005, 0.006),
                          rng.uniform(0.2, 0.5)))
    return mix((crack, 1.0), (thump(rng, 140 if surface == "wood" else 220, 0.02, 0.6), 0.4), *splinters)


def main():
    rng = random.Random(2125)
    OUT.mkdir(parents=True, exist_ok=True)
    for surface in ("metal", "wood", "plastic", "cloth", "glass", "rubber", "concrete", "tile"):
        for weight, heavy in (("light", False), ("heavy", True)):
            for n in range(1, 4):
                write(OUT / f"hit_{surface}_{weight}_{n}.wav", normalize(hit(surface, heavy, rng), 0.8 if heavy else 0.6))
    for surface in ("metal", "wood", "plastic", "rubber", "concrete", "glass"):
        write(OUT / f"roll_{surface}.wav", normalize(roll(surface, rng), 0.5))
    for surface in ("metal", "wood", "plastic", "concrete", "cloth"):
        write(OUT / f"scrape_{surface}.wav", normalize(scrape(surface, rng), 0.45))
    for surface in ("metal", "wood", "plastic", "cloth"):
        write(OUT / f"snap_{surface}.wav", normalize(snap(surface, rng), 0.85))
    print(f"Wrote {len(list(OUT.glob('*.wav')))} sounds to {OUT}")


if __name__ == "__main__":
    main()
