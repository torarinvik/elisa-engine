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
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

import cook_assets
import cook_gltf_geometry
from elisa_package import write_geometry_package
import gltf_hierarchy_self_test
import gltf_morph_self_test
import gltf_scene_self_test
import gltf_skin_self_test
import gltf_texture_self_test


ROOT = Path(__file__).resolve().parents[1]
PARTITION = "subsets do not partition the index stream"
RECORDS = "invalid cooked geometry subset records"
MISSING_SLOT = "subset names a missing material slot"
MATERIAL_RECORDS = "invalid cooked slot material records"
MATERIAL_RANGE = "cooked slot material is out of range"
TEXTURE_RECORDS = "invalid cooked slot texture records"
SECTION_NAME = "invalid cooked texture section name"
MISSING_IMAGE = "cooked slot texture names a missing image"
UNSAMPLED = "cooked texture image is never sampled"
NEEDS_TEXTURE = "cooked slot material lacks the texture it needs"
NEEDS_BUNDLE = "cooked slot textures need an ELPK bundle"
MISSING_SECTION = "cooked slot texture section is missing from its bundle"
# Base color, metallic, roughness, emissive, alpha cutoff, alpha mode, flags.
GLASS = (0.0, 0.0, 0.08, 0.5, 0.0, 0.9, 0.0, 0.0, 1.0, 0.5, 2, 0)
PAINT = (0.08, 0.0, 0.0, 1.0, 0.25, 0.75, 1.0, 0.0, 0.0, 0.5, 0, 1)
ZEROS = (0.0,) * 10 + (0, 0)
ONES = (1.0,) * 10 + (2, 1)
PANEL_MATERIALS = [(0.0, 0.0, 0.08, 1.0, 0.0, 0.9, 0.0, 0.0, 1.0, 0.5, 0, 1), PAINT]
# An alpha-masked material, and a double-sided one with occlusion.
MASKED = (0.0, 0.5, 0.0, 1.0, 0.0, 0.9, 0.0, 0.0, 0.0, 0.25, 1, 0)
SURFACED = (1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.5, 0, 3)
# The textured panel's cutout, painted, glow, and backdrop slots, each with
# its four image references.
TEXTURED_MATERIALS = [struct.unpack_from("<10f2I", gltf_texture_self_test.SLOT_MATERIALS, slot * 48) +
    struct.unpack_from("<4I", gltf_texture_self_test.SLOT_TEXTURES, slot * 16) for slot in range(4)]
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
        skinned: bool = False, materials=None, material_stride: str = "48", textures=None,
        texture_count: int | None = None, texture_stride: str = "16") -> bytes:
    """A row of `triangles` separate triangles with optional subset, slot
    material, and slot texture records. `textures` is (section names, one
    four-reference record per slot)."""
    vertices = triangles * 3
    positions = []
    for triangle in range(triangles):
        positions += [float(triangle), 0.0, 0.0, triangle + 1.0, 0.0, 0.0, float(triangle), 1.0, 0.0]
    lines = [
        "format=" + ("elisa-cooked-v3" if skinned else "elisa-cooked-v2"),
        "source=test/strip.gltf", "source_sha256=" + "0" * 64,
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
    if textures is not None:
        names, references = textures
        records = {
            "texture_count": f"texture_count={len(names) if texture_count is None else texture_count}",
            "texture_names_b64": "texture_names_b64=" + base64.b64encode(b"".join(
                struct.pack("<I", len(name)) + name.encode("ascii") for name in names)).decode("ascii"),
            "slot_texture_stride": f"slot_texture_stride={texture_stride}",
            "slot_textures_b64": "slot_textures_b64=" + encoded(f"<{len(references) * 4}I",
                [value for record in references for value in record]),
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
        rest = (0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0)
        lines += [
            "skin_bones=1", "skin_indices_stride=16", "skin_weights_stride=16",
            "skin_indices_b64=" + encoded(f"<{vertices * 4}I", (0,) * (vertices * 4)),
            "skin_weights_b64=" + encoded(f"<{vertices * 4}f", (1.0, 0.0, 0.0, 0.0) * vertices),
            "skin_names_b64=" + base64.b64encode(struct.pack("<I", len(name)) + name).decode("ascii"),
            "skin_joints=1", "skin_joint_parent_stride=4", "skin_joint_rest_stride=40",
            "skin_joint_parents_b64=" + encoded("<i", (-1,)),
            "skin_joint_rest_b64=" + encoded("<10f", rest),
            "skin_joint_names_b64=" + base64.b64encode(struct.pack("<I", len(name)) + name).decode("ascii"),
            "skin_cluster_joints_stride=4", "skin_cluster_joints_b64=" + encoded("<I", (0,)),
            "animation_clips=0",
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
    hierarchy_metadata = (directory / "hierarchy.pkg").read_bytes()
    gltf_skin_self_test.write_package(directory / "skinned-panel.pkg")
    gltf_morph_self_test.write_package(directory / "morphed-panel.pkg")
    gltf_scene_self_test.write_package(directory / "scene-metadata.pkg")
    scene_metadata = (directory / "scene-metadata.pkg").read_bytes()
    morph = (directory / "morphed-panel.pkg").read_bytes()
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
    # The textured panel may only load from a bundle that holds its images.
    textured_path, _ = cook_gltf_geometry.cook_geometry_package(gltf_texture_self_test.SOURCE,
        gltf_texture_self_test.ASSET_PATH, directory / "textured.pkg", allow_textures=True)
    textured = textured_path.read_bytes()
    images = dict(gltf_texture_self_test.SECTIONS)
    write_geometry_package(directory / "textured.elpk", textured, images)
    write_geometry_package(directory / "textured-missing.elpk", textured,
        {name: data for name, data in images.items() if name != "image_2"})
    textured_sections = [(name, zlib.crc32(data)) for name, data in gltf_texture_self_test.SECTIONS]
    # Sections listed out of bundle order, beside a section nothing samples.
    listed = {"surface_1": images["image_2"], "albedo": images["image_3"]}
    write_geometry_package(directory / "listed.elpk", strip_package(2, two, 2, materials=[MASKED, SURFACED],
        textures=(list(listed), [(2, 0, 0, 0), (2, 0, 1, 0)])), {**listed, "spare": images["image_1"]})
    listed_materials = [MASKED + (2, 0, 0, 0), SURFACED + (2, 0, 1, 0)]
    listed_sections = [(name, zlib.crc32(data)) for name, data in listed.items()]
    plain = [GLASS, PAINT]

    def morph_variant(old: bytes, new: bytes) -> bytes:
        if old not in morph:
            raise RuntimeError("morph fixture mutation did not find its field")
        return morph.replace(old, new, 1)

    def scene_variant(old: bytes, new: bytes) -> bytes:
        if old not in scene_metadata:
            raise RuntimeError("scene fixture mutation did not find its field")
        return scene_metadata.replace(old, new, 1)

    def hierarchy_variant(old: bytes, new: bytes) -> bytes:
        if old not in hierarchy_metadata:
            raise RuntimeError("hierarchy metadata mutation did not find its field")
        return hierarchy_metadata.replace(old, new, 1)

    def textured_strip(names, references, **fields):
        return strip_package(2, two, 2, materials=fields.pop("materials", plain),
            textures=(names, references), **fields)

    def named(name):
        return textured_strip([name], [(1, 0, 0, 0), (0, 0, 0, 0)])

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
        ("accept", "skinned-panel.pkg", None, (18, 2, [(0, 6, 1), (6, 6, 0), (12, 6, 1)], PANEL_MATERIALS,
            "animations", 1, "morphs", 1)),
        ("accept", "morphed-panel.pkg", None, (18, 2, [(0, 6, 1), (6, 6, 0), (12, 6, 1)], PANEL_MATERIALS,
            "morphs", 1)),
        ("accept", "scene-metadata.pkg", None, (18, 2, [(0, 6, 1), (6, 6, 0), (12, 6, 1)], PANEL_MATERIALS,
            "cameras", 2, "lights", 2)),
        ("reject", "scene-camera-projection.pkg", scene_variant(
            b"camera_0_projection=0", b"camera_0_projection=2"), "camera metadata"),
        ("reject", "scene-light-kind.pkg", scene_variant(
            b"light_0_kind=0", b"light_0_kind=3"), "light metadata"),
        ("reject", "scene-camera-transform.pkg", scene_variant(
            b"camera_0_transform_b64=", b"camera_0_transform="), "camera metadata"),
        ("reject", "hierarchy-mesh-stride.pkg", hierarchy_variant(
            b"mesh_placement_stride=56", b"mesh_placement_stride=52"), "mesh placement metadata"),
        ("reject", "hierarchy-mesh-count.pkg", hierarchy_variant(
            b"mesh_count=3", b"mesh_count=4"), "leaves a mesh unplaced"),
        ("reject", "morph-missing-position.pkg", morph_variant(
            b"morph_0_positions_b64=", b"morph_0_position_b64="), "morph position stream"),
        ("reject", "morph-stride.pkg", morph_variant(
            b"morph_target_position_stride=12", b"morph_target_position_stride=16"), "morph metadata"),
        ("reject", "morph-empty.pkg", morph_variant(
            b"morph_targets=1", b"morph_targets=0"), "morph metadata"),
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
        ("accept", "skinned-subsets.pkg", strip_package(2, [(0, 3, 0), (3, 3, 1)], 2, skinned=True),
            (6, 2, [(0, 3, 0), (3, 3, 1)])),
        ("accept", "skinned-single.pkg", strip_package(2, [(0, 6, 0)], 1, skinned=True),
            (6, 1, [(0, 6, 0)])),
        ("accept", "materials.pkg", strip_package(2, two, 2, materials=[GLASS, PAINT]),
            (6, 2, two, [GLASS, PAINT])),
        ("accept", "single-material.pkg", strip_package(2, [(0, 6, 0)], 1, materials=[PAINT]),
            (6, 1, [(0, 6, 0)], [PAINT])),
        ("accept", "material-bounds.pkg", strip_package(2, two, 2, materials=[ZEROS, ONES]),
            (6, 2, two, [ZEROS, ONES])),
        ("reject", "materials-without-subsets.pkg", strip_package(2, materials=[PAINT]), MATERIAL_RECORDS),
        ("accept", "skinned-materials.pkg", strip_package(2, [(0, 6, 0)], 1, skinned=True, materials=[PAINT]),
            (6, 1, [(0, 6, 0)], [PAINT])),
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
            materials=[with_field(GLASS, 10, 1), PAINT]), NEEDS_TEXTURE),
        ("reject", "material-mode.pkg", strip_package(2, two, 2,
            materials=[with_field(GLASS, 10, 3), PAINT]), MATERIAL_RANGE),
        ("reject", "material-flags.pkg", strip_package(2, two, 2,
            materials=[GLASS, with_field(PAINT, 11, 4)]), MATERIAL_RANGE),
        ("reject", "material-occlusion.pkg", strip_package(2, two, 2,
            materials=[GLASS, with_field(PAINT, 11, 2)]), NEEDS_TEXTURE),
        ("accept", "textured.elpk", None, (24, 4, gltf_texture_self_test.SUBSETS, TEXTURED_MATERIALS,
            textured_sections)),
        ("accept", "listed.elpk", None, (6, 2, two, listed_materials, listed_sections)),
        ("reject", "textured.pkg", None, NEEDS_BUNDLE),
        ("reject", "textured-missing.elpk", None, MISSING_SECTION),
        ("reject", "texture-count-zero.pkg", textured_strip([], [(0,) * 4] * 2), TEXTURE_RECORDS),
        ("reject", "texture-count-mismatch.pkg", textured_strip(["albedo"], [(1, 0, 0, 0)] * 2,
            texture_count=2), TEXTURE_RECORDS),
        ("reject", "texture-too-many.pkg", textured_strip([f"image_{index}" for index in range(65)],
            [(1, 2, 3, 4)] * 2), TEXTURE_RECORDS),
        ("reject", "texture-stride.pkg", textured_strip(["albedo"], [(1, 0, 0, 0)] * 2, texture_stride="12"),
            TEXTURE_RECORDS),
        ("reject", "texture-no-count.pkg", textured_strip(["albedo"], [(1, 0, 0, 0)] * 2,
            omit=("texture_count",)), TEXTURE_RECORDS),
        ("reject", "texture-no-names.pkg", textured_strip(["albedo"], [(1, 0, 0, 0)] * 2,
            omit=("texture_names_b64",)), TEXTURE_RECORDS),
        ("reject", "texture-no-stride.pkg", textured_strip(["albedo"], [(1, 0, 0, 0)] * 2,
            omit=("slot_texture_stride",)), TEXTURE_RECORDS),
        ("reject", "texture-no-records.pkg", textured_strip(["albedo"], [(1, 0, 0, 0)] * 2,
            omit=("slot_textures_b64",)), TEXTURE_RECORDS),
        ("reject", "texture-short.pkg", textured_strip(["albedo"], [(1, 0, 0, 0)]), TEXTURE_RECORDS),
        ("reject", "texture-long.pkg", textured_strip(["albedo"], [(1, 0, 0, 0)] * 3), TEXTURE_RECORDS),
        ("reject", "texture-without-materials.pkg", strip_package(2, two, 2,
            textures=(["albedo"], [(1, 0, 0, 0)] * 2)), TEXTURE_RECORDS),
        ("reject", "texture-duplicate.pkg", textured_strip(["albedo", "albedo"], [(1, 0, 0, 0), (2, 0, 0, 0)]),
            SECTION_NAME),
        ("reject", "texture-mesh.pkg", named("mesh"), SECTION_NAME),
        ("reject", "texture-manifest.pkg", named("manifest"), SECTION_NAME),
        ("reject", "texture-empty-name.pkg", named(""), SECTION_NAME),
        ("reject", "texture-uppercase.pkg", named("Albedo"), SECTION_NAME),
        ("reject", "texture-path.pkg", named("../albedo"), SECTION_NAME),
        ("reject", "texture-nul.pkg", named("albedo\0x"), SECTION_NAME),
        ("reject", "texture-long-name.pkg", named("a" * 16), SECTION_NAME),
        ("reject", "texture-beyond.pkg", textured_strip(["albedo"], [(1, 0, 0, 0), (0, 0, 2, 0)]), MISSING_IMAGE),
        ("reject", "texture-unsampled.pkg", textured_strip(["albedo", "spare"], [(1, 0, 0, 0)] * 2), UNSAMPLED),
        ("reject", "mask-without-base.pkg", textured_strip(["normal"], [(0, 1, 0, 0), (0, 0, 0, 0)],
            materials=[MASKED, PAINT]), NEEDS_TEXTURE),
        ("reject", "occlusion-without-surface.pkg", textured_strip(["albedo"], [(0, 0, 0, 0), (1, 0, 0, 0)],
            materials=[GLASS, SURFACED]), NEEDS_TEXTURE),
    ]


