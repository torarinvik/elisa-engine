#!/usr/bin/env python3
"""Synthesize the character course's sound files.

Every sound is generated here from sine tones and seeded noise, so the course
ships no third-party audio. The output is deterministic: rerunning the script
rewrites byte-identical files, and ``--check`` fails if a committed file drifted.
"""

from __future__ import annotations

import argparse
import io
import math
import sys
import wave
from pathlib import Path

RATE = 22050
SOUNDS = Path(__file__).resolve().parent / "sounds"


def envelope(index: int, count: int, attack: int, release: int) -> float:
    if index < attack:
        return index / attack
    if index >= count - release:
        return max(0.0, (count - index) / release)
    return 1.0


def tone(frequencies: list[float], seconds: float, volume: float,
        attack: float = 0.005, release: float = 0.05, glide: float = 0.0) -> list[float]:
    count = int(RATE * seconds)
    samples = []
    phase = [0.0] * len(frequencies)
    for index in range(count):
        progress = index / count
        value = 0.0
        for voice, frequency in enumerate(frequencies):
            phase[voice] += 2.0 * math.pi * frequency * (1.0 + glide * progress) / RATE
            value += math.sin(phase[voice])
        gain = envelope(index, count, int(RATE * attack), int(RATE * release))
        samples.append(volume * gain * value / len(frequencies))
    return samples


def noise(seconds: float, volume: float, smoothing: float, seed: int) -> list[float]:
    state = seed
    level = 0.0
    samples = []
    for _ in range(int(RATE * seconds)):
        state = (state * 1103515245 + 12345) & 0x7FFFFFFF
        level += smoothing * ((state / 0x3FFFFFFF - 1.0) - level)
        samples.append(volume * level)
    return samples


def seamless(samples: list[float], overlap_seconds: float) -> list[float]:
    """Crossfade the tail into the head so a looped voice has no click."""
    overlap = int(RATE * overlap_seconds)
    body = samples[:len(samples) - overlap]
    for index in range(overlap):
        blend = index / overlap
        body[index] = body[index] * blend + samples[len(samples) - overlap + index] * (1.0 - blend)
    return body


def music() -> list[float]:
    # A slow four-chord arpeggio: 8 bars of quarter notes at 90 BPM (21 s).
    chords = [[220.0, 261.63, 329.63], [174.61, 220.0, 261.63],
        [196.0, 246.94, 293.66], [164.81, 207.65, 246.94]]
    beat = 60.0 / 90.0
    samples: list[float] = []
    for bar in range(8):
        chord = chords[bar % len(chords)]
        for step in range(4):
            note = chord[step % 3] * (2.0 if step == 3 else 1.0)
            samples += tone([note, chord[0] / 2.0], beat, 0.22, attack=0.01, release=0.3)
    return samples


def wav_bytes(samples: list[float]) -> bytes:
    buffer = io.BytesIO()
    with wave.open(buffer, "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(RATE)
        output.writeframes(b"".join(
            max(-32768, min(32767, round(sample * 32767))).to_bytes(2, "little", signed=True)
            for sample in samples))
    return buffer.getvalue()


def sounds() -> dict[str, list[float]]:
    return {
        "music.wav": music(),
        "ambience.wav": seamless(noise(4.5, 0.35, 0.02, 7), 0.5),
        "jump_a.wav": tone([440.0], 0.14, 0.45, glide=0.6),
        "jump_b.wav": tone([494.0], 0.14, 0.45, glide=0.6),
        "land.wav": [a + b for a, b in zip(tone([90.0], 0.12, 0.6, release=0.1),
            noise(0.12, 0.3, 0.3, 11))],
        "win.wav": tone([523.25], 0.12, 0.4) + tone([659.25], 0.12, 0.4) + tone([783.99], 0.4, 0.4, release=0.3),
        "fall.wav": tone([392.0], 0.5, 0.4, release=0.2, glide=-0.5),
        "save.wav": tone([880.0, 1318.5], 0.16, 0.35, release=0.1),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="fail if a committed file differs")
    arguments = parser.parse_args()
    SOUNDS.mkdir(exist_ok=True)
    drifted = []
    for name, samples in sounds().items():
        data = wav_bytes(samples)
        path = SOUNDS / name
        if arguments.check:
            if not path.is_file() or path.read_bytes() != data:
                drifted.append(name)
        else:
            path.write_bytes(data)
    if drifted:
        print("Regenerate with make_sounds.py: " + ", ".join(drifted), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
