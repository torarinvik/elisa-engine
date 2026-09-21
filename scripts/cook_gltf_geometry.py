"""Normalize a static glTF mesh into Elisa's bounded runtime package."""

from __future__ import annotations

import base64
import hashlib
import math
from pathlib import Path
import struct

import cook_assets

# The native loader enforces the same bounds.
MAX_SUBSETS = 16
MAX_MATERIAL_SLOTS = 16
MAX_VERTICES = 2_000_000
MAX_INDICES = 15_000_000


def material_slot_count(document: dict, primitives: list) -> int:
    """Return the mesh's material slot count: one per declared glTF material.

    Either every primitive binds a declared material or the document declares
    none. The cooker binds slots, not material properties, so a material with
    anything beyond a name is rejected rather than silently dropped.
    """
    materials = document.get("materials", [])
    if not isinstance(materials, list) or len(materials) > MAX_MATERIAL_SLOTS:
        raise ValueError(f"runtime geometry cooker accepts at most {MAX_MATERIAL_SLOTS} materials")
    for material in materials:
        if not isinstance(material, dict) or set(material) - {"name"}:
            raise ValueError("runtime geometry cooker does not import material properties; "
                "register each slot's material at runtime")
    bindings = [primitive.get("material") for primitive in primitives]
    if not materials:
        if any(binding is not None for binding in bindings):
            raise ValueError("primitive binds a material the document does not declare")
        return 1
    for binding in bindings:
        if type(binding) is not int or not 0 <= binding < len(materials):
            raise ValueError("every primitive must bind one of the document's materials")
    return len(materials)


def validate_static_geometry_source(document: dict) -> tuple[list, int]:
    """Return the triangle primitives and material slot count after rejecting
    unhandled glTF semantics."""
    if document.get("extensionsUsed") or document.get("extensionsRequired"):
        raise ValueError("runtime geometry cooker does not support glTF extensions")
    if any(document.get(name) for name in ("animations", "skins", "cameras")):
        raise ValueError("runtime geometry cooker accepts static geometry only")

    meshes = document.get("meshes", [])
    if len(meshes) != 1:
        raise ValueError("runtime geometry cooker requires exactly one mesh")
    mesh = meshes[0]
    if set(mesh) - {"name", "primitives"}:
        raise ValueError("runtime geometry cooker encountered unsupported mesh properties")
    primitives = mesh.get("primitives", [])
    if not isinstance(primitives, list) or not 1 <= len(primitives) <= MAX_SUBSETS:
        raise ValueError(f"runtime geometry cooker requires 1 to {MAX_SUBSETS} primitives")
    for primitive in primitives:
        if not isinstance(primitive, dict) or set(primitive) - {"attributes", "indices", "mode", "material"}:
            raise ValueError("runtime geometry cooker encountered unsupported primitive properties")
        attributes = primitive.get("attributes", {})
        if not isinstance(attributes, dict) or any(type(value) is not int for value in attributes.values()):
            raise ValueError("primitive attributes must name accessors by index")
        if set(attributes) - {"POSITION", "NORMAL", "TEXCOORD_0"}:
            raise ValueError("runtime geometry cooker encountered an unsupported vertex attribute")
    material_slots = material_slot_count(document, primitives)

    nodes = document.get("nodes", [])
    scenes = document.get("scenes", [])
    if (len(nodes) != 1 or nodes[0].get("mesh") != 0 or
            set(nodes[0]) - {"mesh", "name"}):
        raise ValueError("runtime geometry cooker requires one untransformed mesh node")
    if (len(scenes) != 1 or document.get("scene") != 0 or
            scenes[0].get("nodes") != [0] or set(scenes[0]) - {"nodes", "name"}):
        raise ValueError("runtime geometry cooker requires one scene containing only its mesh node")
    return primitives, material_slots


def float_stream(document: dict, buffer: bytes, reference, type_name: str,
        count: int | None, label: str) -> bytes:
    accessors = document.get("accessors", [])
    if type(reference) is not int or not 0 <= reference < len(accessors):
        raise ValueError(f"{label} accessor index out of range")
    accessor = accessors[reference]
    if accessor["componentType"] != 5126 or accessor["type"] != type_name:
        raise ValueError(f"{label} must be float32 {type_name} values")
    if count is not None and accessor["count"] != count:
        raise ValueError(f"{label} count does not match the positions")
    data = cook_assets.accessor_bytes(document, buffer, reference)
    if not all(math.isfinite(value) for (value,) in struct.iter_unpack("<f", data)):
        raise ValueError(f"{label} data contains non-finite values")
    return data


