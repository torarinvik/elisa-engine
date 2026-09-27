#!/usr/bin/env python3
"""Render a Godot selection outline and require visible output pixels."""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import sys

from compare_renders import read_png


ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
BASELINE = BUILD / "godot-outline-baseline.png"
VISIBLE = BUILD / "godot-outline-visible.png"
MINIMUM_CHANGED_PIXELS = 64


def main() -> int:
    godot = os.environ.get("GODOT_BIN") or shutil.which("godot")
    if not godot:
        print("Install Godot or set GODOT_BIN to its executable path.", file=sys.stderr)
        return 2
    BUILD.mkdir(exist_ok=True)
    BASELINE.unlink(missing_ok=True)
    VISIBLE.unlink(missing_ok=True)
    result = subprocess.run([
        godot,
        "--path", str(ROOT / "backends/godot"),
        "--script", "res://editor_outline_capture.gd",
        "--", str(BASELINE), str(VISIBLE),
    ], cwd=ROOT, check=False)
    if result.returncode != 0:
        return result.returncode
    try:
        baseline = read_png(BASELINE)
        visible = read_png(VISIBLE)
    except (OSError, ValueError) as error:
        print(f"Godot outline captures are missing or invalid: {error}", file=sys.stderr)
        return 1
    if baseline[:3] != visible[:3]:
        print("Godot outline captures have mismatched dimensions or channels.", file=sys.stderr)
        return 1
    channels = baseline[2]
    changed = sum(
        1 for offset in range(0, len(baseline[3]), channels)
        if baseline[3][offset:offset + 3] != visible[3][offset:offset + 3]
    )
    if changed < MINIMUM_CHANGED_PIXELS:
        print(f"Godot selection outline changed only {changed} pixels.", file=sys.stderr)
        return 1
    print(f"Godot selection outline changed {changed} pixels at {baseline[0]}x{baseline[1]}.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
