#!/usr/bin/env python3
"""Smoke for the M06 FBX path (native/fbx_to_glb.c through build/mocap-clean).

Bakes a real Mixamo-rig FBX take to GLB, samples the GLB's node tracks in
Python and compares every bone's world position with Blender's own FBX
import (tools/fbx_glb_reference.py) at four frames. Also checks that a
truncated FBX is rejected with nothing written, that two conversions are
byte-identical, and that the cleaned take reloads and thumbnails. Skips
(exit 0) when the external take or Blender is absent.
"""
import json
import math
import os
import shutil
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TAKE = ROOT.parent / "elisa-boxing-game/build/bladed-cascadeur/black/clips/bladed_cross.fbx"
TOOL = ROOT / "build/mocap-clean"
WORK = ROOT / "build/fbx-batch"
TOLERANCE_M = 1e-3


def fail(code, message):
    print(f"fbx glb smoke FAILED ({code}): {message}")
    sys.exit(code)


def run(folder, out, convert_only):
    out.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, MOCAP_IN=str(folder), MOCAP_OUT=str(out), MOCAP_CONVERT_ONLY=convert_only)
    done = subprocess.run([str(TOOL)], env=env, capture_output=True, text=True, timeout=600)
    return done.returncode, [line for line in done.stdout.split("\n") if line]


def load_glb(path):
    data = path.read_bytes()
    json_length = struct.unpack_from("<I", data, 12)[0]
    doc = json.loads(data[20:20 + json_length])
    bin_start = 20 + json_length + 8
    def floats(index):
        accessor = doc["accessors"][index]
        view = doc["bufferViews"][accessor["bufferView"]]
        width = {"SCALAR": 1, "VEC3": 3, "VEC4": 4}[accessor["type"]]
        values = struct.unpack_from(f"<{accessor['count'] * width}f", data, bin_start + view.get("byteOffset", 0))
        return [values[k * width:(k + 1) * width] for k in range(accessor["count"])]
    return doc, floats


def sample(times, values, t, rotation):
    if t <= times[0][0]:
        return list(values[0])
    for k in range(1, len(times)):
        if t <= times[k][0]:
            a, b = values[k - 1], values[k]
            w = (t - times[k - 1][0]) / (times[k][0] - times[k - 1][0])
            if rotation and sum(x * y for x, y in zip(a, b)) < 0:
                b = [-x for x in b]
            v = [x + (y - x) * w for x, y in zip(a, b)]
            if rotation:
                n = math.sqrt(sum(x * x for x in v))
                v = [x / n for x in v]
            return v
    return list(values[-1])


def rotate(q, v):
    x, y, z, w = q
    tx, ty, tz = 2 * (y * v[2] - z * v[1]), 2 * (z * v[0] - x * v[2]), 2 * (x * v[1] - y * v[0])
    return [v[0] + w * tx + y * tz - z * ty, v[1] + w * ty + z * tx - x * tz, v[2] + w * tz + x * ty - y * tx]


def quat_mul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return [aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz]


def world_positions(doc, floats, t):
    nodes = doc["nodes"]
    local = [{"translation": n.get("translation", [0, 0, 0]), "rotation": n.get("rotation", [0, 0, 0, 1]),
              "scale": n.get("scale", [1, 1, 1])} for n in nodes]
    animation = doc["animations"][0]
    for channel in animation["channels"]:
        sampler = animation["samplers"][channel["sampler"]]
        path = channel["target"]["path"]
        local[channel["target"]["node"]][path] = sample(floats(sampler["input"]), floats(sampler["output"]), t,
                                                        path == "rotation")
    parent = {c: k for k, n in enumerate(nodes) for c in n.get("children", [])}
    positions = {}
    for k, node in enumerate(nodes):
        p, chain = [0.0, 0.0, 0.0], k
        while chain is not None:
            l = local[chain]
            p = [a + b for a, b in zip(l["translation"], rotate(l["rotation"], [s * x for s, x in zip(l["scale"], p)]))]
            chain = parent.get(chain)
        positions[node.get("name", "")] = p
    return positions


def main():
    if not TAKE.exists() or shutil.which("blender") is None:
        print("fbx glb smoke skipped: take or blender missing")
        return
    if not TOOL.exists():
        fail(2, "build/mocap-clean missing; run scripts/build_mocap_clean.sh")
    shutil.rmtree(WORK, ignore_errors=True)
    (WORK / "in").mkdir(parents=True)
    shutil.copyfile(TAKE, WORK / "in/take.fbx")
    (WORK / "bad").mkdir()
    (WORK / "bad/truncated.fbx").write_bytes(TAKE.read_bytes()[:40000])
    code, lines = run(WORK / "bad", WORK / "bad-out", "1")
    if code != 1 or not lines or not lines[0].startswith("FAIL truncated.fbx -101"):
        fail(10, f"truncated FBX not rejected: {code} {lines}")
    if any((WORK / "bad-out").iterdir()):
        fail(11, "a rejected FBX left output behind")
    for out in ("a", "b"):
        code, lines = run(WORK / "in", WORK / out, "1")
        if code != 0:
            fail(12, f"conversion failed: {lines}")
    if (WORK / "a/take.glb").read_bytes() != (WORK / "b/take.glb").read_bytes():
        fail(13, "two conversions differ")
    code, lines = run(WORK / "in", WORK / "clean", "0")
    if code != 0 or not lines[0].startswith("OK take.fbx") or not (WORK / "clean/take.png").exists():
        fail(14, f"clean + thumbnail failed: {lines}")
    subprocess.run(["blender", "--background", "--factory-startup", "--python", str(ROOT / "tools/fbx_glb_reference.py"),
                    "--", str(WORK / "in/take.fbx"), str(WORK / "ref.json")], check=True, capture_output=True, timeout=600)
    reference = json.loads((WORK / "ref.json").read_text())
    doc, floats = load_glb(WORK / "a/take.glb")
    worst, compared = 0.0, 0
    for frame in reference["frames"]:
        ours = world_positions(doc, floats, frame["seconds"])
        for name, expected in frame["joints"].items():
            if name not in ours:
                fail(15, f"bone {name} missing from the GLB")
            worst = max(worst, max(abs(a - b) for a, b in zip(ours[name], expected)))
            compared += 1
    if worst > TOLERANCE_M:
        fail(16, f"worst joint error {worst:.6f} m over {compared} joint samples")
    print(f"fbx glb smoke passed: {compared} joint samples, worst error {worst * 1000:.4f} mm, "
          f"{len(doc['nodes'])} nodes, {len(doc['animations'][0]['channels'])} channels")


if __name__ == "__main__":
    main()
