#!/usr/bin/env python3
"""Build the engine-owned bounded FBX importer and exercise its fixtures."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

import fbx_test_fixtures


ROOT = Path(__file__).resolve().parents[1]


def run(command: list[str]) -> None:
    print("+", shlex.join(command), flush=True)
    subprocess.run(command, cwd=ROOT, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assets-root", type=Path,
        help="also validate the supplied WallGame FBX files under this assets directory")
    parser.add_argument("--cooked-skin", type=Path,
        help="load and validate a cooked skinned-mesh package")
    args = parser.parse_args()
    dependency = ROOT / "dependencies/ufbx"
    source = dependency / "ufbx.c"
    header = dependency / "ufbx.h"
    if not source.is_file() or not header.is_file():
        print("Missing pinned ufbx sources; run python3 scripts/fetch_dependencies.py.",
            file=sys.stderr)
        return 2

    cc = os.environ.get("CC", "cc")
    cxx = os.environ.get("CXX", "c++")
    with tempfile.TemporaryDirectory(prefix="Elisa FBX import ") as temporary:
        build = Path(temporary)
        ufbx_object = build / "ufbx.o"
        test_binary = build / "fbx-asset-import-test"
        run([cc, "-std=c99", "-O2", "-I", str(dependency), "-c", str(source), "-o", str(ufbx_object)])
        compile_command = [cxx, "-std=c++17", "-O2", "-I", str(dependency), "-I", str(ROOT / "native"),
            str(ROOT / "native/fbx_asset_import_test.cpp"), str(ufbx_object), "-o", str(test_binary)]
        if args.cooked_skin is not None:
            compile_command.insert(1, "-DELISA_TEST_COOKED_SKIN")
            zstd_flags = subprocess.check_output(["pkg-config", "--cflags", "--libs", "libzstd"],
                cwd=ROOT, text=True).split()
            compile_command.extend(zstd_flags)
        run(compile_command)
        run([str(test_binary), "--fixture", str(ROOT / "test/fixtures/fbx_triangle.fbx")])
        multi_mesh_fixture = build / "two-mesh-scene.fbx"
        fbx_test_fixtures.write_two_mesh_scene(multi_mesh_fixture)
        run([str(test_binary), "--mesh-selection", str(multi_mesh_fixture)])
        material_fixture = build / "two-material-mesh.fbx"
        fbx_test_fixtures.write_two_material_mesh(material_fixture)
        run([str(test_binary), "--material-subsets", str(material_fixture)])
        texture_fixture = build / "two-material-texture.fbx"
        fbx_test_fixtures.write_two_material_mesh(texture_fixture, "albedo.png")
        run([str(test_binary), "--material-texture", str(texture_fixture)])
        surface_texture_fixture = build / "two-material-surface-texture.fbx"
        fbx_test_fixtures.write_two_material_mesh(surface_texture_fixture,
            roughness_texture="roughness.png", metalness_texture="metalness.png")
        run([str(test_binary), "--material-surface-texture", str(surface_texture_fixture)])
        if args.assets_root is not None:
            asset_root = args.assets_root.expanduser().resolve()
            run([str(test_binary), "--assets-root", str(asset_root)])
        if args.cooked_skin is not None:
            package = args.cooked_skin.expanduser().resolve(strict=True)
            run([str(test_binary), "--cooked-skin", str(package)])
    print("Bounded FBX import tests passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