def float32_text(value: float) -> str:
    return repr(struct.unpack("<f", struct.pack("<f", value))[0])


def manifest_line(directory: Path, verdict: str, name: str, expectation) -> str:
    """One loader-test manifest line; see native/geometry_subset_test.cpp."""
    fields = [verdict, str(directory / name)]
    if verdict == "reject":
        return "\t".join(fields + [expectation])
    index_count, slots, subsets, *records = expectation
    animation_count = None
    morph_count = None
    camera_count = None
    light_count = None
    while len(records) >= 2 and records[-2] in ("animations", "morphs", "cameras", "lights"):
        marker, count = records[-2:]
        if marker == "animations":
            animation_count = count
        else:
            if marker == "morphs":
                morph_count = count
            elif marker == "cameras":
                camera_count = count
            else:
                light_count = count
        records = records[:-2]
    fields += [str(index_count), str(slots)] + [str(value) for subset in subsets for value in subset]
    if records:
        fields.append("materials")
        for record in records[0]:
            record += (0,) * (16 - len(record))
            fields += [float32_text(value) for value in record[:10]] + [str(value) for value in record[10:]]
    if len(records) > 1:
        fields.append("sections")
        for name, checksum in records[1]:
            fields += [name, str(checksum)]
    if animation_count is not None:
        fields += ["animations", str(animation_count)]
    if morph_count is not None:
        fields += ["morphs", str(morph_count)]
    if camera_count is not None:
        fields += ["cameras", str(camera_count)]
    if light_count is not None:
        fields += ["lights", str(light_count)]
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
