#!/usr/bin/env python3
"""Build and run the Godot embedding probe.

The Elisa maze game is compiled to a C archive (examples/maze/capi.elisa),
which a hand-written GDExtension class (backends/godot-embed/) calls, so the
Godot host drives the same Elisa gameplay the native host does. The interface
header is dumped from the installed Godot itself (no godot-cpp branch needed).

Usage:
  python3 scripts/godot_embed_probe.py
"""

import os
import shutil
import subprocess
import sys
from pathlib import Path

ENGINE_ROOT = Path(__file__).resolve().parents[1]
DYLIB_NAME = "libelisa_maze.macos.arm64.dylib"


def find_tool(name: str, variable: str) -> str:
    configured = os.environ.get(variable, "")
    if configured:
        return configured
    located = shutil.which(name)
    if located is None:
        raise SystemExit(f"Install {name} or set {variable} to its executable path.")
    return located


def run(arguments: list[str]) -> subprocess.CompletedProcess:
    return subprocess.run(arguments, capture_output=True, text=True, check=False)


def relay(result: subprocess.CompletedProcess) -> None:
    sys.stdout.write(result.stdout)
    sys.stderr.write(result.stderr)


def main() -> int:
    compiler = os.environ.get("ELISA_COMPILER_BIN", "elisac-stage1")
    godot = find_tool("godot", "GODOT_BIN")
    cxx = os.environ.get("CXX", "c++")
    addons = ENGINE_ROOT / "backends" / "godot-embed"
    build = ENGINE_ROOT / "build"
    header_dir = ENGINE_ROOT / "dependencies" / "gdextension"
    bin_dir = addons / "bin"
    bin_dir.mkdir(parents=True, exist_ok=True)

    header = subprocess.run(
        [sys.executable, str(ENGINE_ROOT / "scripts/fetch_gdextension_header.py")],
        capture_output=True, text=True, check=False,
    )
    relay(header)
    if header.returncode != 0:
        return header.returncode

    build.mkdir(parents=True, exist_ok=True)
    archive = build / "libmaze.a"
    emit = run([compiler, "-emit", "c-archive", "-o", str(archive),
                str(ENGINE_ROOT / "examples/maze/capi.elisa")])
    relay(emit)
    if emit.returncode != 0 or not archive.is_file():
        print("embed godot: c-archive emit failed", file=sys.stderr)
        return emit.returncode if emit.returncode != 0 else 1

    dylib = bin_dir / DYLIB_NAME
    compile_result = run([
        cxx, "-std=c++17", "-O1", "-dynamiclib",
        "-I", str(header_dir), "-I", str(build),
        str(addons / "elisa_godot_bridge.cpp"), str(archive),
        "-o", str(dylib),
    ])
    relay(compile_result)
    if compile_result.returncode != 0 or not dylib.is_file():
        print("embed godot: bridge compile failed", file=sys.stderr)
        return compile_result.returncode if compile_result.returncode != 0 else 1

    # Godot only loads .gdextension files listed in the generated editor cache,
    # and this Godot build crashes at import shutdown after writing it, so the
    # artifact is what gets validated here.
    run([godot, "--headless", "--path", str(addons), "--import"])
    extension_list = addons / ".godot" / "extension_list.cfg"
    if not extension_list.is_file() or "elisa_maze.gdextension" not in extension_list.read_text():
        print("embed godot: the editor cache did not list the extension", file=sys.stderr)
        return 1

    probe = run([
        godot, "--headless", "--path", str(addons),
        "--script", "res://godot_embed_probe.gd",
    ])
    relay(probe)
    markers = (
        "embed godot live input:",
        "embed godot: full session agrees with the native embedding",
    )
    if probe.returncode != 0 or any(marker not in probe.stdout for marker in markers):
        print("Godot embedded maze host failed; the log identifies the step.", file=sys.stderr)
        return probe.returncode if probe.returncode != 0 else 1
    print("Godot embedded maze host passed: the same Elisa gameplay drives both host families.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
