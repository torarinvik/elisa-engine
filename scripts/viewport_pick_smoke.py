#!/usr/bin/env python3
"""M03 skeleton picking in live perspective and orthographic viewports.

Builds test/render_scene_viewport_pick_main.elisa through the render-scene
smoke harness (render-only mode). The main checks every pixel of three
viewports against SkeletonPick and exits non-zero on the first failed rule.
"""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    env = dict(os.environ)
    env.update({
        "ELISA_RENDER_SCENE_RENDER_ONLY": "1",
        "ELISA_RENDER_SCENE_PROFILE_COST_ONLY": "1",
        "ELISA_RENDER_SCENE_NATIVE_MAIN": str(ROOT / "test/render_scene_viewport_pick_main.elisa"),
    })
    result = subprocess.run([sys.executable, str(ROOT / "scripts/render_scene_native_smoke.py")],
        cwd=ROOT, env=env, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        sys.stderr.write(result.stdout[-2000:] + result.stderr[-2000:])
        print(f"viewport pick smoke failed: exit {result.returncode}", file=sys.stderr)
        return 1
    print("viewport pick smoke passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
