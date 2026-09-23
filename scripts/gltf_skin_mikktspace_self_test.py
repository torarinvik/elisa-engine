"""Check skin stream preservation across glTF MikkTSpace seam splits."""

from __future__ import annotations

import struct
import sys

import cook_gltf_geometry
import cook_gltf_mikktspace


def self_test(document: dict, source_buffer: bytes) -> bool:
    buffer = bytearray(source_buffer)
    uv_accessor = document["accessors"][document["meshes"][0]["primitives"][0]["attributes"]["TEXCOORD_0"]]
    uv_view = document["bufferViews"][uv_accessor["bufferView"]]
    uv_offset = uv_view.get("byteOffset", 0)
    uv_values = list(struct.unpack_from("<16f", buffer, uv_offset))
    uv_values[4:6] = (-1.0, 1.0)
    struct.pack_into("<16f", buffer, uv_offset, *uv_values)
    primitives = document["meshes"][0]["primitives"]
    source = cook_gltf_geometry.read_vertices(document, buffer,
        primitives[0]["attributes"], primitives[0].get("targets", []))
    indices = []
    for primitive in (primitives[0], primitives[2]):
        indices.extend(cook_gltf_geometry.read_indices(document, buffer, primitive, source["count"]))
    try:
        expected = cook_gltf_mikktspace.generate(source["positions"], source["normals"],
            source["uvs"], source["count"], indices)
        geometry = cook_gltf_geometry.normalized_geometry(document, bytes(buffer))
    except ValueError as error:
        print(f"glTF skin self-test failed: MikkTSpace seam remap: {error}", file=sys.stderr)
        return False
    placement = geometry["scene"]["mesh_placements"][0]
    palette = geometry["skin"]["placement_palettes"][0]
    start = placement["vertex_start"]
    end = start + len(expected["source_vertices"])
    expected_indices = [source["skin_indices"][vertex * 4 + channel] + palette["palette_offset"]
        for vertex in expected["source_vertices"] for channel in range(4)]
    expected_weights = [source["skin_weights"][vertex * 4 + channel]
        for vertex in expected["source_vertices"] for channel in range(4)]
    if (len(expected["source_vertices"]) <= source["count"] or
            geometry["vertex_count"] < end or
            geometry["skin_indices"][start * 4:end * 4] != expected_indices or
            geometry["skin_weights"][start * 4:end * 4] != expected_weights):
        print("glTF skin self-test failed: tangent seam split lost its source skin influences",
            file=sys.stderr)
        return False
    return True
