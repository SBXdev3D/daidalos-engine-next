#!/usr/bin/env python3
"""Synthesizes the factory's footsteps and impacts per surface (ADR-0067).

    python scripts/make_surface_sounds.py

Writes assets/Factory/Sounds/Surfaces/step_<surface>_<n>.wav and impact_<surface>_<n>.wav, 48 kHz mono
16-bit, a few variations each. Every sound is made here from noise, filters and decaying partials, with a
fixed seed: they are ours, under the project's licence, and the same on every run.

What each surface sounds like, roughly:
    concrete  a dull thump and a short gritty scrape
    metal     a clang: inharmonic partials that ring
    wood      a hollow knock, a little resonance
    tile      a hard click over a thump
    water     a splash: filtered noise and a few bubbles
    glass     high partials that ring briefly (impacts only)
    flesh     a wet, low thud (impacts only)
"""

import math
import random
import struct
import wave
from pathlib import Path

RATE = 48000
OUT = Path(__file__).resolve().parent.parent / "assets" / "Factory" / "Sounds" / "Surfaces"


def silence(seconds):
    return [0.0] * int(seconds * RATE)


def noise(rng, seconds):
    return [rng.uniform(-1.0, 1.0) for _ in range(int(seconds * RATE))]


def envelope(samples, attack, decay):
    """An attack in seconds, then an exponential decay with that time constant."""
    out = []
    for i, value in enumerate(samples):
        t = i / RATE
        gain = t / attack if t < attack else math.exp(-(t - attack) / decay)
        out.append(value * gain)
    return out


def lowpass(samples, cutoff):
    """One pole, run twice: 12 dB per octave."""
    alpha = 1.0 - math.exp(-2.0 * math.pi * cutoff / RATE)
    for _ in range(2):
        state = 0.0
        filtered = []
        for value in samples:
            state += alpha * (value - state)
            filtered.append(state)
        samples = filtered
    return samples


def highpass(samples, cutoff):
    low = lowpass(samples, cutoff)
    return [a - b for a, b in zip(samples, low)]


def bandpass(samples, low, high):
    return highpass(lowpass(samples, high), low)


def partials(rng, seconds, frequencies, decays, amplitudes):
    count = int(seconds * RATE)
    out = [0.0] * count
    for frequency, decay, amplitude in zip(frequencies, decays, amplitudes):
        phase = rng.uniform(0.0, 2.0 * math.pi)
        for i in range(count):
            t = i / RATE
            out[i] += amplitude * math.exp(-t / decay) * math.sin(2.0 * math.pi * frequency * t + phase)
    return out


def thump(rng, frequency, decay, seconds):
    """A low sine that drops in pitch as it dies, as a heel on a floor."""
    count = int(seconds * RATE)
    out = []
    phase = 0.0
    for i in range(count):
        t = i / RATE
        f = frequency * (1.0 + 0.6 * math.exp(-t / 0.01))
        phase += 2.0 * math.pi * f / RATE
        out.append(math.sin(phase) * math.exp(-t / decay))
    return out


def mix(*parts):
    length = max(len(part) for part, _ in parts)
    out = [0.0] * length
    for part, gain in parts:
        for i, value in enumerate(part):
            out[i] += value * gain
    return out


def normalize(samples, peak):
    top = max(abs(value) for value in samples) or 1.0
    return [value * peak / top for value in samples]


