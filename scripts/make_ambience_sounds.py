#!/usr/bin/env python3
# Daidalos Engine Next
# Copyright (C) 2011-2026 Stéphane Baudoux
# SPDX-License-Identifier: MIT OR Apache-2.0
"""Synthesizes the effects gallery's room sounds (ADR-0122).

    python scripts/make_ambience_sounds.py

Writes assets/Factory/Sounds/Ambience/<name>.wav, 48 kHz mono 16-bit. The loops (fire, rain, wind, hum,
steam, water, cryo, flies) end where they begin, so that a level's sound entity plays them without a seam;
explosion and zap are single shots for sequence cues. Every sound is made here from noise, filters and
partials, with a fixed seed: they are ours, under the project's licence, and the same on every run.
"""

import math
import random
import struct
import wave
from pathlib import Path

RATE = 48000
OUT = Path(__file__).resolve().parent.parent / "assets" / "Factory" / "Sounds" / "Ambience"


def noise(rng, count):
    return [rng.uniform(-1.0, 1.0) for _ in range(count)]


def lowpass(samples, cutoff, passes=2):
    """One pole, run `passes` times: 6 dB per octave each."""
    alpha = 1.0 - math.exp(-2.0 * math.pi * cutoff / RATE)
    for _ in range(passes):
        state = 0.0
        out = []
        for value in samples:
            state += alpha * (value - state)
            out.append(state)
        samples = out
    return samples


def highpass(samples, cutoff, passes=1):
    for _ in range(passes):
        low = lowpass(samples, cutoff, 1)
        samples = [a - b for a, b in zip(samples, low)]
    return samples


def band(samples, low, high):
    return highpass(lowpass(samples, high), low)


def resonator(samples, frequency, q):
    """A two-pole band pass, ringing at a frequency (which may be a list, one per sample)."""
    out = []
    y1 = y2 = 0.0
    for index, value in enumerate(samples):
        f = frequency[index] if isinstance(frequency, list) else frequency
        w = 2.0 * math.pi * f / RATE
        r = math.exp(-w / (2.0 * q))
        y = (1.0 - r) * value + 2.0 * r * math.cos(w) * y1 - r * r * y2
        y2, y1 = y1, y
        out.append(y)
    return out


def slow_wave(rng, count, period, depth):
    """A smooth random level around 1, changing over about `period` seconds: gusts, swells."""
    points = max(int(count / (period * RATE)) + 2, 2)
    keys = [1.0 + rng.uniform(-depth, depth) for _ in range(points)]
    keys[-1] = keys[0]  # Loops.
    out = []
    span = count / (points - 1)
    for index in range(count):
        t = index / span
        i = min(int(t), points - 2)
        f = t - i
        f = f * f * (3.0 - 2.0 * f)
        out.append(keys[i] + (keys[i + 1] - keys[i]) * f)
    return out


def add(target, samples, at, gain=1.0):
    for index, value in enumerate(samples):
        position = (at + index) % len(target)  # Wraps around: events near the end ring into the start.
        target[position] += value * gain


def click(rng, seconds, low, high, decay):
    """A short crack: band-passed noise with a fast decay."""
    count = int(seconds * RATE)
    body = band(noise(rng, count), low, high)
    return [value * math.exp(-index / (decay * RATE)) for index, value in enumerate(body)]


def normalize(samples, peak=0.7):
    top = max(abs(value) for value in samples) or 1.0
    return [value * peak / top for value in samples]


def seamless(samples, fade_seconds=0.5):
    """Crossfades the end into the start, so that the loop has no seam."""
    fade = int(fade_seconds * RATE)
    head, body, tail = samples[:fade], samples[fade:-fade], samples[-fade:]
    mixed = [t * math.cos(0.5 * math.pi * i / fade) + h * math.sin(0.5 * math.pi * i / fade)
             for i, (h, t) in enumerate(zip(head, tail))]
    return mixed + body


def write(name, samples):
    OUT.mkdir(parents=True, exist_ok=True)
    with wave.open(str(OUT / f"{name}.wav"), "wb") as file:
        file.setnchannels(1)
        file.setsampwidth(2)
        file.setframerate(RATE)
        file.writeframes(b"".join(struct.pack("<h", int(max(-1.0, min(1.0, v)) * 32767)) for v in samples))
    print(f"{name}.wav: {len(samples) / RATE:.1f} s")


def fire(rng, seconds):
    count = int(seconds * RATE)
    roar = [v * s for v, s in zip(lowpass(noise(rng, count), 250), slow_wave(rng, count, 0.7, 0.5))]
    hiss = [v * 0.15 for v in band(noise(rng, count), 2000, 6000)]
    out = [r * 3.0 + h for r, h in zip(roar, hiss)]
    for _ in range(int(seconds * 14)):
        add(out, click(rng, 0.03, 1500, 7000, rng.uniform(0.002, 0.008)), rng.randrange(count), rng.uniform(0.3, 1.2))
    for _ in range(int(seconds * 1.5)):  # A pop of wood now and then.
        add(out, click(rng, 0.08, 400, 3000, 0.015), rng.randrange(count), rng.uniform(1.0, 2.0))
    return out


def rain(rng, seconds):
    count = int(seconds * RATE)
    wash = band(noise(rng, count), 500, 7000)
    out = [v * s for v, s in zip(wash, slow_wave(rng, count, 3.0, 0.15))]
    for _ in range(int(seconds * 120)):
        add(out, click(rng, 0.012, 2000, 9000, 0.002), rng.randrange(count), rng.uniform(0.1, 0.5))
    rumble = lowpass(noise(rng, count), 60, 3)
    swell = slow_wave(rng, count, 4.0, 0.9)
    return [o + r * 12.0 * max(s - 0.6, 0.0) for o, r, s in zip(out, rumble, swell)]


