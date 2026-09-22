#!/usr/bin/env python3
"""Build an ordinary Elisa main and verify its scene renders through Wicked."""

from __future__ import annotations

import json
import os
import shutil
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path

import bundle_dependency_fixtures
import bundle_texture_fixtures
import cook_assets
import cook_gltf_asset
import cook_gltf_geometry
import elisa_build_run
from elisa_package import write_geometry_package
import gltf_texture_self_test
import packaged_maze_smoke
import test_geometry_subsets

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
    cooked_mesh = build / "cooked/render-scene-triangle.elpk"
    status = run([
        sys.executable, str(ROOT / "scripts/cook_fbx_asset.py"),
        str(ROOT / "test/fixtures/fbx_triangle.fbx"),
        "--asset-path", "test/fixtures/fbx_triangle.fbx",
        "--output", str(cooked_mesh),
    ])
    if status != 0:
        return status
    package_status = run([
        sys.executable, str(ROOT / "scripts/test_elisa_package.py"), str(cooked_mesh),
    ])
    if package_status != 0:
        return package_status
    # The maze snapshot test registers the same tile and texture bundles that
    # the project runner cooks from elisa.project.json.
    maze_project = ROOT / "examples/maze"
    maze_config = json.loads((maze_project / "elisa.project.json").read_text(encoding="utf-8"))
    maze_status = elisa_build_run.cook_declared_assets(maze_project, maze_config)
    if maze_status != 0:
        return maze_status
    normal_map = build / "cooked/render-scene-normal.png"
    shutil.copyfile(ROOT / "backends/coordinate_reference.png", normal_map)
    # The material-subset test draws the three-strip glTF panel and a skinned
    # strip split across two material slots.
    subset_status = run([sys.executable, str(ROOT / "scripts/test_geometry_subsets.py")])
    if subset_status != 0:
        return subset_status
    subset_directory = build / "cooked/subsets"
    subset_directory.mkdir(parents=True, exist_ok=True)
    subset_status = run([
        sys.executable, str(ROOT / "scripts/cook_gltf_asset.py"),
        str(ROOT / "test/fixtures/multi_material_panel.gltf"),
        "--asset-path", "test/fixtures/multi_material_panel.gltf",
        "--output", str(subset_directory / "panel.elpk"),
    ])
    if subset_status != 0:
        return subset_status
    (subset_directory / "skinned.pkg").write_bytes(test_geometry_subsets.strip_package(
        2, [(0, 3, 0), (3, 3, 1)], 2, skinned=True))
    # The cooked-material test also registers a variant whose center slot is
    # blended, single-sided glass. Its buffer is embedded, so the copy cooks
    # anywhere.
    glass = cook_assets.read_gltf((ROOT / "test/fixtures/multi_material_panel.gltf").read_bytes())
    cook_gltf_asset.make_glass(glass)
    glass_source = subset_directory / "glass_panel.gltf"
    glass_source.write_text(json.dumps(glass, indent=2) + "\n", encoding="utf-8")
    cook_gltf_geometry.cook_geometry_package(
        glass_source, "build/cooked/subsets/glass_panel.gltf", subset_directory / "glass_panel.pkg")
    # The node-hierarchy test draws a static glTF scene baked into one mesh.
    subset_status = run([
        sys.executable, str(ROOT / "scripts/cook_gltf_asset.py"),
        str(ROOT / "test/fixtures/node_hierarchy_panel.gltf"),
        "--asset-path", "test/fixtures/node_hierarchy_panel.gltf",
        "--output", str(subset_directory / "hierarchy.elpk"),
    ])
    if subset_status != 0:
        return subset_status
    # The cooked-texture test draws the textured panel from its bundle. The
    # variant swaps the first two images; the test rewrites a copy of the
    # bundle into it after loading the copy's mesh.
    textured = subset_directory / "textured.elpk"
    subset_status = run([
        sys.executable, str(ROOT / "scripts/cook_gltf_asset.py"),
        str(gltf_texture_self_test.SOURCE), "--asset-path", gltf_texture_self_test.ASSET_PATH,
        "--output", str(textured),
    ])
    if subset_status != 0:
        return subset_status
    shutil.copyfile(textured, subset_directory / "textured_rewrite.elpk")
    textured_package, _ = cook_gltf_geometry.cook_geometry_package(gltf_texture_self_test.SOURCE,
        gltf_texture_self_test.ASSET_PATH, subset_directory / "textured.pkg", allow_textures=True)
    variant = dict(gltf_texture_self_test.SECTIONS)
    variant["image_0"], variant["image_1"] = variant["image_1"], variant["image_0"]
    write_geometry_package(subset_directory / "textured_variant.elpk", textured_package.read_bytes(), variant)

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
        "-I", str(brew_include / "harfbuzz"), "-I", str(ROOT / "dependencies/miniaudio"),
        str(ROOT / "native/application_abi.cpp"),
        str(ROOT / "native/render_scene_abi.cpp"),
        str(ROOT / "native/elisa_native_fallbacks.cpp"),
        str(ROOT / "native/audio_service_abi.cpp"),
        str(ROOT / "native/miniaudio_implementation.cpp"),
        str(ROOT / "native/physics_service_abi.cpp"),
        str(ROOT / "native/physics_shape_service_abi.cpp"),
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
        texture_link = bundle_texture_fixtures.write_fixtures(build / "cooked", Path(outside_directory))
        dependency_link = bundle_dependency_fixtures.write_fixtures(build / "cooked", Path(outside_directory))
        try:
            status = run([str(executable), "alwaysactive"], cwd=Path(working_directory), env=runtime_env)
        finally:
            escape_link.unlink(missing_ok=True)
            texture_link.unlink(missing_ok=True)
            dependency_link.unlink(missing_ok=True)
    if status == 0:
        print("Elisa cooked mesh rendered by Wicked; path rejection, handle validation, and cleanup passed.")
        maze_status = run([
            sys.executable, str(ROOT / "scripts/elisa_build_run.py"), "run",
            "--project", str(ROOT / "examples/maze"),
            "--main", "native_smoke_main.elisa",
            "--output", str(build / "maze-native-smoke"),
        ], cwd=ROOT)
        if maze_status != 0:
            print("Elisa-owned native maze application smoke failed.", file=sys.stderr)
            return maze_status
        print("Elisa-owned maze world rendered through the native SDL3/Metal snapshot presenter.")
        packaged_status = packaged_maze_smoke.run(
            build / "maze-native-smoke", ROOT / "examples/maze", wicked_source / "shaders")
        if packaged_status != 0:
            print("Packaged maze smoke outside the checkout failed.", file=sys.stderr)
            return packaged_status
        print("Packaged maze ran outside the checkout; bad bundles failed asset registration.")
    return status


if __name__ == "__main__":
    raise SystemExit(main())
