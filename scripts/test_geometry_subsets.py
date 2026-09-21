#!/usr/bin/env python3
"""Check cooked-geometry subset and slot material records with the production
C++ loader.

The cooked multi-material panel, the baked node hierarchy panel, the legacy
maze tile, and synthetic packages must load with exactly the expected subsets
and slot materials. Packages whose subsets leave a gap, overlap, split a
triangle, name a missing slot, exceed a bound, or appear on skinned geometry
must be rejected for that reason, as must slot materials that are malformed,
miscounted, or out of range. The loader runs under AddressSanitizer and
UndefinedBehaviorSanitizer.
"""

from __future__ import annotations

import base64
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

import cook_assets
import cook_gltf_geometry
from elisa_package import write_geometry_package
import gltf_hierarchy_self_test


ROOT = Path(__file__).resolve().parents[1]
PARTITION = "subsets do not partition the index stream"
RECORDS = "invalid cooked geometry subset records"
MISSING_SLOT = "subset names a missing material slot"
SKINNED = "skinned cooked geometry cannot declare material subsets"
MATERIAL_RECORDS = "invalid cooked slot material records"
MATERIAL_RANGE = "cooked slot material is out of range"
# Base color, metallic, roughness, emissive, alpha cutoff, alpha mode, flags.
GLASS = (0.0, 0.0, 0.08, 0.5, 0.0, 0.9, 0.0, 0.0, 1.0, 0.5, 2, 0)
PAINT = (0.08, 0.0, 0.0, 1.0, 0.25, 0.75, 1.0, 0.0, 0.0, 0.5, 0, 1)
ZEROS = (0.0,) * 10 + (0, 0)
ONES = (1.0,) * 10 + (2, 1)
PANEL_MATERIALS = [(0.0, 0.0, 0.08, 1.0, 0.0, 0.9, 0.0, 0.0, 1.0, 0.5, 0, 1), PAINT]
# The hierarchy panel's single-sided red, green, and blue emissive strips.
HIERARCHY_MATERIALS = [(0.08, 0.0, 0.0, 1.0, 0.0, 0.9, 1.0, 0.0, 0.0, 0.5, 0, 0),
    (0.0, 0.08, 0.0, 1.0, 0.0, 0.9, 0.0, 1.0, 0.0, 0.5, 0, 0),
    (0.0, 0.0, 0.08, 1.0, 0.0, 0.9, 0.0, 0.0, 1.0, 0.5, 0, 0)]


def with_field(record: tuple, index: int, value) -> tuple:
    return record[:index] + (value,) + record[index + 1:]


def encoded(format_string: str, values) -> str:
    return base64.b64encode(struct.pack(format_string, *values)).decode("ascii")


