"""Serialize validated normalized glTF geometry into runtime packages."""

from __future__ import annotations

import base64
import hashlib
import math
from pathlib import Path
import struct

import cook_assets
import cook_gltf_animation
import cook_gltf_geometry as geometry_cooker
from cook_gltf_meshopt_streams import INDEX_STREAM, VERTEX_STREAM, encode_streams


def position_extent(positions: bytes) -> float:
    """Return the largest source-space AABB dimension for conservative LOD error projection."""
    if not positions or len(positions) % 12 != 0:
        raise ValueError("position stream cannot provide a bounded LOD extent")
    minimum = [float("inf")] * 3
    maximum = [float("-inf")] * 3
    for vertex in struct.iter_unpack("<3f", positions):
        for axis, value in enumerate(vertex):
            if not math.isfinite(value):
                raise ValueError("position stream contains a non-finite LOD extent")
            minimum[axis] = min(minimum[axis], value)
            maximum[axis] = max(maximum[axis], value)
    return max(maximum[axis] - minimum[axis] for axis in range(3))


def cook_geometry_package(source_path: Path, asset_path: str, output_path: Path,
        allow_textures: bool = False, simplify_ratio: float | None = None,
        generate_lightmap_uv: bool = False, lightmap_resolution: int = 1024,
        lightmap_padding: int = 4) -> tuple[Path, dict]:
    """Write a geometry package; textured sources require a containing bundle."""
    asset_path = geometry_cooker.safe_asset_path(asset_path)
    source_path = source_path.expanduser().resolve(strict=True)
    if (not source_path.is_file() or source_path.stat().st_size == 0 or
            source_path.stat().st_size > cook_assets.MAX_DOCUMENT_BYTES):
        raise ValueError("glTF source is not a regular file within the 64 MiB source bound")
    data = source_path.read_bytes()
    document = cook_assets.read_gltf(data)
    source_counts = geometry_cooker.placed_counts(document,
        skinned=any("skin" in node for node in document.get("nodes", [])))
    geometry = geometry_cooker.normalized_geometry(document,
        cook_assets.source_bytes(source_path.parent, document), simplify_ratio,
        generate_lightmap_uv, lightmap_resolution, lightmap_padding)
    if geometry["images"] and not allow_textures:
        raise ValueError("material textures need an .elpk bundle output")
    raw_streams = [
        ("positions", VERTEX_STREAM, geometry["vertex_count"], 12, geometry["positions"]),
        ("normals", VERTEX_STREAM, geometry["vertex_count"], 12, geometry["normals"]),
        ("uvs", VERTEX_STREAM, geometry["vertex_count"], 8, geometry["uvs"]),
        ("indices", INDEX_STREAM, geometry["index_count"], 4, geometry["indices"]),
    ]
    if geometry["tangents"]:
        raw_streams.append(("tangents", VERTEX_STREAM, geometry["vertex_count"], 16,
            geometry["tangents"]))
    if geometry["uv1s"]:
        raw_streams.append(("uv1s", VERTEX_STREAM, geometry["vertex_count"], 8,
            geometry["uv1s"]))
    encoded_streams = encode_streams(raw_streams)
    compressed_streams = {}
    for name, _kind, _count, _stride, raw in raw_streams:
        encoded = encoded_streams[name]
        if len(encoded) < len(raw):
            compressed_streams[name] = encoded
    triangles = geometry["index_count"] // 3
    if (source_counts["triangles"] <= 0 or triangles <= 0 or triangles > source_counts["triangles"] or
            (simplify_ratio is None and triangles != source_counts["triangles"]) or
            geometry["index_count"] % 3 != 0 or
            not 0 < geometry["vertex_count"] <= geometry_cooker.MAX_VERTICES or
            cook_assets.normalized_counts(document)["bounds"] is None):
        raise ValueError("normalized geometry does not match the declared source counts")
    source_digest = hashlib.sha256(data).hexdigest()
    output_path = output_path.expanduser().resolve()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "format=" + ("elisa-cooked-v3" if geometry["skin"] is not None or geometry["animation_clips"]
            else "elisa-cooked-v2"),
        f"source={asset_path}", f"source_sha256={source_digest}", f"triangles={triangles}",
        f"positions={geometry['vertex_count']}", f"indices={geometry['index_count']}",
        "position_stride=12", "normal_stride=12", "uv_stride=8", "index_stride=4",
        *(["meshopt_codec=meshoptimizer-v1.2"] if compressed_streams else []),
        *geometry_cooker.subset_lines(geometry),
        *(["tangent_stride=16"] if geometry["tangents"] else []),
        *geometry_cooker.skin_lines(geometry),
        *cook_gltf_animation.package_lines(geometry["animation_clips"],
            0 if geometry["skin"] is None else len(geometry["skin"]["joints"]),
            len(geometry["scene"]["mesh_placements"]), len(geometry["morph_targets"])),
        *geometry_cooker.morph_lines(geometry), *geometry_cooker.scene_lines(geometry),
        *[f"{name}_{'meshopt_' if name in compressed_streams else ''}b64=" +
            base64.b64encode(compressed_streams.get(name, data)).decode("ascii")
            for name, _kind, _count, _stride, data in raw_streams],
    ]
    if geometry["uv1s"]:
        metadata = geometry["uv1_metadata"]
        lines += ["uv1_stride=8", "uv1_source=" + metadata["source"]]
        if metadata["source"] == "xatlas":
            lines += ["uv1_generator_revision=f700c7790aaa030e794b52ba7791a05c085faf0c",
                f"uv1_resolution={metadata['resolution']}", f"uv1_padding={metadata['padding']}",
                f"uv1_chart_count={metadata['chart_count']}"]
    package_bytes = ("\n".join(lines) + "\n").encode("utf-8")
    if len(package_bytes) > 64 * 1024 * 1024:
        raise ValueError("cooked geometry package exceeds the 64 MiB runtime limit")
    output_path.write_bytes(package_bytes)
    attribute_bytes = sum(len(geometry[name]) for name in
        ("positions", "normals", "uvs", "uv1s", "tangents"))
    raw_mesh_stream_bytes = sum(len(data) for _name, _kind, _count, _stride, data in raw_streams)
    stored_mesh_stream_bytes = sum(len(compressed_streams.get(name, data))
        for name, _kind, _count, _stride, data in raw_streams)
    return output_path, {"triangles": triangles, "source_triangles": source_counts["triangles"],
        "positions": geometry["vertex_count"], "indices": geometry["index_count"],
        "attribute_bytes": attribute_bytes, "lightmap_uv": geometry.get("uv1_metadata"),
        "raw_mesh_stream_bytes": raw_mesh_stream_bytes,
        "stored_mesh_stream_bytes": stored_mesh_stream_bytes,
        "meshopt_compressed_streams": len(compressed_streams),
        "position_extent": position_extent(geometry["positions"]),
        "subsets": len(geometry["subsets"]), "material_slots": geometry["material_slots"],
        "slot_materials": len(geometry["slot_materials"]) // geometry_cooker.SLOT_MATERIAL_STRIDE,
        "source_sha256": source_digest, "images": dict(geometry["images"]),
        "morph_targets": len(geometry.get("morph_targets", [])),
        "cameras": len(geometry.get("scene", {}).get("cameras", [])),
        "lights": len(geometry.get("scene", {}).get("lights", [])),
        "lod": geometry.get("lod_report")}
