#!/usr/bin/env python3
"""Check cooked-geometry subset, slot material and slot texture records with
the production C++ loader.

The cooked multi-material panel, the baked node hierarchy panel, the bundled
textured panel, the legacy maze tile, and synthetic packages must load with
exactly the expected subsets, slot materials, and image sections. Packages
whose subsets leave a gap, overlap, split a triangle, name a missing slot,
exceed a bound, or are malformed on skinned geometry must be rejected for
that reason. Valid subsets and slot materials are accepted on skinned
geometry. Slot materials that are malformed, miscounted, or out of
range, and slot textures that are malformed, name a bad or missing section,
leave an image unsampled, or sit outside a bundle. The loader runs under
AddressSanitizer and UndefinedBehaviorSanitizer.
"""

from __future__ import annotations

import base64
import argparse
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

from png_image import encode_png
from geometry_subset_manifest import manifest_line
from geometry_subset_cases import cases

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--extra-package", type=Path,
        help="also load and compare placement records from a cooked .pkg")
    parser.add_argument("--fbx-cutout-package", type=Path,
        help="also load a cooked FBX ELPK cutout material and its base-color image")
    options = parser.parse_args()
    compiler = os.environ.get("CXX", "c++")
    if shutil.which(compiler) is None:
        print(f"C++ compiler is unavailable: {compiler}", file=sys.stderr)
        return 2
    with tempfile.TemporaryDirectory(prefix="elisa-geometry-subsets-") as temporary:
        directory = Path(temporary)
        manifest = []
        test_cases = cases(directory)
        if options.extra_package is not None:
            package_bytes = options.extra_package.read_bytes()
            package_fields = dict(line.split("=", 1)
                for line in package_bytes.decode("ascii").splitlines() if "=" in line)
            subset_records = list(struct.iter_unpack("<3I",
                base64.b64decode(package_fields["subsets_b64"], validate=True)))
            placement_records = list(struct.iter_unpack("<8I12f",
                base64.b64decode(package_fields["mesh_placements_b64"], validate=True)))
            package_index_count = int(package_fields.get("indices",
                str(int(package_fields["triangles"]) * 3)))
            package_name = "extra-placement-package.pkg"
            (directory / package_name).write_bytes(package_bytes)
            test_cases.append(("accept", package_name, None,
                (package_index_count, int(package_fields["material_slots"]),
                    subset_records, "mesh_placements", placement_records)))
        if options.fbx_cutout_package is not None:
            cutout_image = encode_png(2, 1, bytes((220, 80, 40, 0, 40, 80, 220, 255)))
            cutout_materials = [
                (0.1600000113248825, 0.320000022649765, 0.48000001907348633, 1.0, 0.0,
                    0.4343145787715912, 0.30000001192092896, 0.20000000298023224,
                    0.10000000149011612, 0.5,
                    1, 0, 1, 0, 0, 0, 0),
                (0.699999988079071, 0.20000000298023224, 0.10000000149011612, 1.0,
                    0.0, 0.6535898447036743, 0.0, 0.0, 0.0, 0.5,
                    0, 0, 0, 0, 0, 0, 0),
            ]
            package_name = "fbx-cutout-material.elpk"
            shutil.copyfile(options.fbx_cutout_package, directory / package_name)
            test_cases.append(("accept", package_name, None,
                (6, 2, [(0, 3, 0), (3, 3, 1)], cutout_materials,
                    [("fbx_image_0", zlib.crc32(cutout_image) & 0xFFFFFFFF)],
                    "slot_names", ["First", "Second"])))
        for verdict, name, package, expectation in test_cases:
            if package is not None:
                (directory / name).write_bytes(package)
            manifest.append(manifest_line(directory, verdict, name, expectation))
        (directory / "cases.tsv").write_text("\n".join(manifest) + "\n", encoding="utf-8")
        executable = directory / "geometry-subset-test"
        command = [compiler, "-std=c++17", "-O1", "-g", "-fno-omit-frame-pointer",
            "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
            "-I", str(ROOT / "native"), "-I", str(ROOT / "dependencies/meshoptimizer"),
            "-I", "/opt/homebrew/include",
            "-L", os.environ.get("ZSTD_LIBRARY_DIR", "/opt/homebrew/lib"),
            str(ROOT / "native/geometry_subset_test.cpp"),
            str(ROOT / "native/meshopt_stream_codec.cpp"),
            str(ROOT / "dependencies/meshoptimizer/indexcodec.cpp"),
            str(ROOT / "dependencies/meshoptimizer/vertexcodec.cpp"),
            "-lzstd", "-o", str(executable)]
        built = subprocess.run(command, capture_output=True, text=True, check=False)
        if built.returncode != 0:
            print(built.stderr or built.stdout, file=sys.stderr)
            return built.returncode
        checked = subprocess.run([str(executable), str(directory / "cases.tsv")],
            capture_output=True, text=True, check=False)
        sys.stdout.write(checked.stdout)
        sys.stderr.write(checked.stderr)
        if checked.returncode != 0:
            print(f"geometry subset loader test failed with exit {checked.returncode}", file=sys.stderr)
            return 1
        if f"{len(manifest)} cases, 0 failed" not in checked.stdout:
            print("geometry subset loader test did not run every case", file=sys.stderr)
            return 1
        if subprocess.run([sys.executable, str(ROOT / "scripts/test_lod_manifest.py")]).returncode != 0:
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
