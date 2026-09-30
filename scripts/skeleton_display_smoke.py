#!/usr/bin/env python3
"""M03 skeleton display over a mesh on SDL3/Metal.

Cooks a four-joint skinned panel, builds test/render_scene_skeleton_display_main.elisa
through the render-scene smoke harness (render-only mode) and compares its five
captures: the depth-tested skeleton under the panel must stay hidden, the x-ray
skeleton must draw over it in the selected and hovered colours, hiding the mesh
must reveal the depth-tested skeleton, and wireframe mode must change the mesh.
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
SHOTS = {name: ROOT / f"build/skeleton-display-{name.lower()}.png"
    for name in ("BASE", "DEPTH", "XRAY", "HIDDEN", "WIRE")}
MIN_CHANGED = 64
MAX_HIDDEN_LEAK = 16


def changed(first: tuple, second: tuple) -> int:
    channels = first[2]
    return sum(1 for offset in range(0, len(first[3]), channels)
        if first[3][offset:offset + 3] != second[3][offset:offset + 3])


def coloured(image: tuple, low: tuple[int, int, int], high: tuple[int, int, int]) -> int:
    """Pixels whose RGB lies in [low, high], e.g. the selected-orange bone."""
    channels = image[2]
    data = image[3]
    return sum(1 for offset in range(0, len(data), channels)
        if all(low[c] <= data[offset + c] <= high[c] for c in range(3)))


def main() -> int:
    RIG.parent.mkdir(parents=True, exist_ok=True)
    pose_scrub_benchmark.write_rig(RIG, 4)
    env = dict(os.environ)
    env.update({
        "ELISA_RENDER_SCENE_RENDER_ONLY": "1",
        "ELISA_RENDER_SCENE_PROFILE_COST_ONLY": "1",
        "ELISA_RENDER_SCENE_NATIVE_MAIN": str(ROOT / "test/render_scene_skeleton_display_main.elisa"),
    })
    for name, path in SHOTS.items():
        path.unlink(missing_ok=True)
        env[f"ELISA_SKELETON_{name}"] = str(path)
    result = subprocess.run([sys.executable, str(ROOT / "scripts/render_scene_native_smoke.py")],
        cwd=ROOT, env=env, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        sys.stderr.write(result.stdout[-2000:] + result.stderr[-2000:])
        print(f"skeleton display smoke failed: exit {result.returncode}", file=sys.stderr)
        return 1
    try:
        images = {name: compare_renders.read_png(path) for name, path in SHOTS.items()}
    except (OSError, ValueError) as error:
        print(f"skeleton display capture missing or invalid: {error}", file=sys.stderr)
        return 1
    base = images["BASE"]
    leak = changed(base, images["DEPTH"])
    xray = changed(base, images["XRAY"])
    hidden = changed(base, images["HIDDEN"])
    wire = changed(base, images["WIRE"])
    # Selected bone (joint 2) is orange; hovered bone (joint 3) is yellow.
    # Ranges are after Wicked's tone mapping (about 209,165,79 and 205,199,139).
    orange = coloured(images["XRAY"], (190, 140, 50), (230, 185, 110))
    yellow = coloured(images["XRAY"], (190, 185, 115), (225, 215, 165))
    revealed = coloured(images["HIDDEN"], (190, 140, 50), (230, 185, 110))
    print(f"skeleton display: depth leak={leak} xray={xray} (orange={orange} yellow={yellow}) "
          f"mesh-hidden={hidden} (orange={revealed}) wireframe={wire} pixels at {base[0]}x{base[1]}")
    if leak > MAX_HIDDEN_LEAK or coloured(images["DEPTH"], (190, 140, 50), (230, 185, 110)) > 0:
        print("depth-tested skeleton showed through the mesh", file=sys.stderr)
        return 2
    if xray < MIN_CHANGED or orange < 4 or yellow < 4:
        print("x-ray skeleton did not draw over the mesh in role colours", file=sys.stderr)
        return 3
    if hidden < MIN_CHANGED or revealed < 4:
        print("hiding the mesh did not reveal the skeleton", file=sys.stderr)
        return 4
    if wire < MIN_CHANGED:
        print("wireframe mode did not change the mesh", file=sys.stderr)
        return 5
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
