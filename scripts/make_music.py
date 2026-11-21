#!/usr/bin/env python3
"""Synthesizes the factory's layered score, its stingers and the hum of tired lamps (ADR-0068).

    python scripts/make_music.py

Writes into assets/Factory/Sounds/Music/, mono 16-bit at 24 kHz:
    tension_0.wav .. tension_3.wav   four 16-second loops that play together, faded in as tension rises:
                                     a low drone, a heartbeat, a dissonant cluster, a high tremolo
    stinger_1.wav, stinger_2.wav     a boom, a burst and a shriek, for a scare
    hum.wav                          the buzz of a fluorescent tube, a 4-second loop

Everything is made here with a fixed seed: ours, under the project's licence, and the same on every run.
Every frequency of a loop is a whole number of cycles over its length, so that it loops without a click.
"""

import math
import random
import struct
import wave
from pathlib import Path

RATE = 24000
OUT = Path(__file__).resolve().parent.parent / "assets" / "Factory" / "Sounds" / "Music"


def looped(frequency, seconds):
    """The nearest frequency that fits a whole number of cycles in the loop."""
    return round(frequency * seconds) / seconds


def lowpass(samples, cutoff):
    """A one-pole low-pass over a loop: it starts from the state its end leaves, so that the end flows into the
    start. Starting from a raw sample instead left the drone's wind a jump of 60% of full scale at the seam, a
    click every 16 seconds."""
    alpha = 1.0 - math.exp(-2.0 * math.pi * cutoff / RATE)
    state = 0.0
    for value in samples:
        state += alpha * (value - state)
    out = []
    for value in samples:
        state += alpha * (value - state)
        out.append(state)
    return out


def normalize(samples, peak):
    top = max(abs(value) for value in samples) or 1.0
    return [value * peak / top for value in samples]


def write(path, samples):
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as file:
        file.setnchannels(1)
        file.setsampwidth(2)
        file.setframerate(RATE)
        file.writeframes(b"".join(struct.pack("<h", int(max(-1.0, min(1.0, v)) * 32767)) for v in samples))


def drone(seconds, rng):
    """D2, A2, D3 and F3, a dark open chord with soft overtones, breathing slowly, over a faint wind and a D1
    beneath it. Pitched where ordinary speakers play: an octave lower (D1 and A1 alone, under wind three times
    as loud) the notes went unheard and the layer sounded like noise."""
    count = int(seconds * RATE)
    sub = looped(36.71, seconds)
    # Each note, its level, and a twin a little sharp that beats slowly against it.
    notes = [(looped(f, seconds), level, looped(f + beat, seconds))
             for f, level, beat in ((73.42, 1.0, 0.25), (110.0, 0.7, 0.19), (146.83, 0.45, 0.31), (174.61, 0.3, 0.13))]
    breath = looped(1.0 / 8.0, seconds)
    brightness = looped(1.0 / 16.0, seconds)
    wind = lowpass([rng.uniform(-1, 1) for _ in range(count)], 180.0)
    out = []
    for i in range(count):
        t = i / RATE
        swell = 0.75 + 0.25 * math.sin(2 * math.pi * breath * t)
        # The overtones come and go once a loop, as if a filter opened and closed.
        bright = 0.5 + 0.5 * math.sin(2 * math.pi * brightness * t)
        tone = 0.0
        for frequency, level, twin in notes:
            for harmonic, gain in ((1, 1.0), (2, 0.25 + 0.2 * bright), (3, 0.1 + 0.15 * bright)):
                tone += level * gain * (math.sin(2 * math.pi * frequency * harmonic * t) +
                                        0.5 * math.sin(2 * math.pi * twin * harmonic * t))
        tone += 0.8 * math.sin(2 * math.pi * sub * t)
        out.append(tone * swell * 0.25 + wind[i])
    return out


