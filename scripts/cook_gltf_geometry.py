"""Normalize a static glTF primitive into Elisa's bounded runtime package."""

from __future__ import annotations

import base64
import hashlib
import math
from pathlib import Path
import struct

import cook_assets


def validate_static_geometry_source(document: dict) -> dict:
    """Return the sole triangle primitive after rejecting unhandled glTF semantics."""
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
    if len(primitives) != 1:
        raise ValueError("runtime geometry cooker requires exactly one primitive")
    primitive = primitives[0]
    if set(primitive) - {"attributes", "indices", "mode"}:
        raise ValueError("runtime geometry cooker encountered unsupported primitive properties")
    if set(primitive.get("attributes", {})) - {"POSITION", "NORMAL", "TEXCOORD_0"}:
        raise ValueError("runtime geometry cooker encountered an unsupported vertex attribute")

    nodes = document.get("nodes", [])
    scenes = document.get("scenes", [])
    if (len(nodes) != 1 or nodes[0].get("mesh") != 0 or
            set(nodes[0]) - {"mesh", "name"}):
        raise ValueError("runtime geometry cooker requires one untransformed mesh node")
    if (len(scenes) != 1 or document.get("scene") != 0 or
            scenes[0].get("nodes") != [0] or set(scenes[0]) - {"nodes", "name"}):
        raise ValueError("runtime geometry cooker requires one scene containing only its mesh node")
    return primitive


def normalized_geometry(document: dict, buffer: bytes):
    # This first runtime cooker intentionally accepts one static triangle
    # primitive. Scene graphs, skins, and materials stay in their dedicated
    # import paths; silently flattening them here would produce wrong assets.
    primitive = validate_static_geometry_source(document)
    if primitive.get("mode", 4) != 4:
        raise ValueError("runtime geometry cooker supports triangle primitives only")
    attributes = primitive.get("attributes", {})
    indices_ref = primitive.get("indices")
    if indices_ref is None or "POSITION" not in attributes:
        raise ValueError("triangle primitive is missing positions or indices")

    accessors = document.get("accessors", [])
    position_index = attributes["POSITION"]
    if position_index < 0 or position_index >= len(accessors):
        raise ValueError("position accessor index out of range")
    position_accessor = accessors[position_index]
    if position_accessor["componentType"] != 5126 or position_accessor["type"] != "VEC3":
        raise ValueError("only float32 VEC3 positions are cooked")
    vertex_count = position_accessor["count"]
    if vertex_count <= 0 or vertex_count > 2_000_000:
        raise ValueError("position count is empty or exceeds the runtime bound")
    positions = cook_assets.accessor_bytes(document, buffer, position_index)
    if len(positions) != vertex_count * 12 or not all(
            math.isfinite(value) for (value,) in struct.iter_unpack("<f", positions)):
        raise ValueError("position data is malformed or non-finite")

    index_accessor = accessors[indices_ref]
    if index_accessor["type"] != "SCALAR" or index_accessor["componentType"] not in (5121, 5123, 5125):
        raise ValueError("indices must be uint8, uint16, or uint32 scalars")
    raw_indices = cook_assets.accessor_bytes(document, buffer, indices_ref)
    index_format = {5121: "<B", 5123: "<H", 5125: "<I"}[index_accessor["componentType"]]
    converted = b"".join(struct.pack("<I", value)
        for (value,) in struct.iter_unpack(index_format, raw_indices))
    index_count = index_accessor["count"]
    if index_count < 3 or index_count % 3 != 0 or index_count > 15_000_000:
        raise ValueError("index count is not a bounded triangle list")
    if any(value >= vertex_count for (value,) in struct.iter_unpack("<I", converted)):
        raise ValueError("index references a vertex outside the position stream")

    normals = b""
    if "NORMAL" in attributes:
        normal_index = attributes["NORMAL"]
        if normal_index < 0 or normal_index >= len(accessors):
            raise ValueError("normal accessor index out of range")
        normal_accessor = accessors[normal_index]
        if (normal_accessor["componentType"] != 5126 or normal_accessor["type"] != "VEC3" or
                normal_accessor["count"] != vertex_count):
            raise ValueError("normals must be float32 VEC3 values matching the positions")
        normals = cook_assets.accessor_bytes(document, buffer, normal_index)
        if not all(math.isfinite(value) for (value,) in struct.iter_unpack("<f", normals)):
            raise ValueError("normal data contains non-finite values")
    else:
        points = list(struct.iter_unpack("<3f", positions))
        accumulators = [[0.0, 0.0, 0.0] for _ in range(vertex_count)]
        indices = [value for (value,) in struct.iter_unpack("<I", converted)]
        for triangle in range(0, index_count, 3):
            a, b, c = (points[indices[triangle + offset]] for offset in range(3))
            ab = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
            ac = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
            cross = (ab[1] * ac[2] - ab[2] * ac[1],
                ab[2] * ac[0] - ab[0] * ac[2], ab[0] * ac[1] - ab[1] * ac[0])
            for vertex in (indices[triangle], indices[triangle + 1], indices[triangle + 2]):
                for axis in range(3):
                    accumulators[vertex][axis] += cross[axis]
        packed_normals = bytearray()
        for normal in accumulators:
            length = math.sqrt(sum(component * component for component in normal))
            if length <= 1e-20:
                raise ValueError("cannot generate a normal for a degenerate vertex")
            packed_normals += struct.pack("<3f", *(component / length for component in normal))
        normals = bytes(packed_normals)

    uvs = bytes(vertex_count * 8)
    if "TEXCOORD_0" in attributes:
        uv_index = attributes["TEXCOORD_0"]
        if uv_index < 0 or uv_index >= len(accessors):
            raise ValueError("UV accessor index out of range")
        uv_accessor = accessors[uv_index]
        if (uv_accessor["componentType"] != 5126 or uv_accessor["type"] != "VEC2" or
                uv_accessor["count"] != vertex_count):
            raise ValueError("UVs must be float32 VEC2 values matching the positions")
        uvs = cook_assets.accessor_bytes(document, buffer, uv_index)
        if not all(math.isfinite(value) for (value,) in struct.iter_unpack("<f", uvs)):
            raise ValueError("UV data contains non-finite values")
    return {"positions": positions, "normals": normals, "uvs": uvs,
        "indices": converted, "vertex_count": vertex_count, "index_count": index_count}


def safe_asset_path(value: str) -> str:
    if (not value or len(value) > 4096 or value.startswith("/") or "\\" in value or
            "\n" in value or "\r" in value or "\0" in value or
            any(part in ("", ".", "..") for part in value.split("/"))):
        raise ValueError("asset path must be a safe project-relative path without `..`")
    return value


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
        "indices": geometry["index_count"], "source_sha256": digest}
