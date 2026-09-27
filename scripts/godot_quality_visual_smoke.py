#!/usr/bin/env python3
"""Compare Godot Low/High quality captures and require a rendered change."""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import sys

from compare_renders import compare, read_png


ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build" / "godot-quality"
REFERENCES = ROOT / "docs" / "validation" / "references" / "godot-quality"
LOW = BUILD / "low.png"
HIGH = BUILD / "high.png"
REFERENCE_PEAK = 0.12
REFERENCE_MEAN = 0.01
MIN_CHANGED_PIXELS = 1024
MIN_MEAN_DIFFERENCE = 0.02


def changed_pixels(first, second) -> int:
    if first[:3] != second[:3]:
        raise ValueError("Godot quality frames have mismatched dimensions or channels")
    channels = first[2]
    return sum(
        max(abs(first[3][offset + channel] - second[3][offset + channel]) for channel in range(3)) > 3
        for offset in range(0, len(first[3]), channels)
    )


def main() -> int:
    godot = os.environ.get("GODOT_BIN") or shutil.which("godot")
    if not godot:
        print("Install Godot or set GODOT_BIN to its executable path.", file=sys.stderr)
        return 2
    BUILD.mkdir(parents=True, exist_ok=True)
    for capture in (LOW, HIGH):
        capture.unlink(missing_ok=True)
    result = subprocess.run([
        godot,
        "--path", str(ROOT / "backends/godot"),
        "--script", "res://quality_capture.gd",
        "--", str(LOW), str(HIGH),
    ], cwd=ROOT, check=False)
    if result.returncode != 0:
        return result.returncode
    try:
        low = read_png(LOW)
        high = read_png(HIGH)
        reference_low = read_png(REFERENCES / "low.png")
        reference_high = read_png(REFERENCES / "high.png")
        if low[:3] != high[:3] or low[:3] != reference_low[:3] or high[:3] != reference_high[:3]:
            raise ValueError("Godot quality capture dimensions differ from their references")
        changed = changed_pixels(low, high)
        (low_peak, low_mean, low_matches) = compare(low, reference_low, REFERENCE_PEAK, REFERENCE_MEAN)
        (high_peak, high_mean, high_matches) = compare(high, reference_high, REFERENCE_PEAK, REFERENCE_MEAN)
    except (OSError, ValueError) as error:
        print(f"Godot quality captures or references are invalid: {error}", file=sys.stderr)
        return 1
    if changed < MIN_CHANGED_PIXELS:
        print(f"Low/High profiles changed only {changed} rendered pixels.", file=sys.stderr)
        return 1
    # Keep a separate pairwise check even when both individual captures match.
    mean_difference = compare(low, high, 1.0, 1.0)[1]
    if mean_difference < MIN_MEAN_DIFFERENCE:
        print(f"Low/High profiles have only {mean_difference:.4f} mean RGB difference.", file=sys.stderr)
        return 1
    if not low_matches or not high_matches:
        print(
            "Godot quality references differ: "
            f"Low peak/mean={low_peak:.4f}/{low_mean:.4f}, "
            f"High peak/mean={high_peak:.4f}/{high_mean:.4f}.",
            file=sys.stderr,
        )
        return 1
    print(
        f"Godot quality references passed at {low[0]}x{low[1]}: "
        f"changed_pixels={changed}, low_peak/mean={low_peak:.4f}/{low_mean:.4f}, "
        f"high_peak/mean={high_peak:.4f}/{high_mean:.4f}."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
