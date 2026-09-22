"""Flatten a static glTF node hierarchy into world-space mesh placements.

Matrices here are affine 3x4, row-major: (m00, m01, m02, m03, m10, ...), where
the last column is the translation.
"""

from __future__ import annotations

import math
import struct


MAX_NODES = 256
NODE_KEYS = {"name", "mesh", "skin", "children", "matrix", "translation", "rotation", "scale", "camera", "extensions"}
IDENTITY = (1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0)
FLOAT_MAX = 3.4028234663852886e38
# glTF requires unit rotations; exporters round them.
ROTATION_TOLERANCE = 1e-3


def finite_numbers(value, count: int, label: str) -> list[float]:
    if (not isinstance(value, list) or len(value) != count or
            any(type(component) not in (int, float) or not math.isfinite(component) for component in value)):
        raise ValueError(f"node {label} must list {count} finite numbers")
    return [float(component) for component in value]


def local_matrix(node: dict) -> tuple:
    if "matrix" in node:
        if {"translation", "rotation", "scale"} & set(node):
            raise ValueError("a node matrix excludes translation, rotation, and scale")
        m = finite_numbers(node["matrix"], 16, "matrix")
        # glTF stores matrices column-major; the bottom row must be 0 0 0 1.
        if (m[3], m[7], m[11], m[15]) != (0.0, 0.0, 0.0, 1.0):
            raise ValueError("node matrix must be affine")
        return (m[0], m[4], m[8], m[12], m[1], m[5], m[9], m[13], m[2], m[6], m[10], m[14])
    tx, ty, tz = finite_numbers(node.get("translation", [0.0, 0.0, 0.0]), 3, "translation")
    x, y, z, w = finite_numbers(node.get("rotation", [0.0, 0.0, 0.0, 1.0]), 4, "rotation")
    length = math.sqrt(x * x + y * y + z * z + w * w)
    if abs(length - 1.0) > ROTATION_TOLERANCE:
        raise ValueError("node rotation must be a unit quaternion")
    x, y, z, w = x / length, y / length, z / length, w / length
    sx, sy, sz = finite_numbers(node.get("scale", [1.0, 1.0, 1.0]), 3, "scale")
    return (
        (1.0 - 2.0 * (y * y + z * z)) * sx, 2.0 * (x * y - z * w) * sy, 2.0 * (x * z + y * w) * sz, tx,
        2.0 * (x * y + z * w) * sx, (1.0 - 2.0 * (x * x + z * z)) * sy, 2.0 * (y * z - x * w) * sz, ty,
        2.0 * (x * z - y * w) * sx, 2.0 * (y * z + x * w) * sy, (1.0 - 2.0 * (x * x + y * y)) * sz, tz,
    )


def multiply(a: tuple, b: tuple) -> tuple:
    """a applied after b."""
    result = []
    for row in range(3):
        r = a[row * 4:row * 4 + 4]
        for column in range(4):
            value = r[0] * b[column] + r[1] * b[4 + column] + r[2] * b[8 + column]
            result.append(value + r[3] if column == 3 else value)
    return tuple(result)


def cofactors(m: tuple) -> tuple:
    """The linear part's cofactor matrix, row-major 3x3: its determinant
    times the inverse transpose, which carries normals."""
    a00, a01, a02, a10, a11, a12, a20, a21, a22 = m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10]
    return (a11 * a22 - a12 * a21, a12 * a20 - a10 * a22, a10 * a21 - a11 * a20,
        a02 * a21 - a01 * a22, a00 * a22 - a02 * a20, a01 * a20 - a00 * a21,
        a01 * a12 - a02 * a11, a02 * a10 - a00 * a12, a00 * a11 - a01 * a10)


def determinant(m: tuple) -> float:
    c = cofactors(m)
    return m[0] * c[0] + m[1] * c[1] + m[2] * c[2]


