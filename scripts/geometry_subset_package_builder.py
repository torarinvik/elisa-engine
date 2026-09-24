"""Build small synthetic cooked-geometry packages for loader tests."""

import base64
import struct


def _encoded(format_string: str, values) -> str:
    return base64.b64encode(struct.pack(format_string, *values)).decode("ascii")


def strip_package(triangles: int, subsets=None, slots: int | None = None,
        record_count: int | None = None, stride: str = "12", omit: tuple[str, ...] = (),
        skinned: bool = False, materials=None, material_stride: str = "48", textures=None,
        texture_count: int | None = None, texture_stride: str = "20", material_names=None,
        material_name_payload: bytes | None = None, normal_scales=None,
        normal_scale_stride: str = "4") -> bytes:
    """A row of separate triangles with optional subset and material records."""
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
            "subsets_b64": "subsets_b64=" + _encoded(f"<{len(subsets) * 3}I",
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
    if normal_scales is not None:
        records = {
            "slot_normal_scale_stride": f"slot_normal_scale_stride={normal_scale_stride}",
            "slot_normal_scales_b64": "slot_normal_scales_b64=" + _encoded(
                f"<{len(normal_scales)}f", normal_scales),
        }
        lines += [line for key, line in records.items() if key not in omit]
    if material_names is not None or material_name_payload is not None:
        names_data = material_name_payload
        if names_data is None:
            names_data = b"".join(struct.pack("<I", len(name.encode("utf-8"))) + name.encode("utf-8")
                for name in material_names)
        if "slot_material_names_b64" not in omit:
            lines.append("slot_material_names_b64=" + base64.b64encode(names_data).decode("ascii"))
    if textures is not None:
        names, references = textures
        references = [tuple(record) + (0,) if len(record) == 4 else tuple(record) for record in references]
        records = {
            "texture_count": f"texture_count={len(names) if texture_count is None else texture_count}",
            "texture_names_b64": "texture_names_b64=" + base64.b64encode(b"".join(
                struct.pack("<I", len(name)) + name.encode("ascii") for name in names)).decode("ascii"),
            "slot_texture_stride": f"slot_texture_stride={texture_stride}",
            "slot_textures_b64": "slot_textures_b64=" + _encoded(f"<{len(references) * 5}I",
                [value for record in references for value in record]),
        }
        lines += [line for key, line in records.items() if key not in omit]
    lines += [
        "positions_b64=" + _encoded(f"<{vertices * 3}f", positions),
        "normals_b64=" + _encoded(f"<{vertices * 3}f", (0.0, 0.0, 1.0) * vertices),
        "uvs_b64=" + _encoded(f"<{vertices * 2}f", (0.0,) * (vertices * 2)),
        "indices_b64=" + _encoded(f"<{vertices}I", range(vertices)),
    ]
    if skinned:
        name = b"root"
        rest = (0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0)
        lines += [
            "skin_bones=1", "skin_indices_stride=16", "skin_weights_stride=16",
            "skin_indices_b64=" + _encoded(f"<{vertices * 4}I", (0,) * (vertices * 4)),
            "skin_weights_b64=" + _encoded(f"<{vertices * 4}f", (1.0, 0.0, 0.0, 0.0) * vertices),
            "skin_names_b64=" + base64.b64encode(struct.pack("<I", len(name)) + name).decode("ascii"),
            "skin_joints=1", "skin_joint_parent_stride=4", "skin_joint_rest_stride=40",
            "skin_joint_parents_b64=" + _encoded("<i", (-1,)),
            "skin_joint_rest_b64=" + _encoded("<10f", rest),
            "skin_joint_names_b64=" + base64.b64encode(struct.pack("<I", len(name)) + name).decode("ascii"),
            "skin_cluster_joints_stride=4", "skin_cluster_joints_b64=" + _encoded("<I", (0,)),
            "animation_clips=0",
        ]
    return ("\n".join(lines) + "\n").encode("ascii")
