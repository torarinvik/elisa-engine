#!/usr/bin/env python3
"""M01 skinned mesh in an engine viewport, on SDL3/Metal.

Builds test/viewport_wicked_backdrop_main.elisa through the render-scene smoke
harness with the Metal viewport bridge linked in. The Wicked renderer draws the
skinned self-test panel; its presented frame becomes the backdrop of a 320x240
engine viewport, and the shared-scene skeleton draws over it. The test itself
checks redraw-on-demand and the selected-joint overlay; this script checks the
composited viewport reproduces the Wicked frame wherever the overlay is absent.
The test also drives Wicked's camera from viewport cameras and checks that
Wicked's picking rays match the viewport's, so backdrop and overlays line up.
"""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import compare_renders  # noqa: E402
import pose_scrub_benchmark  # noqa: E402

RIG = ROOT / "build/cooked/subsets/skeleton-display-rig.pkg"
WICKED = ROOT / "build/viewport-backdrop-wicked.png"
VIEWPORT = ROOT / "build/viewport-backdrop-viewport.png"
TOLERANCE = 6       # per channel, for the linear resample of a scaled frame
MIN_MATCH = 0.95    # of viewport pixels match the Wicked frame
MIN_MESH = 1500     # viewport pixels that differ from the Wicked clear colour


def pixel(image: tuple, x: int, y: int) -> tuple[int, int, int]:
    width, _, channels, data = image[0], image[1], image[2], image[3]
    offset = (y * width + x) * channels
    return data[offset], data[offset + 1], data[offset + 2]


def main() -> int:
    RIG.parent.mkdir(parents=True, exist_ok=True)
    pose_scrub_benchmark.write_rig(RIG, 4)
    build = subprocess.run(["clang", "-c", "-fobjc-arc", "-O2", "-Wall", "-Wextra", "-Werror",
        "-o", str(ROOT / "build/viewport_metal.o"), str(ROOT / "native/viewport_metal.m")], cwd=ROOT, check=False)
    if build.returncode != 0:
        return 1
    env = dict(os.environ)
    env.update({
        "ELISA_RENDER_SCENE_RENDER_ONLY": "1",
        "ELISA_RENDER_SCENE_PROFILE_COST_ONLY": "1",
        "ELISA_RENDER_SCENE_NATIVE_MAIN": str(ROOT / "test/viewport_wicked_backdrop_main.elisa"),
        "ELISA_RENDER_SCENE_EXTRA_OBJECTS": str(ROOT / "build/viewport_metal.o"),
        "ELISA_BACKDROP_WICKED": str(WICKED),
        "ELISA_BACKDROP_VIEWPORT": str(VIEWPORT),
    })
    for path in (WICKED, VIEWPORT):
        path.unlink(missing_ok=True)
    result = subprocess.run([sys.executable, str(ROOT / "scripts/render_scene_native_smoke.py")],
        cwd=ROOT, env=env, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        sys.stderr.write(result.stdout[-2000:] + result.stderr[-2000:])
        print(f"viewport backdrop smoke failed: exit {result.returncode}", file=sys.stderr)
        return 1
    try:
        wicked = compare_renders.read_png(WICKED)
        view = compare_renders.read_png(VIEWPORT)
    except (OSError, ValueError) as error:
        print(f"viewport backdrop capture missing or invalid: {error}", file=sys.stderr)
        return 1
    ww, wh, vw, vh = wicked[0], wicked[1], view[0], view[1]
    clear = pixel(wicked, 0, 0)
    matched = mesh = 0
    for y in range(vh):
        for x in range(vw):
            # Centre of the viewport pixel in the Wicked frame's pixel grid.
            sx = min(ww - 1, int((x + 0.5) * ww / vw))
            sy = min(wh - 1, int((y + 0.5) * wh / vh))
            got = pixel(view, x, y)
            want = pixel(wicked, sx, sy)
            if all(abs(got[c] - want[c]) <= TOLERANCE for c in range(3)):
                matched += 1
            if any(abs(got[c] - clear[c]) > 24 for c in range(3)):
                mesh += 1
    share = matched / float(vw * vh)
    print(f"viewport backdrop wicked={ww}x{wh} viewport={vw}x{vh} match={share:.4f} non_clear={mesh}")
    if share < MIN_MATCH:
        print(f"composited viewport matches the Wicked frame on only {share:.4f}", file=sys.stderr)
        return 1
    if mesh < MIN_MESH:
        print(f"only {mesh} viewport pixels show the mesh or overlay", file=sys.stderr)
        return 1
    print("viewport backdrop smoke passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