def heartbeat(seconds):
    """Lub-dub at 60 beats a minute."""
    count = int(seconds * RATE)
    out = [0.0] * count
    for beat in range(int(seconds)):
        for offset, gain in ((0.0, 1.0), (0.28, 0.7)):
            start = int((beat + offset) * RATE)
            for i in range(int(0.25 * RATE)):
                if start + i >= count:
                    break
                t = i / RATE
                frequency = 48.0 * (1.0 + 0.8 * math.exp(-t / 0.02))
                out[start + i] += gain * math.sin(2 * math.pi * frequency * t) * math.exp(-t / 0.06)
    return out


def cluster(seconds):
    """D3, E-flat 3 and A-flat 3, detuned saws through a low-pass, with a slow tremolo."""
    count = int(seconds * RATE)
    voices = [looped(f, seconds) for f in (146.83, 147.6, 155.56, 207.65)]
    tremolo = looped(0.5, seconds)
    out = []
    for i in range(count):
        t = i / RATE
        value = 0.0
        for frequency in voices:
            phase = (frequency * t) % 1.0
            value += 2.0 * phase - 1.0
        out.append(value * (0.7 + 0.3 * math.sin(2 * math.pi * tremolo * t)))
    return lowpass(lowpass(out, 900.0), 900.0)


def screech(seconds):
    """D6 against E-flat 6 in a fast tremolo, swelling twice a loop."""
    count = int(seconds * RATE)
    high = looped(1174.66, seconds)
    rub = looped(1244.51, seconds)
    tremolo = looped(7.0, seconds)
    swell = looped(2.0 / seconds, seconds)
    out = []
    for i in range(count):
        t = i / RATE
        level = 0.5 - 0.5 * math.cos(2 * math.pi * swell * t)
        flutter = 0.6 + 0.4 * math.sin(2 * math.pi * tremolo * t)
        out.append((math.sin(2 * math.pi * high * t) + 0.8 * math.sin(2 * math.pi * rub * t)) * level * flutter)
    return out


def stinger(rng, seconds):
    count = int(seconds * RATE)
    out = []
    shriek = [rng.uniform(1500, 2600) for _ in range(5)]
    for i in range(count):
        t = i / RATE
        boom = math.sin(2 * math.pi * 38.0 * (1 + 1.5 * math.exp(-t / 0.05)) * t) * math.exp(-t / 0.6)
        burst = rng.uniform(-1, 1) * math.exp(-t / 0.08)
        cry = sum(math.sin(2 * math.pi * f * (1 + 0.02 * t) * t) for f in shriek) / len(shriek)
        cry *= min(t / 0.05, 1.0) * math.exp(-t / 0.9)
        out.append(boom * 1.0 + burst * 0.5 + cry * 0.6)
    return out


def hum(seconds, rng):
    """A tube's 100 Hz buzz, its odd harmonics strongest, with a little crackle."""
    count = int(seconds * RATE)
    out = []
    for i in range(count):
        t = i / RATE
        value = 0.0
        for harmonic, gain in ((1, 1.0), (2, 0.4), (3, 0.6), (4, 0.15), (5, 0.35), (7, 0.2)):
            value += gain * math.sin(2 * math.pi * 100.0 * harmonic * t)
        crackle = rng.uniform(-1, 1) if rng.random() < 0.002 else 0.0
        out.append(value * 0.3 + crackle)
    return out


def main():
    rng = random.Random(1311)
    loop = 16.0
    layers = [drone(loop, rng), heartbeat(loop), cluster(loop), screech(loop)]
    for index, layer in enumerate(layers):
        write(OUT / f"tension_{index}.wav", normalize(layer, 0.6))
    for index in (1, 2):
        write(OUT / f"stinger_{index}.wav", normalize(stinger(rng, 2.5), 0.9))
    write(OUT / "hum.wav", normalize(hum(4.0, rng), 0.5))
    print(f"Wrote {len(list(OUT.glob('*.wav')))} files to {OUT}")


if __name__ == "__main__":
    main()
