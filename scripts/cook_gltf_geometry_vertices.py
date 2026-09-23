"""Read one glTF primitive's bounded vertex-side attribute streams."""

from __future__ import annotations


def read(document: dict, buffer: bytes, attributes: dict, targets: list[dict]) -> dict:
    import cook_gltf_geometry
    import cook_gltf_skin

    max_vertices = cook_gltf_geometry.MAX_VERTICES
    if "POSITION" not in attributes:
        raise ValueError("triangle primitive is missing positions or indices")
    accessors = document.get("accessors", [])
    position_index = attributes["POSITION"]
    if type(position_index) is not int or not 0 <= position_index < len(accessors):
        raise ValueError("position accessor index out of range")
    vertex_count = accessors[position_index]["count"]
    if vertex_count <= 0 or vertex_count > max_vertices:
        raise ValueError("position count is empty or exceeds the runtime bound")
    positions = cook_gltf_geometry.float_stream(document, buffer, position_index, "VEC3", None, "positions")
    if len(positions) != vertex_count * 12:
        raise ValueError("position data is malformed or non-finite")
    normals = None
    if "NORMAL" in attributes:
        normals = cook_gltf_geometry.float_stream(document, buffer, attributes["NORMAL"], "VEC3", vertex_count, "normals")
    tangents = None
    if "TANGENT" in attributes:
        tangents = cook_gltf_geometry.float_stream(document, buffer, attributes["TANGENT"], "VEC4", vertex_count, "tangents")
    uvs = bytes(vertex_count * 8)
    if "TEXCOORD_0" in attributes:
        uvs = cook_gltf_geometry.float_stream(document, buffer, attributes["TEXCOORD_0"], "VEC2", vertex_count, "UVs")
    uv1s = None
    if "TEXCOORD_1" in attributes:
        uv1s = cook_gltf_geometry.float_stream(document, buffer, attributes["TEXCOORD_1"], "VEC2", vertex_count,
            "lightmap UVs")
    skin_indices = skin_weights = None
    if "JOINTS_0" in attributes:
        skin_indices, skin_weights = cook_gltf_skin.read_influences(document, buffer, attributes, vertex_count)
    return {"positions": positions, "normals": normals, "tangents": tangents, "uvs": uvs,
        "uv1s": uv1s,
        "skin_indices": skin_indices, "skin_weights": skin_weights,
        "morph_targets": cook_gltf_geometry.read_morph_targets(document, buffer, targets, vertex_count),
        "count": vertex_count, "triangles": []}

\n