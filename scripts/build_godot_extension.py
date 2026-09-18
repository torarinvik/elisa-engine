#!/usr/bin/env python3
"""Build the ElisaMaze GDExtension into a Godot project.

Emits the C ABI archive, dumps the installed Godot's own interface header,
compiles backends/godot-embed/elisa_godot_bridge.cpp into the project's
res://bin, copies the .gdextension beside the project, and imports so the
generated extension cache lists it. Godot only loads extensions named in that
cache, and this Godot build crashes at import shutdown after writing it, so the
cache artifact is validated rather than the import exit code. Used by the
rendered capture host; the embedding probe has its own runner.

Usage:
  python3 scripts/build_godot_extension.py BACKENDS/GODOT
"""

import os
import shutil
import subprocess
import sys
from pathlib import Path

ENGINE_ROOT = Path(__file__).resolve().parents[1]
DYLIB_NAME = "libelisa_maze.macos.arm64.dylib"


def relay(result: subprocess.CompletedProcess) -> None:
    sys.stdout.write(result.stdout)
    sys.stderr.write(result.stderr)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: build_godot_extension.py <project-directory>", file=sys.stderr)
        return 2
    project = Path(sys.argv[1]).resolve(strict=True)
    if not (project / "project.godot").is_file():
        print(f"{project} is not a Godot project", file=sys.stderr)
        return 2
    addons = ENGINE_ROOT / "backends/godot-embed"
    build = ENGINE_ROOT / "build"
    header_dir = ENGINE_ROOT / "dependencies/gdextension"
    compiler = os.environ.get("ELISA_COMPILER_BIN", "elisac-stage1")
    godot = os.environ.get("GODOT_BIN", "") or shutil.which("godot")
    if not godot:
        print("install Godot or set GODOT_BIN", file=sys.stderr)
        return 2

    header = subprocess.run(
        [sys.executable, str(ENGINE_ROOT / "scripts/fetch_gdextension_header.py")],
        capture_output=True, text=True, check=False,
    )
    relay(header)
    if header.returncode != 0:
        return header.returncode

    build.mkdir(parents=True, exist_ok=True)
    archive = build / "libmaze.a"
    emit = subprocess.run(
        [compiler, "-emit", "c-archive", "-o", str(archive), str(ENGINE_ROOT / "examples/maze/capi.elisa")],
        capture_output=True, text=True, check=False,
    )
    relay(emit)
    if emit.returncode != 0 or not archive.is_file():
        print("Godot extension build: c-archive emit failed", file=sys.stderr)
        return emit.returncode if emit.returncode != 0 else 1

    bin_dir = project / "bin"
    bin_dir.mkdir(parents=True, exist_ok=True)
    dylib = bin_dir / DYLIB_NAME
    compile_result = subprocess.run([
        os.environ.get("CXX", "c++"), "-std=c++17", "-O1", "-dynamiclib",
        "-I", str(header_dir), "-I", str(build),
        str(addons / "elisa_godot_bridge.cpp"), str(archive),
        "-o", str(dylib),
    ], capture_output=True, text=True, check=False)
    relay(compile_result)
    if compile_result.returncode != 0 or not dylib.is_file():
        print("Godot extension build: bridge compile failed", file=sys.stderr)
        return compile_result.returncode if compile_result.returncode != 0 else 1

    shutil.copy2(addons / "elisa_maze.gdextension", project / "elisa_maze.gdextension")
    subprocess.run([godot, "--headless", "--path", str(project), "--import"], capture_output=True, text=True, check=False)
    extension_list = project / ".godot/extension_list.cfg"
    if not extension_list.is_file() or "elisa_maze.gdextension" not in extension_list.read_text(encoding="utf-8"):
        print("Godot extension build: the editor cache did not list the extension", file=sys.stderr)
        return 1
    print(f"Godot extension ready: {dylib.relative_to(ENGINE_ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
