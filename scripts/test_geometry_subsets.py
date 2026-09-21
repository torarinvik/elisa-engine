#!/usr/bin/env python3
"""Check cooked-geometry subset records with the production C++ loader.

The cooked multi-material panel, the legacy maze tile, and synthetic packages
must load with exactly the expected subsets. Packages whose subsets leave a
gap, overlap, split a triangle, name a missing slot, exceed a bound, or appear
on skinned geometry must be rejected for that reason. The loader runs under
AddressSanitizer and UndefinedBehaviorSanitizer.
"""

from __future__ import annotations

import base64
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

import cook_gltf_geometry
from elisa_package import write_geometry_package


ROOT = Path(__file__).resolve().parents[1]
PARTITION = "subsets do not partition the index stream"
RECORDS = "invalid cooked geometry subset records"
MISSING_SLOT = "subset names a missing material slot"
SKINNED = "skinned cooked geometry cannot declare material subsets"


def encoded(format_string: str, values) -> str:
    return base64.b64encode(struct.pack(format_string, *values)).decode("ascii")


def strip_package(triangles: int, subsets=None, slots: int | None = None,
        record_count: int | None = None, stride: str = "12", omit: tuple[str, ...] = (),
        skinned: bool = False) -> bytes:
    """A row of `triangles` separate triangles with optional subset records."""
    vertices = triangles * 3
    positions = []
    for triangle in range(triangles):
        positions += [float(triangle), 0.0, 0.0, triangle + 1.0, 0.0, 0.0, float(triangle), 1.0, 0.0]
    lines = [
        "format=elisa-cooked-v2", "source=test/strip.gltf", "source_sha256=" + "0" * 64,
        f"triangles={triangles}", f"positions={vertices}", f"indices={vertices}",
        "position_stride=12", "normal_stride=12", "uv_stride=8", "index_stride=4",
    ]
    if subsets is not None:
        records = {
            "material_slots": f"material_slots={slots}",
            "subset_count": f"subset_count={len(subsets) if record_count is None else record_count}",
            "subset_stride": f"subset_stride={stride}",
            "subsets_b64": "subsets_b64=" + encoded(f"<{len(subsets) * 3}I",
                [value for subset in subsets for value in subset]),
        }
        lines += [line for key, line in records.items() if key not in omit]
    lines += [
        "positions_b64=" + encoded(f"<{vertices * 3}f", positions),
        "normals_b64=" + encoded(f"<{vertices * 3}f", (0.0, 0.0, 1.0) * vertices),
        "uvs_b64=" + encoded(f"<{vertices * 2}f", (0.0,) * (vertices * 2)),
        "indices_b64=" + encoded(f"<{vertices}I", range(vertices)),
    ]
    if skinned:
        name = b"root"
        lines += [
            "skin_bones=1", "skin_indices_stride=16", "skin_weights_stride=16",
            "skin_indices_b64=" + encoded(f"<{vertices * 4}I", (0,) * (vertices * 4)),
            "skin_weights_b64=" + encoded(f"<{vertices * 4}f", (1.0, 0.0, 0.0, 0.0) * vertices),
            "skin_names_b64=" + base64.b64encode(struct.pack("<I", len(name)) + name).decode("ascii"),
        ]
    return ("\n".join(lines) + "\n").encode("ascii")


