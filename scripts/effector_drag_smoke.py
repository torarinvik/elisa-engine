#!/usr/bin/env python3
"""M04 live IK drag on a skinned SDL3/Metal instance.

Cooks the four-joint skinned panel, builds test/render_scene_effector_drag_main.elisa
through the render-scene smoke harness (render-only mode) and compares its
captures: the dragged pose must move the panel, and cancelling must return it
to the rest capture.
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

RIG = ROOT / "build/cooked/subsets/effector-drag-rig.pkg"
SHOTS = {name: ROOT / f"build/effector-drag-{name.lower()}.png" for name in ("REST", "DRAG", "CANCEL")}
MIN_MOVED = 200
MAX_CANCEL_DRIFT = 0


def changed(first: tuple, second: tuple) -> int:
    channels = first[2]
    return sum(1 for offset in range(0, len(first[3]), channels)
        if first[3][offset:offset + 3] != second[3][offset:offset + 3])


def main() -> int:
    RIG.parent.mkdir(parents=True, exist_ok=True)
    pose_scrub_benchmark.write_rig(RIG, 4)
    env = dict(os.environ)
    env.update({
        "ELISA_RENDER_SCENE_RENDER_ONLY": "1",
        "ELISA_RENDER_SCENE_PROFILE_COST_ONLY": "1",
        "ELISA_RENDER_SCENE_NATIVE_MAIN": str(ROOT / "test/render_scene_effector_drag_main.elisa"),
    })
    for name, path in SHOTS.items():
        path.unlink(missing_ok=True)
        env[f"ELISA_EFFECTOR_{name}"] = str(path)
    result = subprocess.run([sys.executable, str(ROOT / "scripts/render_scene_native_smoke.py")],
        cwd=ROOT, env=env, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        sys.stderr.write(result.stdout[-2000:] + result.stderr[-2000:])
        print(f"effector drag smoke failed: exit {result.returncode}", file=sys.stderr)
        return 1
    images = {name: compare_renders.read_png(path) for name, path in SHOTS.items()}
    moved = changed(images["REST"], images["DRAG"])
    drift = changed(images["REST"], images["CANCEL"])
    print(f"effector drag moved={moved} cancel_drift={drift}")
    if moved < MIN_MOVED:
        print(f"the drag moved {moved} pixels, expected at least {MIN_MOVED}", file=sys.stderr)
        return 2
    if drift > MAX_CANCEL_DRIFT:
        print(f"cancel left {drift} pixels different from rest", file=sys.stderr)
        return 3
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
