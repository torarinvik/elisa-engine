"""Compare authored global joint palettes with the cooked rig at rest.

This regression gate checks preservation of glTF joint ancestors. Input is the explicitly
repaired benchmark derivative; no source geometry is modified here.
"""
from __future__ import annotations

import json
import math
from pathlib import Path
import struct

import authored_animation_fixture
import cook_animation_contract
import cook_assets
import cook_gltf_geometry
import cook_gltf_nodes
import cook_gltf_skin


def rows(column_major):
    return tuple(column_major[column * 4 + row] for row in range(3) for column in range(4))


def point(matrix, value):
    return tuple(sum(matrix[row * 4 + column] * value[column] for column in range(3))
        + matrix[row * 4 + 3] for row in range(3))


def bounds(points):
    return {"min": [min(p[axis] for p in points) for axis in range(3)],
        "max": [max(p[axis] for p in points) for axis in range(3)]}


def check(path: Path) -> dict:
    document = cook_assets.read_gltf(path.read_bytes())
    buffer = cook_assets.source_bytes(path.parent, document)
    nodes = document["nodes"]
    parents = {}
    for index, node in enumerate(nodes):
        for child in node.get("children", []):
            if child in parents:
                raise ValueError("source node has multiple parents")
            parents[child] = index
    worlds = {}

    def world(index, active=()):
        if index in active:
            raise ValueError("source hierarchy cycle")
        if index not in worlds:
            local = cook_gltf_nodes.local_matrix(nodes[index])
            worlds[index] = local if index not in parents else cook_gltf_nodes.multiply(
                world(parents[index], (*active, index)), local)
        return worlds[index]

    source_skin = document["skins"][0]
    inverse = cook_gltf_skin._read_inverse_bind(document, buffer, source_skin, len(source_skin["joints"]))
    source_palette = [cook_gltf_nodes.multiply(world(index), rows(inverse[bone * 16:bone * 16 + 16]))
        for bone, index in enumerate(source_skin["joints"])]
    # Read original vertices, before seam splitting, to retain correspondence.
    primitive = document["meshes"][0]["primitives"][0]
    source = cook_gltf_geometry.read_vertices(document, buffer, primitive["attributes"], [])
    positions = list(struct.iter_unpack("<3f", source["positions"]))
    indices, weights = source["skin_indices"], source["skin_weights"]
    rig = cook_gltf_skin.normalize(document, buffer)
    models = []
    for joint in rig["joints"]:
        local = rows(cook_animation_contract._matrix(joint["rest"]))
        models.append(local if joint["parent"] < 0 else cook_gltf_nodes.multiply(models[joint["parent"]], local))
    cooked_palette = [cook_gltf_nodes.multiply(models[index], rows(
        rig["inverse_bind_matrices"][bone * 16:bone * 16 + 16]))
        for bone, index in enumerate(rig["cluster_joints"])]

    def deform(palette, vertex_basis=cook_gltf_nodes.IDENTITY):
        output = []
        for vertex, position in enumerate(positions):
            position = point(vertex_basis, position)
            influences = [point(palette[indices[vertex * 4 + slot]], position) for slot in range(4)]
            output.append(tuple(sum(influences[slot][axis] * weights[vertex * 4 + slot]
                for slot in range(4)) for axis in range(3)))
        return output

    mesh_node = next(index for index, node in enumerate(nodes) if node.get("skin") == 0)
    expected, actual = deform(source_palette), deform(cooked_palette,
        rig["placement_palettes"][mesh_node].get("bind_shape", cook_gltf_nodes.IDENTITY))
    errors = [math.dist(a, b) for a, b in zip(expected, actual)]
    bind_shape = source_palette[0]
    bind_shape_spread = max(abs(a - b) for matrix in source_palette
        for a, b in zip(matrix, bind_shape))
    return {"source": str(path), "vertices": len(positions), "tolerance_m": 1e-5,
        "rest_bind_shape_row_major_3x4": bind_shape,
        "max_rest_bind_shape_matrix_spread": bind_shape_spread,
        "max_position_error_m": max(errors), "rms_position_error_m": math.sqrt(
            sum(error * error for error in errors) / len(errors)),
        "source_bounds": bounds(expected), "cooked_bounds": bounds(actual),
        "passed": max(errors) <= 1e-5}


if __name__ == "__main__":
    result = check(authored_animation_fixture.OUTPUT.with_suffix(".gltf"))
    destination = authored_animation_fixture.OUTPUT.with_suffix(".skin-space.json")
    destination.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
    raise SystemExit(0 if result["passed"] else 1)
