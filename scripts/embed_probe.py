#!/usr/bin/env python3
"""Build and run the host-embedding probe.

The maze game is compiled to a C archive (examples/maze/capi.elisa) whose
exported functions a C++ host links and calls, so gameplay runs in-process. The
host then reads an SDL key event, maps it to a move code, and steps the game,
which is live input driving Elisa gameplay across the boundary. This is separate
from scripts/check.elisascript, which does not emit a C archive.

Usage:
  python3 scripts/embed_probe.py
"""

import os
import subprocess
import sys
from pathlib import Path

ENGINE_ROOT = Path(__file__).resolve().parents[1]


def relay(result: subprocess.CompletedProcess) -> None:
    sys.stdout.write(result.stdout)
    sys.stderr.write(result.stderr)


def main() -> int:
    compiler = os.environ.get("ELISA_COMPILER_BIN", "elisac-stage1")
    cxx = os.environ.get("CXX", "c++")
    sdl_include = os.environ.get("ELISA_SDL3_INCLUDE_DIR", "/opt/homebrew/include")
    sdl_library = os.environ.get("ELISA_SDL3_LIB_DIR", "/opt/homebrew/lib")
    build = ENGINE_ROOT / "build"
    build.mkdir(parents=True, exist_ok=True)
    archive = build / "libmaze.a"
    emit = subprocess.run(
        [compiler, "-emit", "c-archive", "-o", str(archive), str(ENGINE_ROOT / "examples/maze/capi.elisa")],
        capture_output=True, text=True, check=False,
    )
    relay(emit)
    if emit.returncode != 0:
        return emit.returncode
    if not archive.is_file():
        print("embed: c-archive emit produced no archive", file=sys.stderr)
        return 1
    host = build / "embed-probe"
    build_result = subprocess.run(
        [cxx, "-std=c++17", "-I", str(build), "-I", sdl_include,
         str(ENGINE_ROOT / "native/embed_probe.cpp"), str(archive),
         "-L", sdl_library, "-lSDL2", "-o", str(host)],
        capture_output=True, text=True, check=False,
    )
    relay(build_result)
    if build_result.returncode != 0:
        return build_result.returncode
    run = subprocess.run([str(host), str(ENGINE_ROOT / "backends/scene_manifest.txt")], capture_output=True, text=True, check=False)
    relay(run)
    if run.returncode != 0:
        print("Embedded maze host failed; exit status identifies the step.", file=sys.stderr)
        return run.returncode
    print("Embedded maze host passed: gameplay exports and live input both drove the game.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