def wind(rng, seconds):
    count = int(seconds * RATE)
    gusts = slow_wave(rng, count, 1.5, 0.7)
    body = [v * g for v, g in zip(lowpass(noise(rng, count), 500), gusts)]
    howl = resonator(noise(rng, count), [400.0 + 350.0 * g for g in gusts], 8.0)
    return [b * 2.0 + h * 0.6 * g for b, h, g in zip(body, howl, gusts)]


def hum(rng, seconds):
    count = int(seconds * RATE)
    out = []
    for index in range(count):
        t = index / RATE
        out.append(0.5 * math.sin(2 * math.pi * 50 * t) + 0.3 * math.sin(2 * math.pi * 100 * t) +
                   0.25 * math.sin(2 * math.pi * 150 * t) + 0.1 * math.sin(2 * math.pi * 250 * t))
    buzz = [v * 0.08 for v in band(noise(rng, count), 1000, 5000)]
    out = [o + b for o, b in zip(out, buzz)]
    for _ in range(int(seconds * 3)):  # Crackles of an arc.
        at = rng.randrange(count)
        for _ in range(rng.randint(3, 9)):
            add(out, click(rng, 0.02, 1500, 9000, 0.004), at, rng.uniform(1.0, 2.5))
            at += rng.randint(200, 2500)
    return out


def steam(rng, seconds):
    count = int(seconds * RATE)
    hiss = [v * s for v, s in zip(band(noise(rng, count), 1500, 9000), slow_wave(rng, count, 1.2, 0.4))]
    rumble = lowpass(noise(rng, count), 120)
    return [h + r * 2.0 for h, r in zip(hiss, rumble)]


def water(rng, seconds):
    count = int(seconds * RATE)
    spray = [v * s for v, s in zip(band(noise(rng, count), 700, 5000), slow_wave(rng, count, 0.4, 0.3))]
    out = [v * 0.8 for v in spray]
    for _ in range(int(seconds * 4)):  # Drops and bubbles: a short rising chirp.
        length = int(rng.uniform(0.02, 0.05) * RATE)
        start = rng.uniform(500, 1200)
        chirp, phase = [], 0.0
        for index in range(length):
            frequency = start * (1.0 + 1.5 * index / length)
            phase += 2 * math.pi * frequency / RATE
            chirp.append(math.sin(phase) * math.exp(-index / (0.012 * RATE)))
        add(out, chirp, rng.randrange(count), rng.uniform(0.3, 0.8))
    return out


def cryo(rng, seconds):
    count = int(seconds * RATE)
    hiss = [v * s for v, s in zip(highpass(noise(rng, count), 3000, 2), slow_wave(rng, count, 0.5, 0.25))]
    boil = lowpass(noise(rng, count), 300)
    out = [h * 0.8 + b * 1.5 for h, b in zip(hiss, boil)]
    for _ in range(int(seconds * 6)):  # Ice ticking.
        add(out, click(rng, 0.01, 4000, 12000, 0.001), rng.randrange(count), rng.uniform(0.2, 0.6))
    return out


def flies(rng, seconds):
    count = int(seconds * RATE)
    out = [0.0] * count
    for _ in range(5):
        base = rng.uniform(170, 240)
        wander = slow_wave(rng, count, 0.6, 0.08)
        level = slow_wave(rng, count, 0.8, 0.9)
        phase = 0.0
        voice = []
        for index in range(count):
            phase = (phase + base * wander[index] / RATE) % 1.0
            voice.append((2.0 * phase - 1.0) * max(level[index], 0.0))
        for index, value in enumerate(lowpass(voice, 1800, 1)):
            out[index] += value
    return out


def explosion(rng):
    count = int(4.0 * RATE)
    boom = lowpass(noise(rng, count), 180, 3)
    boom = [v * math.exp(-i / (0.9 * RATE)) for i, v in enumerate(boom)]
    crack = click(rng, 0.25, 300, 6000, 0.05)
    out = [b * 6.0 for b in boom]
    add(out, crack, 0, 1.0)
    for _ in range(40):  # Debris falling back.
        add(out, click(rng, 0.03, 800, 5000, 0.006), int(rng.uniform(0.4, 2.5) * RATE), rng.uniform(0.05, 0.25))
    return out


def zap(rng):
    count = int(0.6 * RATE)
    out = [0.0] * count
    at = 0
    while at < count - 2000:
        add(out, click(rng, 0.03, 1200, 10000, 0.006), at, rng.uniform(0.5, 1.0))
        at += rng.randint(300, 2200)
    tone = [math.sin(2 * math.pi * 100 * i / RATE) * math.exp(-i / (0.2 * RATE)) for i in range(count)]
    return [o + t * 0.3 for o, t in zip(out, tone)]


def main():
    loops = {"fire": fire, "rain": rain, "wind": wind, "hum": hum, "steam": steam, "water": water, "cryo": cryo,
             "flies": flies}
    for index, (name, make) in enumerate(loops.items()):
        rng = random.Random(1220 + index)
        write(name, normalize(seamless(make(rng, 8.5)), 0.6))
    write("explosion", normalize(explosion(random.Random(1230)), 0.9))
    write("zap", normalize(zap(random.Random(1231)), 0.8))


if __name__ == "__main__":
    main()
