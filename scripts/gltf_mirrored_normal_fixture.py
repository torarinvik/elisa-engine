#!/usr/bin/env python3
"""Write the mirrored-UV normal-map panel used by the native render smoke."""

from __future__ import annotations

import base64
import json
from pathlib import Path
import struct
import sys

from png_image import encode_png


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "test/fixtures/mirrored_normal_panel.gltf"
PANEL_HALF_EXTENT = 1.0
SOURCE_VERTEX_COUNT = 6
INDEX_COUNT = 12
NORMAL_TEXTURE_SIZE = 8
OCCLUSION_TEXTURE_SIZE = 8
TANGENT_POSITIVE_X_RGBA = (255, 128, 128, 255)
OCCLUSION_RGBA = (51, 51, 51, 255)
NORMAL_TEXTURE_SCALE = 0.75
OCCLUSION_STRENGTH = 0.65


def fixture_text() -> str:
    # Two coplanar quads share the centre edge. The right chart reverses U,
    # which requires MikkTSpace to duplicate the seam vertices and flip their
    # tangent-frame handedness. A +X tangent-space normal makes the rendered
    # sides respond differently to the same directional light.
    positions = (
        (-PANEL_HALF_EXTENT, 0.0, -PANEL_HALF_EXTENT),
        (-PANEL_HALF_EXTENT, 0.0, PANEL_HALF_EXTENT), (0.0, 0.0, PANEL_HALF_EXTENT),
        (0.0, 0.0, -PANEL_HALF_EXTENT),
        (PANEL_HALF_EXTENT, 0.0, -PANEL_HALF_EXTENT),
        (PANEL_HALF_EXTENT, 0.0, PANEL_HALF_EXTENT),
    )
    uvs = ((0.0, 0.0), (0.0, 1.0), (1.0, 1.0), (1.0, 0.0),
        (0.0, 0.0), (0.0, 1.0))
    normals = ((0.0, 1.0, 0.0),) * SOURCE_VERTEX_COUNT
    indices = (0, 1, 2, 0, 2, 3, 2, 5, 4, 2, 4, 3)
    normal_map = encode_png(NORMAL_TEXTURE_SIZE, NORMAL_TEXTURE_SIZE,
        bytes(TANGENT_POSITIVE_X_RGBA) * NORMAL_TEXTURE_SIZE * NORMAL_TEXTURE_SIZE)
    occlusion_map = encode_png(OCCLUSION_TEXTURE_SIZE, OCCLUSION_TEXTURE_SIZE,
        bytes(OCCLUSION_RGBA) * OCCLUSION_TEXTURE_SIZE * OCCLUSION_TEXTURE_SIZE)
    blocks = (
        b"".join(struct.pack("<3f", *point) for point in positions),
        b"".join(struct.pack("<3f", *normal) for normal in normals),
        b"".join(struct.pack("<2f", *uv) for uv in uvs),
        struct.pack(f"<{INDEX_COUNT}H", *indices),
        normal_map,
        occlusion_map,
    )
    buffer = bytearray()
    views = []
    for block in blocks:
        views.append({"buffer": 0, "byteOffset": len(buffer), "byteLength": len(block)})
        buffer.extend(block)
        buffer.extend(bytes(-len(buffer) % 4))
    payload = base64.b64encode(buffer).decode("ascii")
    return json.dumps({
        "asset": {"version": "2.0", "generator": "Elisa mirrored normal reference"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": "mirrored_normal_panel", "mesh": 0}],
        "meshes": [{"primitives": [{
            "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
            "indices": 3,
            "material": 0,
        }]}],
        "materials": [{"name": "mirrored_normal", "pbrMetallicRoughness": {
            "baseColorFactor": [0.85, 0.85, 0.85, 1.0],
            "metallicFactor": 0.0,
            "roughnessFactor": 0.9,
        }, "normalTexture": {"index": 0, "scale": NORMAL_TEXTURE_SCALE},
            "occlusionTexture": {"index": 1, "strength": OCCLUSION_STRENGTH}}],
        "textures": [{"source": 0}, {"source": 1}],
        "images": [
            {"name": "normal", "bufferView": 4, "mimeType": "image/png"},
            {"name": "occlusion", "bufferView": 5, "mimeType": "image/png"},
        ],
        "buffers": [{"byteLength": len(buffer),
            "uri": "data:application/octet-stream;base64," + payload}],
        "bufferViews": views,
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": SOURCE_VERTEX_COUNT, "type": "VEC3",
                "min": [-PANEL_HALF_EXTENT, 0.0, -PANEL_HALF_EXTENT],
                "max": [PANEL_HALF_EXTENT, 0.0, PANEL_HALF_EXTENT]},
            {"bufferView": 1, "componentType": 5126, "count": SOURCE_VERTEX_COUNT, "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": SOURCE_VERTEX_COUNT, "type": "VEC2"},
            {"bufferView": 3, "componentType": 5123, "count": INDEX_COUNT, "type": "SCALAR"},
        ],
    }, indent=2) + "\n"


if __name__ == "__main__":
    if sys.argv[1:] != ["--write-fixture"]:
        raise SystemExit("usage: gltf_mirrored_normal_fixture.py --write-fixture")
    OUTPUT.write_text(fixture_text(), encoding="utf-8")
