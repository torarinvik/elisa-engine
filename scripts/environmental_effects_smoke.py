#!/usr/bin/env python3
"""Render and compare the environmental-effects example before/after spawn."""

from __future__ import annotations

import os
import subprocess
import sys
import tempfile
from pathlib import Path

import compare_renders


ROOT = Path(__file__).resolve().parents[1]


def changed_pixels(first, second) -> tuple[int, float]:
    if first[:3] != second[:3]:
        raise ValueError("captured frame sizes or channel layouts differ")
    channels = first[2]
    pixels = len(first[3]) // channels
    changed = 0
    total = 0
    for index in range(pixels):
        offset = index * channels
        difference = max(abs(first[3][offset + channel] - second[3][offset + channel])
            for channel in range(3))
        total += difference
        changed += difference > 2
    return changed, total / pixels / 255.0


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="elisa-effects-") as temporary:
        output = Path(temporary)
        baseline = output / "baseline.png"
        impact = output / "impact.png"
        environment = os.environ.copy()
        environment["ELISA_EFFECTS_SAMPLE_SMOKE"] = "1"
        environment["ELISA_EFFECTS_BASELINE_CAPTURE"] = str(baseline)
        environment["ELISA_EFFECTS_IMPACT_CAPTURE"] = str(impact)
        command = [sys.executable, str(ROOT / "scripts/elisa_build_run.py"), "run",
            "--project", str(ROOT / "examples/environmental_effects")]
        result = subprocess.run(command, cwd=ROOT, env=environment, check=False)
        if result.returncode != 0:
            return result.returncode
        try:
            before = compare_renders.read_png(str(baseline))
            after = compare_renders.read_png(str(impact))
        except (OSError, ValueError) as error:
            print(f"Environmental-effects captures are invalid: {error}", file=sys.stderr)
            return 1
        changed, mean = changed_pixels(before, after)
        print(f"Environmental impact changed {changed}/{before[0] * before[1]} pixels; mean RGB delta={mean:.5f}.")
        if changed < 500 or mean < 0.0005:
            print("The dispatched effects did not produce a visible rendered change.", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