def mesh_placements(document: dict, mesh_count: int) -> list[tuple[int, tuple]]:
    """Return (mesh index, world matrix) for every node that places a mesh,
    depth first in scene order, a node before its children. The nodes must
    form one scene's forest; each mesh must be placed at least once."""
    nodes = document.get("nodes", [])
    if not isinstance(nodes, list) or not 1 <= len(nodes) <= MAX_NODES:
        raise ValueError(f"runtime geometry cooker requires 1 to {MAX_NODES} nodes")
    parents = [None] * len(nodes)
    locals_ = []
    for index, node in enumerate(nodes):
        if not isinstance(node, dict) or set(node) - NODE_KEYS:
            raise ValueError("runtime geometry cooker encountered unsupported node properties")
        mesh = node.get("mesh")
        if "mesh" in node and (type(mesh) is not int or not 0 <= mesh < mesh_count):
            raise ValueError("node mesh index out of range")
        children = node.get("children", [])
        if not isinstance(children, list):
            raise ValueError("node children must list node indices")
        for child in children:
            if type(child) is not int or not 0 <= child < len(nodes):
                raise ValueError("node child index out of range")
            if child == index or parents[child] is not None:
                raise ValueError("a node must have at most one parent")
            parents[child] = index
        locals_.append(local_matrix(node))

    scenes = document.get("scenes", [])
    if (not isinstance(scenes, list) or len(scenes) != 1 or document.get("scene") != 0 or
            not isinstance(scenes[0], dict) or set(scenes[0]) - {"nodes", "name"}):
        raise ValueError("runtime geometry cooker requires exactly one scene")
    roots = scenes[0].get("nodes")
    if (not isinstance(roots, list) or not roots or
            any(type(root) is not int or not 0 <= root < len(nodes) for root in roots) or
            len(set(roots)) != len(roots)):
        raise ValueError("the scene must list distinct node indices")
    if any(parents[root] is not None for root in roots):
        raise ValueError("scene nodes must be hierarchy roots")

    placements = []
    visited = [False] * len(nodes)
    pending = [(root, IDENTITY) for root in reversed(roots)]
    while pending:
        index, parent_world = pending.pop()
        visited[index] = True
        world = multiply(parent_world, locals_[index])
        if "mesh" in nodes[index]:
            if not all(math.isfinite(value) for value in world):
                raise ValueError("node transforms must be finite and invertible")
            det = determinant(world)
            if not math.isfinite(det) or det == 0.0:
                raise ValueError("node transforms must be finite and invertible")
            placements.append((nodes[index]["mesh"], world))
        pending.extend((child, world) for child in reversed(nodes[index].get("children", [])))
    # Parent links are unique and roots have none, so a cycle is unreachable.
    if not all(visited):
        raise ValueError("every node must belong to the scene's hierarchy")
    if {mesh for mesh, _ in placements} != set(range(mesh_count)):
        raise ValueError("every mesh must be placed by a node")
    return placements


def packed_float3(values: tuple) -> bytes:
    if not all(math.isfinite(value) and abs(value) <= FLOAT_MAX for value in values):
        raise ValueError("node transform moves a vertex outside the float range")
    return struct.pack("<3f", *values)


def transform_points(data: bytes, m: tuple) -> bytes:
    if m == IDENTITY:
        return data
    return b"".join(packed_float3((
        m[0] * x + m[1] * y + m[2] * z + m[3],
        m[4] * x + m[5] * y + m[6] * z + m[7],
        m[8] * x + m[9] * y + m[10] * z + m[11],
    )) for x, y, z in struct.iter_unpack("<3f", data))


def transform_vectors(data: bytes, m: tuple) -> bytes:
    """Apply only the linear part of an affine node transform to vectors."""
    if m == IDENTITY:
        return data
    return b"".join(packed_float3((
        m[0] * x + m[1] * y + m[2] * z,
        m[4] * x + m[5] * y + m[6] * z,
        m[8] * x + m[9] * y + m[10] * z,
    )) for x, y, z in struct.iter_unpack("<3f", data))


def transform_normal_deltas(data: bytes, m: tuple) -> bytes:
    """Apply the inverse-transpose linear transform without normalizing."""
    if m == IDENTITY:
        return data
    c = cofactors(m)
    sign = 1.0 if determinant(m) > 0.0 else -1.0
    return b"".join(packed_float3((
        sign * (c[0] * x + c[1] * y + c[2] * z),
        sign * (c[3] * x + c[4] * y + c[5] * z),
        sign * (c[6] * x + c[7] * y + c[8] * z),
    )) for x, y, z in struct.iter_unpack("<3f", data))


def transform_normals(data: bytes, m: tuple) -> bytes:
    """Carry normals by the inverse transpose, renormalized. A mirroring
    transform's cofactors point normals inward, so they flip back."""
    if m == IDENTITY:
        return data
    c = cofactors(m)
    sign = 1.0 if determinant(m) > 0.0 else -1.0
    packed = bytearray()
    for x, y, z in struct.iter_unpack("<3f", data):
        normal = (sign * (c[0] * x + c[1] * y + c[2] * z),
            sign * (c[3] * x + c[4] * y + c[5] * z),
            sign * (c[6] * x + c[7] * y + c[8] * z))
        length = math.sqrt(sum(component * component for component in normal))
        if not math.isfinite(length) or length == 0.0:
            raise ValueError("cannot transform a zero-length normal")
        packed += struct.pack("<3f", *(component / length for component in normal))
    return bytes(packed)


def placed_indices(values: list[int], m: tuple) -> list[int]:
    """A mirroring transform reverses triangle winding; swap it back so the
    authored front faces stay front-facing."""
    if determinant(m) > 0.0:
        return values
    flipped = list(values)
    flipped[1::3], flipped[2::3] = values[2::3], values[1::3]
    return flipped
