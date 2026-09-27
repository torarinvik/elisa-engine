#!/usr/bin/env python3
"""Verify that Godot light and sun updates alter rendered pixels."""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import sys

from compare_renders import read_png


ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
CAPTURES = [
    BUILD / "godot-lighting-unlit.png",
    BUILD / "godot-lighting-lit.png",
    BUILD / "godot-lighting-moved.png",
]


def changed_pixels(first, second) -> int:
    if first[:3] != second[:3]:
        raise ValueError("Godot lighting frames have mismatched dimensions or channels")
    channels = first[2]
    return sum(
        first[3][offset:offset + 3] != second[3][offset:offset + 3]
        for offset in range(0, len(first[3]), channels)
    )


def main() -> int:
    godot = os.environ.get("GODOT_BIN") or shutil.which("godot")
    if not godot:
        print("Install Godot or set GODOT_BIN to its executable path.", file=sys.stderr)
        return 2
    BUILD.mkdir(exist_ok=True)
    for capture in CAPTURES:
        capture.unlink(missing_ok=True)
    result = subprocess.run([
        godot,
        "--path", str(ROOT / "backends/godot"),
        "--script", "res://lighting_capture.gd",
        "--", *(str(capture) for capture in CAPTURES),
    ], cwd=ROOT, check=False)
    if result.returncode != 0:
        return result.returncode
    try:
        frames = [read_png(capture) for capture in CAPTURES]
        counts = [changed_pixels(frames[0], frame) for frame in frames[1:]]
    except (OSError, ValueError) as error:
        print(f"Godot lighting captures are missing or invalid: {error}", file=sys.stderr)
        return 1
    if counts[0] < 256:
        print(f"Godot sun and point lights changed only {counts[0]} pixels.", file=sys.stderr)
        return 1
    moved = changed_pixels(frames[1], frames[2])
    if moved < 64:
        print(f"Moving the Godot point light changed only {moved} pixels.", file=sys.stderr)
        return 1
    print(
        "Godot lighting pixels: "
        f"unlit-to-lit={counts[0]}, moved-light={moved}."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