def cases(directory: Path) -> list[tuple]:
    """Return (verdict, file name, package bytes or None, expectation) tuples."""
    panel_source = ROOT / "test/fixtures/multi_material_panel.gltf"
    panel_path, _ = cook_gltf_geometry.cook_geometry_package(
        panel_source, "test/fixtures/multi_material_panel.gltf", directory / "panel.pkg")
    panel = panel_path.read_bytes()
    write_geometry_package(directory / "panel.elpk", panel)
    tile_path, _ = cook_gltf_geometry.cook_geometry_package(
        ROOT / "examples/maze/assets/maze_tile.gltf", "assets/maze_tile.gltf", directory / "tile.pkg")
    panel_subsets = [(0, 6, 1), (6, 6, 0), (12, 6, 1)]
    sixteen = [(index * 3, 3, index) for index in range(16)]
    seventeen = [(index * 3, 3, index % 16) for index in range(17)]
    return [
        ("accept", "panel.pkg", None, (18, 2, panel_subsets)),
        ("accept", "panel.elpk", None, (18, 2, panel_subsets)),
        ("accept", tile_path.name, None, (36, 1, [(0, 36, 0)])),
        ("accept", "legacy.pkg", strip_package(2), (6, 1, [(0, 6, 0)])),
        ("accept", "explicit-single.pkg", strip_package(2, [(0, 6, 0)], 1), (6, 1, [(0, 6, 0)])),
        ("accept", "unused-slot.pkg", strip_package(2, [(0, 3, 2), (3, 3, 0)], 3),
            (6, 3, [(0, 3, 2), (3, 3, 0)])),
        ("accept", "sixteen.pkg", strip_package(16, sixteen, 16), (48, 16, sixteen)),
        ("accept", "skinned.pkg", strip_package(2, skinned=True), (6, 1, [(0, 6, 0)])),
        ("reject", "gap.pkg", strip_package(3, [(0, 3, 0), (6, 3, 0)], 1), PARTITION),
        ("reject", "overlap.pkg", strip_package(3, [(0, 6, 0), (3, 6, 1)], 2), PARTITION),
        ("reject", "split-triangle.pkg", strip_package(2, [(0, 4, 0), (4, 2, 1)], 2), PARTITION),
        ("reject", "empty-subset.pkg", strip_package(2, [(0, 6, 0), (6, 0, 1)], 2), PARTITION),
        ("reject", "missing-slot.pkg", strip_package(2, [(0, 3, 0), (3, 3, 2)], 2), MISSING_SLOT),
        ("reject", "short.pkg", strip_package(3, [(0, 3, 0), (3, 3, 1)], 2), PARTITION),
        ("reject", "overrun.pkg", strip_package(2, [(0, 3, 0), (3, 6, 1)], 2), PARTITION),
        ("reject", "wrapping.pkg", strip_package(2, [(0, 3, 0), (3, 0xFFFFFFFF, 1)], 2), PARTITION),
        # Each subset wraps uint32 once. Three wraps land back on an exact
        # partition of 6 indices, so only the per-subset bound rejects it.
        ("reject", "triple-wrap.pkg", strip_package(2, [(0, 0xC0000000, 0), (0xC0000000, 0xC0000000, 0),
            (0x80000000, 0xC0000000, 0), (0x40000000, 0xC0000006, 0)], 1), PARTITION),
        ("reject", "seventeen.pkg", strip_package(17, seventeen, 16), RECORDS),
        ("reject", "seventeen-slots.pkg", strip_package(2, [(0, 6, 16)], 17), RECORDS),
        ("reject", "zero-slots.pkg", strip_package(2, [(0, 6, 0)], 0), RECORDS),
        ("reject", "count-mismatch.pkg", strip_package(2, [(0, 3, 0), (3, 3, 1)], 2, record_count=3), RECORDS),
        ("reject", "stride.pkg", strip_package(2, [(0, 6, 0)], 1, stride="16"), RECORDS),
        ("reject", "no-slots.pkg", strip_package(2, [(0, 6, 0)], 1, omit=("material_slots",)), RECORDS),
        ("reject", "no-records.pkg", strip_package(2, [(0, 6, 0)], 1, omit=("subsets_b64",)), RECORDS),
        ("reject", "skinned-subsets.pkg", strip_package(2, [(0, 3, 0), (3, 3, 1)], 2, skinned=True), SKINNED),
        ("reject", "skinned-single.pkg", strip_package(2, [(0, 6, 0)], 1, skinned=True), SKINNED),
    ]


def main() -> int:
    compiler = os.environ.get("CXX", "c++")
    if shutil.which(compiler) is None:
        print(f"C++ compiler is unavailable: {compiler}", file=sys.stderr)
        return 2
    with tempfile.TemporaryDirectory(prefix="elisa-geometry-subsets-") as temporary:
        directory = Path(temporary)
        manifest = []
        for verdict, name, package, expectation in cases(directory):
            if package is not None:
                (directory / name).write_bytes(package)
            fields = [verdict, str(directory / name)]
            if verdict == "accept":
                index_count, slots, subsets = expectation
                fields += [str(index_count), str(slots)] + [str(value) for subset in subsets for value in subset]
            else:
                fields.append(expectation)
            manifest.append("\t".join(fields))
        (directory / "cases.tsv").write_text("\n".join(manifest) + "\n", encoding="utf-8")
        executable = directory / "geometry-subset-test"
        command = [compiler, "-std=c++17", "-O1", "-g", "-fno-omit-frame-pointer",
            "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
            "-I", str(ROOT / "native"), "-I", "/opt/homebrew/include",
            "-L", os.environ.get("ZSTD_LIBRARY_DIR", "/opt/homebrew/lib"),
            str(ROOT / "native/geometry_subset_test.cpp"), "-lzstd", "-o", str(executable)]
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
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
