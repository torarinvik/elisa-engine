#!/usr/bin/env python3
"""Time the M05 overlay budget on SDL3/Metal and capture the flagged spike.

Builds test/render_scene_overlay_budget_main.elisa through the render-scene
smoke harness (render-only mode) and reads the 600 per-frame wall times it
prints (whole frame, and the overlay work before presenting), in nanoseconds.
Each frame rebuilds heat trails for 4 joints x 600 frames, 3 onion ghosts
either side of a moving frame and the ground-contact crosses, submits them
through the batched debug-line path, and presents. The final frame centres
the ghosts on the knocked frame and is saved to build/overlay-budget.png,
which must contain heat-red pixels confined to the knocked segment.
"""

from __future__ import annotations

import os
from pathlib import Path
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import compare_renders  # noqa: E402

BUDGET_MS = 16.6
MEASURED = 600
CAPTURE = ROOT / "build/overlay-budget.png"
MIN_RED_PIXELS = 4


MAX_RED_SPREAD = 80


def red_pixels(path: Path) -> list[tuple[int, int]]:
    """Positions of heat-red pixels; only the knocked segment should be red."""
    width, height, channels, data = compare_renders.read_png(path)
    return [((offset // channels) % width, (offset // channels) // width)
        for offset in range(0, width * height * channels, channels)
        if data[offset] >= 200 and data[offset + 1] <= 80 and data[offset + 2] <= 80]


def main() -> int:
    CAPTURE.unlink(missing_ok=True)
    env = dict(os.environ)
    env.update({
        "ELISA_RENDER_SCENE_RENDER_ONLY": "1",
        "ELISA_RENDER_SCENE_PROFILE_COST_ONLY": "1",
        "ELISA_RENDER_SCENE_NATIVE_MAIN": str(ROOT / "test/render_scene_overlay_budget_main.elisa"),
        "ELISA_OVERLAY_BUDGET_CAPTURE": str(CAPTURE),
    })
    result = subprocess.run([sys.executable, str(ROOT / "scripts/render_scene_native_smoke.py")],
        cwd=ROOT, env=env, capture_output=True, text=True, check=False)
    lines = result.stdout.splitlines()
    times = [int(line[len("frame_ns="):]) / 1e6 for line in lines if line.startswith("frame_ns=")]
    works = [int(line[len("work_ns="):]) / 1e6 for line in lines if line.startswith("work_ns=")]
    counts = sorted({int(line[len("lines="):]) for line in lines if line.startswith("lines=")})
    # Profile-cost mode runs the binary three times, giving three runs.
    runs = 3
    if result.returncode != 0 or len(times) != MEASURED * runs or len(works) != len(times):
        sys.stderr.write(result.stdout[-2000:] + result.stderr[-2000:])
        print(f"overlay benchmark failed: exit {result.returncode}, {len(times)} samples", file=sys.stderr)
        return 1
    worst = 0.0
    for index in range(runs):
        run = sorted(times[index * MEASURED:(index + 1) * MEASURED])
        work = sorted(works[index * MEASURED:(index + 1) * MEASURED])
        p95 = run[int(0.95 * len(run)) - 1]
        worst = max(worst, p95)
        print(f"overlay run={index + 1} lines={counts} frame median={statistics.median(run):.3f}ms "
              f"p95={p95:.3f}ms max={run[-1]:.3f}ms | work median={statistics.median(work):.3f}ms "
              f"p95={work[int(0.95 * len(work)) - 1]:.3f}ms | budget={BUDGET_MS}ms")
    try:
        red = red_pixels(CAPTURE)
    except (OSError, ValueError) as error:
        print(f"overlay capture missing or invalid: {error}", file=sys.stderr)
        return 1
    if len(red) < MIN_RED_PIXELS:
        print(f"overlay capture has only {len(red)} heat-red pixels", file=sys.stderr)
        return 4
    xs = [x for x, _ in red]
    ys = [y for _, y in red]
    print(f"overlay capture {CAPTURE.relative_to(ROOT)} has {len(red)} heat-red pixels "
          f"in x {min(xs)}..{max(xs)}, y {min(ys)}..{max(ys)}")
    # The spike is flagged and nothing else is: all red sits in one small box.
    if max(xs) - min(xs) > MAX_RED_SPREAD or max(ys) - min(ys) > MAX_RED_SPREAD:
        print("heat-red pixels are spread beyond the knocked segment", file=sys.stderr)
        return 5
    return 0 if worst <= BUDGET_MS else 3

if __name__ == "__main__":
    raise SystemExit(main())
