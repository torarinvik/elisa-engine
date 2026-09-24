#!/usr/bin/env python3
"""Build an ordinary Elisa main and verify its scene renders through Wicked."""

from __future__ import annotations

import json
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import bundle_dependency_fixtures
import bundle_texture_fixtures
import cook_assets
import cook_gltf_asset
import cook_gltf_geometry
import cook_gltf_lod
import fbx_test_fixtures
from cook_gltf_lod_package import cook_lod_chain, parse_lod_ratios
import elisa_build_run
from elisa_package import write_geometry_package
from png_image import encode_png
import gltf_texture_self_test
import gltf_clearcoat_self_test
import gltf_mirrored_normal_fixture
import gltf_skin_self_test
import gltf_morph_self_test
import gltf_scene_self_test
import packaged_maze_smoke
import geometry_subset_cases

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
    basisu_transcoder = ROOT / "dependencies/basisu/transcoder"
    required = [
        wicked_source / "wiApplication.h", wicked_source / "wiAppleHelper.mm",
        wicked_source / "wiInput_Apple.mm", wicked_source / "shaders",
        wicked_source / "libdxcompiler.dylib", libraries / "libWickedEngine.a",
        libraries / "libJolt.a", utility / "libUtility.a",
        utility / "FAudio/libFAudio.a", libraries / "LUA/libLUA.a",
        sdl_include / "SDL3/SDL.h", sdl_library / "libSDL3.dylib",
        basisu_transcoder / "basisu_transcoder.h", basisu_transcoder / "basisu_transcoder.cpp",
    ]
    missing = [str(path) for path in required if not path.exists()]
    if missing:
        print("Missing render scene smoke dependency:\n  " + "\n  ".join(missing), file=sys.stderr)
        return 2

    build = ROOT / "build"
    build.mkdir(exist_ok=True)
    render_only = os.environ.get("ELISA_RENDER_SCENE_RENDER_ONLY") == "1"
    if render_only:
        (build / "cooked").mkdir(parents=True, exist_ok=True)
    if not render_only:
        ktx2_status = run([sys.executable, str(ROOT / "scripts/basisu_probe.py")])
        if ktx2_status != 0:
            return ktx2_status
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
        subset_directory = build / "cooked/subsets"
        subset_directory.mkdir(parents=True, exist_ok=True)
        fbx_material_source = subset_directory / "two-material-mesh.fbx"
        (subset_directory / "fbx-albedo.png").write_bytes(encode_png(2, 1,
            bytes((220, 80, 40, 255, 40, 80, 220, 255))))
        (subset_directory / "fbx-roughness.png").write_bytes(encode_png(2, 1,
            bytes((64, 1, 2, 255, 128, 3, 4, 255))))
        (subset_directory / "fbx-metalness.png").write_bytes(encode_png(2, 1,
            bytes((200, 5, 6, 255, 20, 7, 8, 255))))
        fbx_test_fixtures.write_two_material_mesh(fbx_material_source, "fbx-albedo.png",
            "fbx-roughness.png", "fbx-metalness.png")
        subset_status = run([
            sys.executable, str(ROOT / "scripts/cook_fbx_asset.py"), str(fbx_material_source),
            "--asset-path", "test/fixtures/two-material-mesh.fbx",
            "--output", str(subset_directory / "fbx-two-material.elpk"),
        ])
        if subset_status != 0:
            return subset_status
        fbx_scene_source = subset_directory / "two-mesh-scene.fbx"
        fbx_test_fixtures.write_two_mesh_scene(fbx_scene_source)
        subset_status = run([
            sys.executable, str(ROOT / "scripts/cook_fbx_asset.py"), str(fbx_scene_source),
            "--asset-path", "test/fixtures/two-mesh-scene.fbx",
            "--output", str(subset_directory / "fbx-all-meshes.pkg"), "--all-meshes",
        ])
        if subset_status != 0:
            return subset_status
        fbx_cutout_source = subset_directory / "two-material-cutout.fbx"
        (subset_directory / "fbx-cutout.png").write_bytes(encode_png(2, 1,
            bytes((220, 80, 40, 0, 40, 80, 220, 255))))
        fbx_test_fixtures.write_two_material_mesh(fbx_cutout_source, "fbx-cutout.png",
            transparency_factor=0.0)
        subset_status = run([
            sys.executable, str(ROOT / "scripts/cook_fbx_asset.py"), str(fbx_cutout_source),
            "--asset-path", "test/fixtures/two-material-cutout.fbx",
            "--output", str(subset_directory / "fbx-cutout.elpk"),
        ])
        if subset_status != 0:
            return subset_status
        # Check the FBX all-mesh package through the same sanitized production
        # loader used for the glTF placement and subset fixtures. Also validate
        # inferred cutout mode against the packaged base-color image.
        subset_status = run([sys.executable, str(ROOT / "scripts/test_geometry_subsets.py"),
            "--extra-package", str(subset_directory / "fbx-all-meshes.pkg"),
            "--fbx-cutout-package", str(subset_directory / "fbx-cutout.elpk")])
        if subset_status != 0:
            return subset_status
        subset_status = run([
            sys.executable, str(ROOT / "scripts/cook_gltf_asset.py"),
            str(ROOT / "test/fixtures/multi_material_panel.gltf"),
            "--asset-path", "test/fixtures/multi_material_panel.gltf",
            "--generate-lightmap-uv", "--lightmap-resolution", "128", "--lightmap-padding", "4",
            "--output", str(subset_directory / "uv1.pkg"),
        ])
        if subset_status != 0:
            return subset_status
        lod_source = cook_gltf_lod.write_test_source_with_unused_vertices(subset_directory)
        cook_lod_chain(lod_source, "test/fixtures/runtime_lod.gltf",
            subset_directory / "runtime_lod.pkg", parse_lod_ratios("0.5,0.25"))
        subset_status = run([
            sys.executable, str(ROOT / "scripts/cook_gltf_asset.py"),
            str(ROOT / "test/fixtures/multi_material_panel.gltf"),
            "--asset-path", "test/fixtures/multi_material_panel.gltf",
            "--output", str(subset_directory / "panel.elpk"),
        ])
        if subset_status != 0:
            return subset_status
        (subset_directory / "skinned.pkg").write_bytes(geometry_subset_cases.strip_package(
            2, [(0, 3, 0), (3, 3, 1)], 2, skinned=True))
        gltf_skin_self_test.write_package(subset_directory / "morphed-skinned.pkg", second_animation=True)
        gltf_skin_self_test.write_root_motion_package(
            subset_directory / "root-motion-skinned.pkg")
        gltf_skin_self_test.write_separate_root_package(
            subset_directory / "separate-root-skinned.pkg")
        gltf_skin_self_test.write_multi_skin_package(
            subset_directory / "multi-skin-panel.pkg")
        gltf_skin_self_test.write_mixed_skin_package(
            subset_directory / "mixed-skin-panel.pkg")
        gltf_morph_self_test.write_animated_package(
            subset_directory / "animated-morph-panel.pkg")
        gltf_scene_self_test.write_package(subset_directory / "scene-metadata.pkg")
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
        clearcoat_document = gltf_texture_self_test.textured_panel()
        gltf_clearcoat_self_test.clearcoat_material(clearcoat_document)
        clearcoat_source = subset_directory / "clearcoat_panel.gltf"
        clearcoat_source.write_text(json.dumps(clearcoat_document, indent=2) + "\n", encoding="utf-8")
        clearcoat_package, clearcoat_result = cook_gltf_geometry.cook_geometry_package(
            clearcoat_source, "test/fixtures/clearcoat_panel.gltf", subset_directory / "clearcoat.pkg",
            allow_textures=True)
        write_geometry_package(subset_directory / "clearcoat.elpk", clearcoat_package.read_bytes(),
            clearcoat_result["images"])
        fixture_status = run([sys.executable, str(ROOT / "scripts/gltf_mirrored_normal_fixture.py"),
            "--write-fixture"])
        if fixture_status != 0:
            return fixture_status
        mirrored_normal = subset_directory / "mirrored_normal.elpk"
        fixture_status = run([sys.executable, str(ROOT / "scripts/cook_gltf_asset.py"),
            str(gltf_mirrored_normal_fixture.OUTPUT), "--asset-path",
            "test/fixtures/mirrored_normal_panel.gltf", "--output", str(mirrored_normal)])
        if fixture_status != 0:
            return fixture_status

    if render_only:
        # The native test rewrites this bundle while checking checksum
        # rejection. Restore the source copy before every render-only run so
        # reruns don't start with the already-mutated variant from a prior run.
        subset_directory = build / "cooked/subsets"
        textured = subset_directory / "textured.elpk"
        clearcoat = subset_directory / "clearcoat.elpk"
        rewrite = subset_directory / "textured_rewrite.elpk"
        if not textured.is_file() or not clearcoat.is_file() or not rewrite.is_file():
            print("Render-only mode requires the cooked textured-panel fixtures", file=sys.stderr)
            return 2
        shutil.copyfile(textured, rewrite)
        mirrored_normal = subset_directory / "mirrored_normal.elpk"
        if not mirrored_normal.is_file():
            fixture_status = run([sys.executable, str(ROOT / "scripts/cook_gltf_asset.py"),
                str(gltf_mirrored_normal_fixture.OUTPUT), "--asset-path",
                "test/fixtures/mirrored_normal_panel.gltf", "--output", str(mirrored_normal)])
            if fixture_status != 0:
                return fixture_status

    compiler = os.environ.get("ELISA_COMPILER_BIN", "elisac-stage1")
    cxx = os.environ.get("CXX", "clang++")
    archive = build / "render-scene-native-smoke.a"
    executable = build / "render-scene-native-smoke"
    native_main = Path(os.environ.get(
        "ELISA_RENDER_SCENE_NATIVE_MAIN",
        ROOT / "test/render_scene_native_main.elisa",
    )).resolve()
    if not native_main.is_file():
        print(f"native smoke main does not exist: {native_main}", file=sys.stderr)
        return 2
    status = run([
        compiler, "-emit", "c-archive", "-o", str(archive),
        str(native_main),
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
        "-I", str(build), "-I", str(ROOT / "native"), "-I", str(ROOT / "dependencies/meshoptimizer"),
        "-I", str(wicked_source),
        "-I", str(utility), "-I", str(wicked_source / "Utility/metal"),
        "-I", str(wicked_source / "Utility/DirectXMath"),
        "-I", str(sdl_include), "-I", str(sdl_include / "SDL3"),
        "-I", str(brew_include), "-I", str(brew_include / "freetype2"),
        "-I", str(brew_include / "harfbuzz"), "-I", str(ROOT / "dependencies/miniaudio"),
        "-I", str(basisu_transcoder),
        str(ROOT / "native/application_abi.cpp"),
        str(ROOT / "native/render_scene_abi.cpp"),
        str(ROOT / "native/meshopt_stream_codec.cpp"),
        str(ROOT / "dependencies/meshoptimizer/indexcodec.cpp"),
        str(ROOT / "dependencies/meshoptimizer/vertexcodec.cpp"),
        str(ROOT / "native/elisa_native_fallbacks.cpp"),
        str(ROOT / "native/audio_service_abi.cpp"),
        str(ROOT / "native/miniaudio_implementation.cpp"),
        str(ROOT / "native/physics_service_abi.cpp"),
        str(ROOT / "native/physics_shape_service_abi.cpp"),
        str(basisu_transcoder / "basisu_transcoder.cpp"),
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
    fine_lod_capture = build / "render-scene-lod-fine.png"
    coarse_lod_capture = build / "render-scene-lod-coarse.png"
    mirrored_normal_capture = build / "render-scene-mirrored-normal.png"
    mirrored_normal_no_occlusion_capture = build / "render-scene-mirrored-normal-no-occlusion.png"
    clearcoat_baseline_capture = build / "render-scene-clearcoat-baseline.png"
    clearcoat_coated_capture = build / "render-scene-clearcoat-coated.png"
    point_light_left_capture = build / "render-scene-point-light-left.png"
    point_light_right_capture = build / "render-scene-point-light-right.png"
    shadows_disabled_capture = build / "render-scene-shadows-disabled.png"
    shadows_enabled_capture = build / "render-scene-shadows-enabled.png"
    lighting_outdoor_capture = build / "render-scene-lighting-outdoor.png"
    lighting_indoor_capture = build / "render-scene-lighting-indoor.png"
    mirrored_normal_capture.unlink(missing_ok=True)
    mirrored_normal_no_occlusion_capture.unlink(missing_ok=True)
    shadows_disabled_capture.unlink(missing_ok=True)
    shadows_enabled_capture.unlink(missing_ok=True)
    lighting_outdoor_capture.unlink(missing_ok=True)
    lighting_indoor_capture.unlink(missing_ok=True)
    runtime_env["ELISA_MIRRORED_NORMAL_CAPTURE"] = str(mirrored_normal_capture)
    runtime_env["ELISA_MIRRORED_NORMAL_NO_OCCLUSION_CAPTURE"] = str(
        mirrored_normal_no_occlusion_capture)
    runtime_env["ELISA_CLEARCOAT_BASELINE_CAPTURE"] = str(clearcoat_baseline_capture)
    runtime_env["ELISA_CLEARCOAT_COATED_CAPTURE"] = str(clearcoat_coated_capture)
    runtime_env["ELISA_POINT_LIGHT_LEFT_CAPTURE"] = str(point_light_left_capture)
    runtime_env["ELISA_POINT_LIGHT_RIGHT_CAPTURE"] = str(point_light_right_capture)
    runtime_env["ELISA_SHADOWS_DISABLED_CAPTURE"] = str(shadows_disabled_capture)
    runtime_env["ELISA_SHADOWS_ENABLED_CAPTURE"] = str(shadows_enabled_capture)
    runtime_env["ELISA_LIGHTING_OUTDOOR_CAPTURE"] = str(lighting_outdoor_capture)
    runtime_env["ELISA_LIGHTING_INDOOR_CAPTURE"] = str(lighting_indoor_capture)
    lod_fixture_available = (build / "cooked/subsets/runtime_lod.lod.json").is_file()
    capture_lod_quality = not render_only or lod_fixture_available
    if capture_lod_quality:
        fine_lod_capture.unlink(missing_ok=True)
        coarse_lod_capture.unlink(missing_ok=True)
        runtime_env["ELISA_LOD_FINE_CAPTURE"] = str(fine_lod_capture)
        runtime_env["ELISA_LOD_COARSE_CAPTURE"] = str(coarse_lod_capture)
    with tempfile.TemporaryDirectory(prefix="Elisa render scene smoke ") as working_directory:
        if render_only:
            status = run([str(executable), "alwaysactive"], cwd=Path(working_directory), env=runtime_env)
        else:
            with tempfile.TemporaryDirectory(prefix="Elisa cooked mesh path escape ") as outside_directory:
                outside_package = Path(outside_directory) / "outside.pkg"
                outside_package.write_text("format=elisa-cooked-v2\n", encoding="ascii")
                escape_link = build / "cooked/render-scene-outside-link.pkg"
                escape_link.unlink(missing_ok=True)
                escape_link.symlink_to(outside_package)
                texture_link = bundle_texture_fixtures.write_fixtures(build / "cooked", Path(outside_directory),
                    build / "cooked/maze_tile_tex.ktx2")
                dependency_link = bundle_dependency_fixtures.write_fixtures(build / "cooked", Path(outside_directory))
                try:
                    status = run([str(executable), "alwaysactive"], cwd=Path(working_directory), env=runtime_env)
                finally:
                    escape_link.unlink(missing_ok=True)
                    texture_link.unlink(missing_ok=True)
                    dependency_link.unlink(missing_ok=True)
    if status == 0 and capture_lod_quality:
        quality_status = run([sys.executable, str(ROOT / "scripts/compare_renders.py"),
            "lod-quality", str(fine_lod_capture), str(coarse_lod_capture)])
        if quality_status != 0:
            print("Same-camera LOD image quality comparison failed.", file=sys.stderr)
            return quality_status
    if status == 0:
        normal_status = run([sys.executable, str(ROOT / "scripts/compare_mirrored_normal.py"),
            str(mirrored_normal_capture)])
        if normal_status != 0:
            print("Mirrored-UV normal-map image comparison failed.", file=sys.stderr)
            return normal_status
    if status == 0:
        occlusion_status = run([sys.executable, str(ROOT / "scripts/compare_occlusion_strength.py"),
            str(mirrored_normal_capture), str(mirrored_normal_no_occlusion_capture)])
        if occlusion_status != 0:
            print("Occlusion-strength image comparison failed.", file=sys.stderr)
            return occlusion_status
    if status == 0:
        if render_only:
            print("Elisa UI, sun shadows, indoor/outdoor lighting, point lights and authored glTF material references rendered by Wicked; visual checks passed.")
            return 0
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
