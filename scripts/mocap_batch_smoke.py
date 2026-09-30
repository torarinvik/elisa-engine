#!/usr/bin/env python3
"""Smoke for the M07 batch CLI (build/mocap-clean, scripts/build_mocap_clean.sh).

Cleans a temp folder holding the boxer GLB, a malformed .glb and a non-GLB
file twice and checks: the bad file is reported and counted in the exit code,
the other file still finishes, both runs give byte-identical GLB and PNG
output, the thumbnail shows bone pixels, and a later key gives another pose.
Skips (exit 0) when the external fixture is absent.
"""
import os
import shutil
import subprocess
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FIXTURE = ROOT.parent / "elisa-boxing-game/build/dual-stance/black/black-boxer.glb"
TOOL = ROOT / "build/mocap-clean"
WORK = ROOT / "build/mocap-batch"


def fail(code, message):
    print(f"mocap batch smoke FAILED ({code}): {message}")
    sys.exit(code)


def run(out, key):
    out.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, MOCAP_IN=str(WORK / "in"), MOCAP_OUT=str(out), MOCAP_THUMB_KEY=str(key))
    done = subprocess.run([str(TOOL)], env=env, capture_output=True, text=True, timeout=600)
    return done.returncode, done.stdout.split("\n")


def pixels(path):
    data = path.read_bytes()
    at, idat, width = 8, b"", 0
    while at < len(data):
        length = int.from_bytes(data[at:at + 4], "big")
        kind = data[at + 4:at + 8]
        body = data[at + 8:at + 8 + length]
        if kind == b"IHDR":
            width = int.from_bytes(body[0:4], "big")
        if kind == b"IDAT":
            idat += body
        at += 12 + length
    raw = zlib.decompress(idat)
    rows = [raw[r + 1:r + 1 + width * 4] for r in range(0, len(raw), width * 4 + 1)]
    return b"".join(rows)


def bone_pixels(rgba):
    # ViewportDraw bone colour rgba(240, 200, 60).
    return sum(1 for i in range(0, len(rgba), 4) if rgba[i] > 200 and rgba[i + 1] > 160 and rgba[i + 2] < 110)


def main():
    if not FIXTURE.exists():
        print(f"mocap batch smoke skipped: {FIXTURE} not found")
        return
    if not TOOL.exists():
        fail(2, "build/mocap-clean missing; run scripts/build_mocap_clean.sh")
    shutil.rmtree(WORK, ignore_errors=True)
    (WORK / "in").mkdir(parents=True)
    shutil.copyfile(FIXTURE, WORK / "in/boxer.glb")
    (WORK / "in/broken.glb").write_bytes(b"glTF not really")
    (WORK / "in/notes.txt").write_text("not a glb\n")
    first, lines = run(WORK / "a", 0)
    second, _ = run(WORK / "b", 0)
    later, _ = run(WORK / "c", 40)
    print("\n".join(line for line in lines if line))
    if first != 1 or second != 1 or later != 1:
        fail(10, f"exit codes {first} {second} {later}, expected 1 (one bad file)")
    if not any(l.startswith("FAIL broken.glb") for l in lines):
        fail(11, "broken.glb not reported")
    if not any(l.startswith("OK boxer.glb") for l in lines) or "DONE files 2 1" not in lines:
        fail(12, "boxer.glb did not finish or the summary is wrong")
    for name in ("boxer.glb", "boxer.png"):
        if (WORK / "a" / name).read_bytes() != (WORK / "b" / name).read_bytes():
            fail(13, f"{name} differs between runs")
    rest, posed = pixels(WORK / "a/boxer.png"), pixels(WORK / "c/boxer.png")
    bones = bone_pixels(rest)
    if bones < 200:
        fail(14, f"thumbnail shows {bones} bone pixels")
    changed = sum(1 for i in range(0, len(rest), 4) if rest[i:i + 4] != posed[i:i + 4])
    if changed < 100:
        fail(15, f"key 40 thumbnail differs from key 0 by only {changed} pixels")
    print(f"mocap batch smoke passed: bone pixels={bones}, key0 vs key40 changed={changed}")


if __name__ == "__main__":
    main()
