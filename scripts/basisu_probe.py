#!/usr/bin/env python3
"""Build and run the Basis Universal KTX2 probe.

Fetches and builds the pinned Basis encoder (scripts/fetch_basisu.py), cooks
the texture so build/cooked/maze_tile_tex.ktx2 exists, compiles
native/basisu_probe.cpp with the Basis transcoder from the same checkout, and
requires the container to transcode to the cooked green. This is the cooked
supercompressed path the plan names for KTX/Basis; Godot's own loader consumes
the same file in the gated scene probe.

Usage:
  python3 scripts/basisu_probe.py
"""

import subprocess
import sys
from pathlib import Path

ENGINE_ROOT = Path(__file__).resolve().parents[1]
BASISU = ENGINE_ROOT / "dependencies/basisu"
MARKER = "basisu transcode:"


def relay(result: subprocess.CompletedProcess) -> None:
    sys.stdout.write(result.stdout)
    sys.stderr.write(result.stderr)


def main() -> int:
    fetch = subprocess.run(
        [sys.executable, str(ENGINE_ROOT / "scripts/fetch_basisu.py")],
        capture_output=True, text=True, check=False,
    )
    relay(fetch)
    if fetch.returncode != 0:
        return fetch.returncode

    cook = subprocess.run(
        [sys.executable, str(ENGINE_ROOT / "scripts/cook_assets.py"), str(ENGINE_ROOT)],
        capture_output=True, text=True, check=False,
    )
    relay(cook)
    ktx2 = ENGINE_ROOT / "build/cooked/maze_tile_tex.ktx2"
    cube = ENGINE_ROOT / "build/cooked/maze_tile_cube.ktx2"
    alpha = ENGINE_ROOT / "build/cooked/maze_tile_alpha.ktx2"
    if cook.returncode != 0 or not ktx2.is_file() or not cube.is_file() or not alpha.is_file():
        print("basisu probe: the cooker produced no KTX2", file=sys.stderr)
        return cook.returncode if cook.returncode != 0 else 1

    build = ENGINE_ROOT / "build"
    zstd_object = build / "basisu-zstd.o"
    cxx = "c++"
    zstd = subprocess.run(
        [cxx, "-x", "c", "-O1", "-c", str(BASISU / "zstd/zstddeclib.c"), "-o", str(zstd_object)],
        capture_output=True, text=True, check=False,
    )
    relay(zstd)
    if zstd.returncode != 0:
        return zstd.returncode

    probe = build / "basisu-probe"
    compile_result = subprocess.run([
        cxx, "-std=c++17", "-O1",
        "-I", str(ENGINE_ROOT / "native"),
        "-I", str(BASISU / "transcoder"),
        str(ENGINE_ROOT / "native/basisu_probe.cpp"),
        str(BASISU / "transcoder/basisu_transcoder.cpp"),
        str(zstd_object),
        "-o", str(probe),
    ], capture_output=True, text=True, check=False)
    relay(compile_result)
    if compile_result.returncode != 0 or not probe.is_file():
        print("basisu probe: compile failed", file=sys.stderr)
        return compile_result.returncode if compile_result.returncode != 0 else 1

    run = subprocess.run([str(probe), str(ktx2), str(cube), str(alpha)], capture_output=True, text=True, check=False)
    relay(run)
    if run.returncode != 0 or MARKER not in run.stdout:
        print("basisu probe failed; the log identifies the step.", file=sys.stderr)
        return run.returncode if run.returncode != 0 else 1
    if "basisu cubemap transcode: faces=6" not in run.stdout:
        print("basisu probe: cubemap faces were not verified", file=sys.stderr)
        return 1
    if "basisu alpha transcode:" not in run.stdout:
        print("basisu probe: alpha did not survive transcoding", file=sys.stderr)
        return 1
    print("Basis Universal validated the 2D color, cubemap, and alpha fixtures.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