def read_vertices(document: dict, buffer: bytes, attributes: dict) -> dict:
    if "POSITION" not in attributes:
        raise ValueError("triangle primitive is missing positions or indices")
    accessors = document.get("accessors", [])
    position_index = attributes["POSITION"]
    if type(position_index) is not int or not 0 <= position_index < len(accessors):
        raise ValueError("position accessor index out of range")
    vertex_count = accessors[position_index]["count"]
    if vertex_count <= 0 or vertex_count > MAX_VERTICES:
        raise ValueError("position count is empty or exceeds the runtime bound")
    positions = float_stream(document, buffer, position_index, "VEC3", None, "positions")
    if len(positions) != vertex_count * 12:
        raise ValueError("position data is malformed or non-finite")
    normals = None
    if "NORMAL" in attributes:
        normals = float_stream(document, buffer, attributes["NORMAL"], "VEC3", vertex_count, "normals")
    uvs = bytes(vertex_count * 8)
    if "TEXCOORD_0" in attributes:
        uvs = float_stream(document, buffer, attributes["TEXCOORD_0"], "VEC2", vertex_count, "UVs")
    return {"positions": positions, "normals": normals, "uvs": uvs, "count": vertex_count, "triangles": []}


def read_indices(document: dict, buffer: bytes, primitive: dict, vertex_count: int) -> list[int]:
    accessors = document.get("accessors", [])
    reference = primitive.get("indices")
    if reference is None:
        raise ValueError("triangle primitive is missing positions or indices")
    if type(reference) is not int or not 0 <= reference < len(accessors):
        raise ValueError("index accessor index out of range")
    accessor = accessors[reference]
    if accessor["type"] != "SCALAR" or accessor["componentType"] not in (5121, 5123, 5125):
        raise ValueError("indices must be uint8, uint16, or uint32 scalars")
    index_count = accessor["count"]
    if index_count < 3 or index_count % 3 != 0 or index_count > MAX_INDICES:
        raise ValueError("index count is not a bounded triangle list")
    index_format = {5121: "<B", 5123: "<H", 5125: "<I"}[accessor["componentType"]]
    values = [value for (value,) in struct.iter_unpack(index_format,
        cook_assets.accessor_bytes(document, buffer, reference))]
    if any(value >= vertex_count for value in values):
        raise ValueError("index references a vertex outside the position stream")
    return values


def generated_normals(positions: bytes, vertex_count: int, indices: list[int]) -> bytes:
    points = list(struct.iter_unpack("<3f", positions))
    accumulators = [[0.0, 0.0, 0.0] for _ in range(vertex_count)]
    for triangle in range(0, len(indices), 3):
        a, b, c = (points[indices[triangle + offset]] for offset in range(3))
        ab = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
        ac = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
        cross = (ab[1] * ac[2] - ab[2] * ac[1],
            ab[2] * ac[0] - ab[0] * ac[2], ab[0] * ac[1] - ab[1] * ac[0])
        for vertex in indices[triangle:triangle + 3]:
            for axis in range(3):
                accumulators[vertex][axis] += cross[axis]
    packed = bytearray()
    for normal in accumulators:
        length = math.sqrt(sum(component * component for component in normal))
        if length <= 1e-20:
            raise ValueError("cannot generate a normal for a degenerate vertex")
        packed += struct.pack("<3f", *(component / length for component in normal))
    return bytes(packed)