def strip_package(triangles: int, subsets=None, slots: int | None = None,
        record_count: int | None = None, stride: str = "12", omit: tuple[str, ...] = (),
        skinned: bool = False, materials=None, material_stride: str = "48") -> bytes:
    """A row of `triangles` separate triangles with optional subset and slot
    material records."""
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
    if materials is not None:
        records = {
            "slot_material_stride": f"slot_material_stride={material_stride}",
            "slot_materials_b64": "slot_materials_b64=" + base64.b64encode(
                b"".join(struct.pack("<10f2I", *record) for record in materials)).decode("ascii"),
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
    hierarchy_source = ROOT / "test/fixtures/node_hierarchy_panel.gltf"
    cook_gltf_geometry.cook_geometry_package(
        hierarchy_source, "test/fixtures/node_hierarchy_panel.gltf", directory / "hierarchy.pkg")
    # Fifteen placements alternating red and green, then blue: the most
    # subsets a baked hierarchy may need.
    alternating = cook_assets.read_gltf(hierarchy_source.read_bytes())
    gltf_hierarchy_self_test.alternating(15)(alternating)
    (directory / "alternating.gltf").write_text(json.dumps(alternating), encoding="utf-8")
    cook_gltf_geometry.cook_geometry_package(
        directory / "alternating.gltf", "test/alternating.gltf", directory / "alternating.pkg")
    alternating_subsets = [(index * 6, 6, index % 2) for index in range(15)] + [(90, 6, 2)]
    panel_subsets = [(0, 6, 1), (6, 6, 0), (12, 6, 1)]
    sixteen = [(index * 3, 3, index) for index in range(16)]
    seventeen = [(index * 3, 3, index % 16) for index in range(17)]
    two = [(0, 3, 0), (3, 3, 1)]
    return [
        ("accept", "panel.pkg", None, (18, 2, panel_subsets, PANEL_MATERIALS)),
        ("accept", "panel.elpk", None, (18, 2, panel_subsets, PANEL_MATERIALS)),
        ("accept", tile_path.name, None, (36, 1, [(0, 36, 0)])),
        ("accept", "hierarchy.pkg", None, (24, 3, gltf_hierarchy_self_test.SUBSETS, HIERARCHY_MATERIALS)),
        ("accept", "alternating.pkg", None, (96, 3, alternating_subsets, HIERARCHY_MATERIALS)),
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
        ("accept", "materials.pkg", strip_package(2, two, 2, materials=[GLASS, PAINT]),
            (6, 2, two, [GLASS, PAINT])),
        ("accept", "single-material.pkg", strip_package(2, [(0, 6, 0)], 1, materials=[PAINT]),
            (6, 1, [(0, 6, 0)], [PAINT])),
        ("accept", "material-bounds.pkg", strip_package(2, two, 2, materials=[ZEROS, ONES]),
            (6, 2, two, [ZEROS, ONES])),
        ("reject", "materials-without-subsets.pkg", strip_package(2, materials=[PAINT]), MATERIAL_RECORDS),
        ("reject", "skinned-materials.pkg", strip_package(2, skinned=True, materials=[PAINT]), MATERIAL_RECORDS),
        ("reject", "material-stride.pkg", strip_package(2, two, 2, materials=[GLASS, PAINT],
            material_stride="44"), MATERIAL_RECORDS),
        ("reject", "material-no-stride.pkg", strip_package(2, two, 2, materials=[GLASS, PAINT],
            omit=("slot_material_stride",)), MATERIAL_RECORDS),
        ("reject", "material-no-records.pkg", strip_package(2, two, 2, materials=[GLASS, PAINT],
            omit=("slot_materials_b64",)), MATERIAL_RECORDS),
        ("reject", "material-short.pkg", strip_package(2, two, 2, materials=[GLASS]), MATERIAL_RECORDS),
        ("reject", "material-long.pkg", strip_package(2, two, 2, materials=[GLASS, PAINT, PAINT]),
            MATERIAL_RECORDS),
        ("reject", "material-above-one.pkg", strip_package(2, two, 2,
            materials=[GLASS, with_field(PAINT, 0, 1.5)]), MATERIAL_RANGE),
        ("reject", "material-negative.pkg", strip_package(2, two, 2,
            materials=[with_field(GLASS, 5, -0.25), PAINT]), MATERIAL_RANGE),
        ("reject", "material-nan.pkg", strip_package(2, two, 2,
            materials=[GLASS, with_field(PAINT, 4, float("nan"))]), MATERIAL_RANGE),
        ("reject", "material-infinite.pkg", strip_package(2, two, 2,
            materials=[GLASS, with_field(PAINT, 8, float("inf"))]), MATERIAL_RANGE),
        ("reject", "material-cutoff.pkg", strip_package(2, two, 2,
            materials=[GLASS, with_field(PAINT, 9, 1.5)]), MATERIAL_RANGE),
        ("reject", "material-mask.pkg", strip_package(2, two, 2,
            materials=[with_field(GLASS, 10, 1), PAINT]), MATERIAL_RANGE),
        ("reject", "material-mode.pkg", strip_package(2, two, 2,
            materials=[with_field(GLASS, 10, 3), PAINT]), MATERIAL_RANGE),
        ("reject", "material-flags.pkg", strip_package(2, two, 2,
            materials=[GLASS, with_field(PAINT, 11, 3)]), MATERIAL_RANGE),
    ]


def float32_text(value: float) -> str:
    return repr(struct.unpack("<f", struct.pack("<f", value))[0])


def manifest_line(directory: Path, verdict: str, name: str, expectation) -> str:
    """One loader-test manifest line; see native/geometry_subset_test.cpp."""
    fields = [verdict, str(directory / name)]
    if verdict == "reject":
        return "\t".join(fields + [expectation])
    index_count, slots, subsets, *materials = expectation
    fields += [str(index_count), str(slots)] + [str(value) for subset in subsets for value in subset]
    if materials:
        fields.append("materials")
        for record in materials[0]:
            fields += [float32_text(value) for value in record[:10]] + [str(value) for value in record[10:]]
    return "\t".join(fields)


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
            manifest.append(manifest_line(directory, verdict, name, expectation))
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
