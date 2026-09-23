"""Production-loader cases for authored and generated glTF lightmap UVs."""

from __future__ import annotations

import base64
import json
from pathlib import Path
import struct

import cook_gltf_geometry
import gltf_skin_self_test


def cases(directory: Path, panel_source: Path, panel_materials: list, panel_subsets: list,
        skin_subsets: list) -> list[tuple]:
    """Cook UV1 fixtures and return normal geometry-loader case tuples."""
    atlas_path, atlas_geometry = cook_gltf_geometry.cook_geometry_package(
        panel_source, "test/fixtures/multi_material_panel.gltf", directory / "uv1.pkg",
        generate_lightmap_uv=True, lightmap_resolution=128, lightmap_padding=4)
    atlas_sections = dict(line.split("=", 1) for line in atlas_path.read_text(encoding="ascii").splitlines())
    authored_metadata = {"uv1_generator_revision", "uv1_resolution", "uv1_padding", "uv1_chart_count"}
    authored_lines = []
    for line in atlas_path.read_text(encoding="ascii").splitlines():
        name = line.partition("=")[0]
        if name in authored_metadata:
            continue
        authored_lines.append("uv1_source=gltf" if name == "uv1_source" else line)
    authored_package = ("\n".join(authored_lines) + "\n").encode("ascii")

    skinned_source = directory / "skinned-uv1.gltf"
    skinned_source.write_text(json.dumps(gltf_skin_self_test.generated_document(), separators=(",", ":")),
        encoding="utf-8")
    skinned_path, skinned_geometry = cook_gltf_geometry.cook_geometry_package(
        skinned_source, "test/generated/skinned-uv1.gltf", directory / "skinned-uv1.pkg",
        generate_lightmap_uv=True, lightmap_resolution=128, lightmap_padding=4)
    skinned_sections = dict(line.split("=", 1) for line in skinned_path.read_text(encoding="ascii").splitlines())

    def mutate_atlas_field(name: str, value: str | None) -> bytes:
        lines = atlas_path.read_text(encoding="ascii").splitlines()
        prefix = name + "="
        for index, line in enumerate(lines):
            if line.startswith(prefix):
                if value is None:
                    del lines[index]
                else:
                    lines[index] = prefix + value
                return ("\n".join(lines) + "\n").encode("ascii")
        if value is not None:
            lines.append(prefix + value)
        return ("\n".join(lines) + "\n").encode("ascii")

    return [
        ("accept", atlas_path.name, None, (18, 2, panel_subsets, panel_materials,
            "uv1", atlas_geometry["positions"], "xatlas", 128, 4,
            int(atlas_sections["uv1_chart_count"]))),
        ("accept", "uv1-authored.pkg", authored_package,
            (18, 2, panel_subsets, panel_materials, "uv1", atlas_geometry["positions"], "gltf", 0, 0, 0)),
        ("accept", skinned_path.name, None,
            (36, 2, skin_subsets, panel_materials, "animations", 1, "morphs", 1,
            "inverse_binds", gltf_skin_self_test.INVERSE_BIND_MATRICES,
            "uv1", skinned_geometry["positions"], "xatlas", 128, 4,
            int(skinned_sections["uv1_chart_count"]))),
        ("reject", "uv1-no-data.pkg", mutate_atlas_field("uv1s_b64", None),
            "incomplete cooked geometry UV1 stream"),
        ("reject", "uv1-stride.pkg", mutate_atlas_field("uv1_stride", "16"),
            "invalid cooked geometry UV1 stream"),
        ("reject", "uv1-source.pkg", mutate_atlas_field("uv1_source", "future"),
            "unknown cooked geometry UV1 source"),
        ("reject", "uv1-revision.pkg", mutate_atlas_field("uv1_generator_revision", "unknown"),
            "invalid cooked geometry UV1 atlas metadata"),
        ("reject", "uv1-atlas-range.pkg", mutate_atlas_field("uv1s_b64",
            base64.b64encode(struct.pack("<2f", 1.5, 0.5) +
                base64.b64decode(atlas_sections["uv1s_b64"])[8:]).decode("ascii")),
            "cooked geometry UV1 atlas coordinates are outside [0, 1]"),
    ]