def normalized_geometry(document: dict, buffer: bytes):
    # Each primitive becomes one index subset bound to its material slot.
    # Primitives that name the same vertex accessors share one copy of those
    # vertices. Scene graphs, skins, and material properties stay in their
    # dedicated import paths; silently flattening them would produce wrong assets.
    primitives, material_slots = validate_static_geometry_source(document)
    blocks: dict[tuple, dict] = {}
    block_for_position: dict = {}
    primitive_indices = []
    for primitive in primitives:
        if primitive.get("mode", 4) != 4:
            raise ValueError("runtime geometry cooker supports triangle primitives only")
        attributes = primitive.get("attributes", {})
        key = tuple(sorted(attributes.items()))
        if block_for_position.setdefault(attributes.get("POSITION"), key) != key:
            raise ValueError("primitives that share positions must share every vertex attribute")
        if key not in blocks:
            blocks[key] = read_vertices(document, buffer, attributes)
        values = read_indices(document, buffer, primitive, blocks[key]["count"])
        blocks[key]["triangles"].extend(values)
        primitive_indices.append((key, values))

    positions, normals, uvs = bytearray(), bytearray(), bytearray()
    base_vertex: dict[tuple, int] = {}
    for key, block in blocks.items():
        base_vertex[key] = len(positions) // 12
        if base_vertex[key] + block["count"] > MAX_VERTICES:
            raise ValueError("position count is empty or exceeds the runtime bound")
        positions += block["positions"]
        normals += block["normals"] if block["normals"] is not None else \
            generated_normals(block["positions"], block["count"], block["triangles"])
        uvs += block["uvs"]

    indices = bytearray()
    subsets = []
    for primitive, (key, values) in zip(primitives, primitive_indices):
        start = len(indices) // 4
        if start + len(values) > MAX_INDICES:
            raise ValueError("index count is not a bounded triangle list")
        indices += struct.pack(f"<{len(values)}I", *(value + base_vertex[key] for value in values))
        subsets.append((start, len(values), primitive.get("material", 0)))
    return {"positions": bytes(positions), "normals": bytes(normals), "uvs": bytes(uvs),
        "indices": bytes(indices), "vertex_count": len(positions) // 12,
        "index_count": len(indices) // 4, "subsets": subsets, "material_slots": material_slots}


def safe_asset_path(value: str) -> str:
    if (not value or len(value) > 4096 or value.startswith("/") or "\\" in value or
            "\n" in value or "\r" in value or "\0" in value or
            any(part in ("", ".", "..") for part in value.split("/"))):
        raise ValueError("asset path must be a safe project-relative path without `..`")
    return value


def subset_lines(geometry: dict) -> list[str]:
    """Subset records, omitted when one subset covers every index with slot 0
    so that single-primitive packages keep their earlier bytes."""
    subsets = geometry["subsets"]
    if geometry["material_slots"] == 1 and len(subsets) == 1:
        return []
    packed = b"".join(struct.pack("<3I", *subset) for subset in subsets)
    return [
        f"material_slots={geometry['material_slots']}",
        f"subset_count={len(subsets)}",
        "subset_stride=12",
        "subsets_b64=" + base64.b64encode(packed).decode("ascii"),
    ]


def cook_geometry_package(source_path: Path, asset_path: str, output_path: Path) -> tuple[Path, dict]:
    asset_path = safe_asset_path(asset_path)
    source_path = source_path.expanduser().resolve(strict=True)
    if not source_path.is_file() or source_path.stat().st_size == 0 or source_path.stat().st_size > cook_assets.MAX_DOCUMENT_BYTES:
        raise ValueError("glTF source is not a regular file within the 64 MiB source bound")
    data = source_path.read_bytes()
    document = cook_assets.read_gltf(data)
    geometry = normalized_geometry(document, cook_assets.source_bytes(source_path.parent, document))
    counts = cook_assets.normalized_counts(document)
    if (counts["triangles"] <= 0 or geometry["index_count"] != counts["triangles"] * 3 or
            geometry["vertex_count"] != counts["positions"] or counts["bounds"] is None):
        raise ValueError("normalized geometry does not match the declared source counts")
    digest = hashlib.sha256(data).hexdigest()
    output_path = output_path.expanduser().resolve()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "format=elisa-cooked-v2",
        f"source={asset_path}",
        f"source_sha256={digest}",
        f"triangles={counts['triangles']}",
        f"positions={geometry['vertex_count']}",
        f"indices={geometry['index_count']}",
        "position_stride=12",
        "normal_stride=12",
        "uv_stride=8",
        "index_stride=4",
        *subset_lines(geometry),
        "positions_b64=" + base64.b64encode(geometry["positions"]).decode("ascii"),
        "normals_b64=" + base64.b64encode(geometry["normals"]).decode("ascii"),
        "uvs_b64=" + base64.b64encode(geometry["uvs"]).decode("ascii"),
        "indices_b64=" + base64.b64encode(geometry["indices"]).decode("ascii"),
    ]
    package_bytes = ("\n".join(lines) + "\n").encode("utf-8")
    if len(package_bytes) > 64 * 1024 * 1024:
        raise ValueError("cooked geometry package exceeds the 64 MiB runtime limit")
    output_path.write_bytes(package_bytes)
    return output_path, {"triangles": counts["triangles"], "positions": geometry["vertex_count"],
        "indices": geometry["index_count"], "subsets": len(geometry["subsets"]),
        "material_slots": geometry["material_slots"], "source_sha256": digest}