def step(surface, rng):
    if surface == "concrete":
        return mix((thump(rng, rng.uniform(70, 95), 0.03, 0.25), 0.8),
                   (envelope(bandpass(noise(rng, 0.25), 300, 2500), 0.003, 0.04), 0.5))
    if surface == "metal":
        base = rng.uniform(380, 520)
        ring = partials(rng, 0.6, [base, base * 2.32, base * 3.87, base * 5.4], [0.12, 0.08, 0.05, 0.03],
                        [0.5, 0.35, 0.2, 0.12])
        return mix((thump(rng, 90, 0.025, 0.6), 0.6), (envelope(highpass(noise(rng, 0.6), 1500), 0.001, 0.02), 0.3),
                   (ring, 0.4))
    if surface == "wood":
        base = rng.uniform(150, 210)
        body = partials(rng, 0.35, [base, base * 2.7], [0.06, 0.03], [0.6, 0.3])
        return mix((thump(rng, base * 0.6, 0.04, 0.35), 0.6), (body, 0.5),
                   (envelope(bandpass(noise(rng, 0.35), 400, 1800), 0.002, 0.03), 0.3))
    if surface == "tile":
        return mix((thump(rng, 85, 0.025, 0.25), 0.6),
                   (envelope(highpass(noise(rng, 0.25), 2500), 0.0005, 0.008), 0.6),
                   (envelope(bandpass(noise(rng, 0.25), 600, 3000), 0.002, 0.03), 0.3))
    if surface == "water":
        splash = envelope(bandpass(noise(rng, 0.5), 400, 4000), 0.01, 0.09)
        bubbles = []
        for _ in range(rng.randint(2, 4)):
            start = rng.uniform(0.02, 0.2)
            frequency = rng.uniform(600, 1400)
            bubble = silence(start)
            for i in range(int(0.05 * RATE)):
                t = i / RATE
                bubble.append(math.sin(2 * math.pi * frequency * (1 + 3 * t) * t) * math.exp(-t / 0.015))
            bubbles.append((bubble, 0.25))
        return mix((thump(rng, 70, 0.03, 0.5), 0.4), (splash, 0.8), *bubbles)
    raise ValueError(surface)


def impact(surface, rng):
    if surface == "concrete":
        return mix((envelope(bandpass(noise(rng, 0.4), 500, 6000), 0.0005, 0.03), 1.0),
                   (thump(rng, 120, 0.02, 0.4), 0.4),
                   (envelope(bandpass(noise(rng, 0.4), 1500, 5000), 0.02, 0.08), 0.2))  # Debris.
    if surface == "metal":
        base = rng.uniform(700, 1100)
        ring = partials(rng, 1.0, [base, base * 2.76, base * 5.4, base * 8.93], [0.3, 0.2, 0.12, 0.06],
                        [0.6, 0.4, 0.25, 0.15])
        return mix((envelope(highpass(noise(rng, 1.0), 2000), 0.0003, 0.01), 0.7), (ring, 0.6))
    if surface == "wood":
        base = rng.uniform(220, 320)
        return mix((envelope(bandpass(noise(rng, 0.4), 300, 3000), 0.0005, 0.02), 0.8),
                   (partials(rng, 0.4, [base, base * 2.4], [0.05, 0.03], [0.5, 0.3]), 0.5))
    if surface == "tile":
        return mix((envelope(highpass(noise(rng, 0.4), 2000), 0.0003, 0.015), 0.9),
                   (envelope(bandpass(noise(rng, 0.4), 2000, 8000), 0.03, 0.06), 0.3))
    if surface == "glass":
        frequencies = [rng.uniform(2200, 2600), rng.uniform(3500, 4200), rng.uniform(5200, 6400), rng.uniform(7400, 8800)]
        tinkle = partials(rng, 0.8, frequencies, [0.12, 0.09, 0.06, 0.04], [0.5, 0.4, 0.3, 0.2])
        return mix((envelope(highpass(noise(rng, 0.8), 3000), 0.0003, 0.01), 0.7), (tinkle, 0.5))
    if surface == "flesh":
        return mix((thump(rng, 60, 0.04, 0.35), 0.9),
                   (envelope(lowpass(noise(rng, 0.35), 700), 0.003, 0.05), 0.6))
    raise ValueError(surface)


def write(path, samples):
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as file:
        file.setnchannels(1)
        file.setsampwidth(2)
        file.setframerate(RATE)
        file.writeframes(b"".join(struct.pack("<h", int(max(-1.0, min(1.0, value)) * 32767)) for value in samples))


def main():
    rng = random.Random(2026)
    for surface in ("concrete", "metal", "wood", "tile", "water"):
        for n in range(1, 5):
            write(OUT / f"step_{surface}_{n}.wav", normalize(step(surface, rng), 0.5))
    for surface in ("concrete", "metal", "wood", "tile", "glass", "flesh"):
        for n in range(1, 4):
            write(OUT / f"impact_{surface}_{n}.wav", normalize(impact(surface, rng), 0.8))
    print(f"Wrote {len(list(OUT.glob('*.wav')))} sounds to {OUT}")


if __name__ == "__main__":
    main()
