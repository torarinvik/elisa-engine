"""Serialize validated normalized glTF geometry into runtime packages."""

from __future__ import annotations

import base64
import hashlib
from pathlib import Path

import cook_assets
import cook_gltf_animation
import cook_gltf_geometry as geometry_cooker


def cook_geometry_package(source_path: Path, asset_path: str, output_path: Path,
        allow_textures: bool = False, simplify_ratio: float | None = None) -> tuple[Path, dict]:
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
        cook_assets.source_bytes(source_path.parent, document), simplify_ratio)
    if geometry["images"] and not allow_textures:
        raise ValueError("material textures need an .elpk bundle output")
    triangles = geometry["index_count"] // 3
    if (source_counts["triangles"] <= 0 or triangles <= 0 or triangles > source_counts["triangles"] or
            (simplify_ratio is None and triangles != source_counts["triangles"]) or
            geometry["index_count"] % 3 != 0 or
            geometry["vertex_count"] > source_counts["positions"] or
            (simplify_ratio is None and geometry["vertex_count"] != source_counts["positions"]) or
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
        *geometry_cooker.subset_lines(geometry), *geometry_cooker.tangent_lines(geometry),
        *geometry_cooker.skin_lines(geometry),
        *cook_gltf_animation.package_lines(geometry["animation_clips"],
            0 if geometry["skin"] is None else len(geometry["skin"]["joints"]),
            len(geometry["scene"]["mesh_placements"]), len(geometry["morph_targets"])),
        *geometry_cooker.morph_lines(geometry), *geometry_cooker.scene_lines(geometry),
        "positions_b64=" + base64.b64encode(geometry["positions"]).decode("ascii"),
        "normals_b64=" + base64.b64encode(geometry["normals"]).decode("ascii"),
        "uvs_b64=" + base64.b64encode(geometry["uvs"]).decode("ascii"),
        "indices_b64=" + base64.b64encode(geometry["indices"]).decode("ascii"),
    ]
    package_bytes = ("\n".join(lines) + "\n").encode("utf-8")
    if len(package_bytes) > 64 * 1024 * 1024:
        raise ValueError("cooked geometry package exceeds the 64 MiB runtime limit")
    output_path.write_bytes(package_bytes)
    return output_path, {"triangles": triangles, "source_triangles": source_counts["triangles"],
        "positions": geometry["vertex_count"], "indices": geometry["index_count"],
        "subsets": len(geometry["subsets"]), "material_slots": geometry["material_slots"],
        "slot_materials": len(geometry["slot_materials"]) // geometry_cooker.SLOT_MATERIAL_STRIDE,
        "source_sha256": source_digest, "images": dict(geometry["images"]),
        "morph_targets": len(geometry.get("morph_targets", [])),
        "cameras": len(geometry.get("scene", {}).get("cameras", [])),
        "lights": len(geometry.get("scene", {}).get("lights", [])),
        "lod": geometry.get("lod_report")}
