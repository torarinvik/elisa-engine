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


ROOT = Path(__file__).resolve().parents[1]


def run(command: list[str]) -> None:
    print("+", shlex.join(command), flush=True)
    subprocess.run(command, cwd=ROOT, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assets-root", type=Path,
        help="also validate the supplied WallGame FBX files under this assets directory")
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
        run([cxx, "-std=c++17", "-O2", "-I", str(dependency), "-I", str(ROOT / "native"),
            str(ROOT / "native/fbx_asset_import_test.cpp"), str(ufbx_object), "-o", str(test_binary)])
        run([str(test_binary), "--fixture", str(ROOT / "test/fixtures/fbx_triangle.fbx")])
        if args.assets_root is not None:
            asset_root = args.assets_root.expanduser().resolve()
            run([str(test_binary), "--assets-root", str(asset_root)])
    print("Bounded FBX import tests passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
