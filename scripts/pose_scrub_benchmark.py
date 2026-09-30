#!/usr/bin/env python3
"""Time M02 random-access scrubbing of a 10,000-frame take on SDL3/Metal.

Builds test/render_scene_pose_scrub_main.elisa through the render-scene smoke
harness (render-only mode, so no fixtures are recooked) and reads the 600
per-frame wall times it prints (whole frame, and the scrub work before
presenting), in nanoseconds. Each frame samples a 64-bone
120 Hz clip at a random jump, fills and submits the render pose, and presents.
"""

from __future__ import annotations

import json
import os
from pathlib import Path
import statistics
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import cook_gltf_geometry  # noqa: E402
import cook_gltf_skin  # noqa: E402
import gltf_skin_self_test  # noqa: E402

BUDGET_MS = 16.6
RIG = ROOT / "build/cooked/subsets/scrub-rig.pkg"


def write_rig(output: Path, joints: int) -> None:
    """The self-test skinned panel with its tip extended into a joint chain."""
    document = gltf_skin_self_test.generated_document(inverse_bind_matrices=None)
    for primitive in document["meshes"][0]["primitives"]:
        primitive.pop("targets", None)
    document["meshes"][0].pop("weights", None)
    parent = 2
    for index in range(joints - 2):
        child = len(document["nodes"])
        document["nodes"][parent]["children"] = [child]
        document["nodes"].append({"name": f"chain_{index}", "translation": [0.0, 0.02, 0.0]})
        document["skins"][0]["joints"].append(child)
        parent = child
    with tempfile.TemporaryDirectory(prefix="elisa-scrub-rig-") as temporary:
        source = Path(temporary) / "scrub_rig.gltf"
        source.write_text(json.dumps(document, separators=(",", ":")), encoding="utf-8")
        cook_gltf_geometry.cook_geometry_package(source, gltf_skin_self_test.ASSET_PATH, output)


def main() -> int:
    RIG.parent.mkdir(parents=True, exist_ok=True)
    write_rig(RIG, cook_gltf_skin.MAX_JOINTS)
    env = dict(os.environ)
    env.update({
        "ELISA_RENDER_SCENE_RENDER_ONLY": "1",
        "ELISA_RENDER_SCENE_PROFILE_COST_ONLY": "1",
        "ELISA_RENDER_SCENE_NATIVE_MAIN": str(ROOT / "test/render_scene_pose_scrub_main.elisa"),
    })
    result = subprocess.run([sys.executable, str(ROOT / "scripts/render_scene_native_smoke.py")],
        cwd=ROOT, env=env, capture_output=True, text=True, check=False)
    lines = result.stdout.splitlines()
    times = [int(line[len("frame_ns="):]) / 1e6 for line in lines if line.startswith("frame_ns=")]
    works = [int(line[len("work_ns="):]) / 1e6 for line in lines if line.startswith("work_ns=")]
    # Profile-cost mode runs the binary three times (the benchmark ignores the
    # profile it sets), giving three independent runs.
    runs = 3
    if result.returncode != 0 or len(times) != 600 * runs or len(works) != len(times):
        sys.stderr.write(result.stdout[-2000:] + result.stderr[-2000:])
        print(f"scrub benchmark failed: exit {result.returncode}, {len(times)} samples", file=sys.stderr)
        return 1
    worst = 0.0
    for index in range(runs):
        run = times[index * 600:(index + 1) * 600]
        work = sorted(works[index * 600:(index + 1) * 600])
        ordered = sorted(run)
        p95 = ordered[int(0.95 * len(ordered)) - 1]
        worst = max(worst, p95)
        print(f"scrub run={index + 1} frames={len(run)} frame median={statistics.median(run):.3f}ms "
              f"p95={p95:.3f}ms max={ordered[-1]:.3f}ms | work median={statistics.median(work):.3f}ms "
              f"p95={work[int(0.95 * len(work)) - 1]:.3f}ms | budget={BUDGET_MS}ms")
    return 0 if worst <= BUDGET_MS else 3

if __name__ == "__main__":
    raise SystemExit(main())
