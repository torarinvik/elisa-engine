#!/usr/bin/env python3
"""Build an ordinary Elisa main and verify its scene renders through Wicked."""

from __future__ import annotations

import json
import os
import shutil
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FRAMEWORKS = [
    "Foundation", "CoreFoundation", "CoreGraphics", "CoreText", "ImageIO",
    "Metal", "QuartzCore", "AppKit", "IOKit", "GameController", "AudioToolbox",
    "CoreAudio", "AVFoundation", "VideoToolbox", "Cocoa",
]


def run(command: list[str], *, cwd: Path | None = None, env: dict[str, str] | None = None) -> int:
    print("+", shlex.join(command), flush=True)
    return subprocess.run(command, cwd=cwd, env=env, check=False).returncode


def main() -> int:
    if sys.platform != "darwin":
        print("render scene smoke requires the SDL3/Metal macOS native gate", file=sys.stderr)
        return 2

    wicked_root = Path(os.environ.get("WICKED_ROOT", ROOT.parent / "WickedEngine")).resolve()
    wicked_source = wicked_root / "WickedEngine"
    wicked_build = Path(os.environ.get("WICKED_BUILD", wicked_root / "build-elisa-sdl3")).resolve()
    libraries = wicked_build / "WickedEngine"
    sdl_include = Path(os.environ.get("WICKED_SDL3_INCLUDE_DIR", "/opt/homebrew/include")).resolve()
    sdl_library = Path(os.environ.get("WICKED_SDL3_LIB_DIR", "/opt/homebrew/lib")).resolve()
    brew_include = Path(os.environ.get("WICKED_BREW_INCLUDE_DIR", "/opt/homebrew/include")).resolve()
    brew_library = Path(os.environ.get("WICKED_BREW_LIB_DIR", "/opt/homebrew/lib")).resolve()
    utility = libraries / "Utility"
    required = [
        wicked_source / "wiApplication.h", wicked_source / "wiAppleHelper.mm",
        wicked_source / "wiInput_Apple.mm", wicked_source / "shaders",
        wicked_source / "libdxcompiler.dylib", libraries / "libWickedEngine.a",
        libraries / "libJolt.a", utility / "libUtility.a",
        utility / "FAudio/libFAudio.a", libraries / "LUA/libLUA.a",
        sdl_include / "SDL3/SDL.h", sdl_library / "libSDL3.dylib",
    ]
    missing = [str(path) for path in required if not path.exists()]
    if missing:
        print("Missing render scene smoke dependency:\n  " + "\n  ".join(missing), file=sys.stderr)
        return 2

    build = ROOT / "build"
    build.mkdir(exist_ok=True)
    cooked_mesh = build / "cooked/render-scene-triangle.pkg"
    status = run([
        sys.executable, str(ROOT / "scripts/cook_fbx_asset.py"),
        str(ROOT / "test/fixtures/fbx_triangle.fbx"),
        "--asset-path", "test/fixtures/fbx_triangle.fbx",
        "--output", str(cooked_mesh),
    ])
    if status != 0:
        return status
    normal_map = build / "cooked/render-scene-normal.png"
    shutil.copyfile(ROOT / "backends/coordinate_reference.png", normal_map)

    compiler = os.environ.get("ELISA_COMPILER_BIN", "elisac-stage1")
    cxx = os.environ.get("CXX", "clang++")
    archive = build / "render-scene-native-smoke.a"
    executable = build / "render-scene-native-smoke"
    status = run([
        compiler, "-emit", "c-archive", "-o", str(archive),
        str(ROOT / "test/render_scene_native_main.elisa"),
    ])
    if status != 0:
        return status
    manifest = archive.with_suffix(".elisa-abi.json")
    if not manifest.is_file():
        print("compiler did not emit the Elisa ABI audit manifest", file=sys.stderr)
        return 1
    abi = json.loads(manifest.read_text())
    if any(abi.get(field) for field in ("exported_functions", "exported_globals", "exported_types")):
        print("render scene smoke must not declare game-owned C exports", file=sys.stderr)
        return 1

    command = [
        cxx, "-std=c++17", "-O0", "-include", "filesystem", "-DWI_UNORDERED_MAP_TYPE=2",
        "-DWICKED_CMAKE_BUILD", "-DSDL3=1", "-D__OBJC_BOOL_IS_BOOL=1",
        "-DELISA_RENDER_SCENE_TEST_PROBE=1",
        "-I", str(build), "-I", str(ROOT / "native"), "-I", str(wicked_source),
        "-I", str(utility), "-I", str(wicked_source / "Utility/metal"),
        "-I", str(wicked_source / "Utility/DirectXMath"),
        "-I", str(sdl_include), "-I", str(sdl_include / "SDL3"),
        "-I", str(brew_include), "-I", str(brew_include / "freetype2"),
        "-I", str(brew_include / "harfbuzz"),
        str(ROOT / "native/application_abi.cpp"),
        str(ROOT / "native/render_scene_abi.cpp"),
        str(ROOT / "native/elisa_native_fallbacks.cpp"),
        str(wicked_source / "wiAppleHelper.mm"), str(wicked_source / "wiInput_Apple.mm"),
        str(archive), str(libraries / "libWickedEngine.a"), str(libraries / "libJolt.a"),
        str(utility / "libUtility.a"), str(utility / "FAudio/libFAudio.a"),
        str(libraries / "LUA/libLUA.a"),
        "-L", str(sdl_library), "-lSDL3", "-L", str(brew_library),
        "-lfreetype", "-lharfbuzz", "-lzstd", "-Wl,-rpath,@executable_path",
        "-Wl,-rpath," + str(wicked_source),
    ]
    for framework in FRAMEWORKS:
        command.extend(["-framework", framework])
    command.extend(["-o", str(executable)])
    status = run(command)
    if status != 0:
        return status

    runtime_env = dict(os.environ)
    runtime_env["ELISA_ENGINE_SHADER_PATH"] = str(wicked_source / "shaders")
    runtime_env["ELISA_PROJECT_ROOT"] = str(ROOT)
    with tempfile.TemporaryDirectory(prefix="Elisa render scene smoke ") as working_directory, \
            tempfile.TemporaryDirectory(prefix="Elisa cooked mesh path escape ") as outside_directory:
        outside_package = Path(outside_directory) / "outside.pkg"
        outside_package.write_text("format=elisa-cooked-v2\n", encoding="ascii")
        escape_link = build / "cooked/render-scene-outside-link.pkg"
        escape_link.unlink(missing_ok=True)
        escape_link.symlink_to(outside_package)
        try:
            status = run([str(executable), "alwaysactive"], cwd=Path(working_directory), env=runtime_env)
        finally:
            escape_link.unlink(missing_ok=True)
    if status == 0:
        print("Elisa cooked mesh rendered by Wicked; path rejection, handle validation, and cleanup passed.")
    return status


if __name__ == "__main__":
    raise SystemExit(main())
