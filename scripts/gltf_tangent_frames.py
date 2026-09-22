"""Deterministic tangent-frame generation for cooked glTF geometry."""

from __future__ import annotations

import math
import struct

import cook_gltf_nodes


def generated(positions: bytes, normals: bytes, uvs: bytes,
        vertex_count: int, indices: list[int]) -> bytes:
    """Generate float4 frames from the final transformed indexed streams."""
    points = list(struct.iter_unpack("<3f", positions))
    normal_values = list(struct.iter_unpack("<3f", normals))
    uv_values = list(struct.iter_unpack("<2f", uvs))
    tangent_sum = [[0.0, 0.0, 0.0] for _ in range(vertex_count)]
    bitangent_sum = [[0.0, 0.0, 0.0] for _ in range(vertex_count)]
    for triangle in range(0, len(indices), 3):
        i0, i1, i2 = indices[triangle:triangle + 3]
        edge1 = tuple(points[i1][axis] - points[i0][axis] for axis in range(3))
        edge2 = tuple(points[i2][axis] - points[i0][axis] for axis in range(3))
        du1 = uv_values[i1][0] - uv_values[i0][0]
        dv1 = uv_values[i1][1] - uv_values[i0][1]
        du2 = uv_values[i2][0] - uv_values[i0][0]
        dv2 = uv_values[i2][1] - uv_values[i0][1]
        determinant = du1 * dv2 - du2 * dv1
        if abs(determinant) <= 1.0e-20 or not math.isfinite(determinant):
            continue
        inverse = 1.0 / determinant
        tangent = tuple((edge1[axis] * dv2 - edge2[axis] * dv1) * inverse for axis in range(3))
        bitangent = tuple((edge2[axis] * du1 - edge1[axis] * du2) * inverse for axis in range(3))
        for vertex in (i0, i1, i2):
            for axis in range(3):
                tangent_sum[vertex][axis] += tangent[axis]
                bitangent_sum[vertex][axis] += bitangent[axis]
    packed = bytearray()
    for vertex, normal_value in enumerate(normal_values):
        normal_length = math.sqrt(sum(component * component for component in normal_value))
        if not math.isfinite(normal_length) or normal_length <= 1.0e-20:
            raise ValueError("cannot generate a tangent for a zero-length normal")
        normal = tuple(component / normal_length for component in normal_value)
        projection = sum(normal[axis] * tangent_sum[vertex][axis] for axis in range(3))
        tangent = tuple(tangent_sum[vertex][axis] - normal[axis] * projection for axis in range(3))
        tangent_length = math.sqrt(sum(component * component for component in tangent))
        if not math.isfinite(tangent_length) or tangent_length <= 1.0e-20:
            axis = (1.0, 0.0, 0.0) if abs(normal[0]) < 0.75 else (0.0, 1.0, 0.0)
            tangent = (normal[1] * axis[2] - normal[2] * axis[1],
                normal[2] * axis[0] - normal[0] * axis[2],
                normal[0] * axis[1] - normal[1] * axis[0])
            tangent_length = math.sqrt(sum(component * component for component in tangent))
        if not math.isfinite(tangent_length) or tangent_length <= 1.0e-20:
            raise ValueError("cannot generate a tangent frame")
        tangent = tuple(component / tangent_length for component in tangent)
        cross = (normal[1] * tangent[2] - normal[2] * tangent[1],
            normal[2] * tangent[0] - normal[0] * tangent[2],
            normal[0] * tangent[1] - normal[1] * tangent[0])
        orientation = sum(cross[axis] * bitangent_sum[vertex][axis] for axis in range(3))
        packed += struct.pack("<4f", *tangent, -1.0 if orientation < 0.0 else 1.0)
    return bytes(packed)


def transformed(data: bytes, normals: bytes, matrix: tuple) -> bytes:
    """Transform authored tangent frames and preserve their handedness."""
    normal_values = list(struct.iter_unpack("<3f", normals))
    packed = bytearray()
    determinant_sign = 1.0 if cook_gltf_nodes.determinant(matrix) > 0.0 else -1.0
    for vertex, (tx, ty, tz, handedness) in enumerate(struct.iter_unpack("<4f", data)):
        if abs(abs(handedness) - 1.0) > 1.0e-4:
            raise ValueError("authored tangent handedness must be -1 or 1")
        nx, ny, nz = normal_values[vertex]
        normal_length = math.sqrt(nx * nx + ny * ny + nz * nz)
        if not math.isfinite(normal_length) or normal_length <= 1.0e-20:
            raise ValueError("authored tangent has a zero-length normal")
        nx, ny, nz = nx / normal_length, ny / normal_length, nz / normal_length
        tangent = (matrix[0] * tx + matrix[1] * ty + matrix[2] * tz,
            matrix[4] * tx + matrix[5] * ty + matrix[6] * tz,
            matrix[8] * tx + matrix[9] * ty + matrix[10] * tz)
        projection = nx * tangent[0] + ny * tangent[1] + nz * tangent[2]
        tangent = (tangent[0] - nx * projection, tangent[1] - ny * projection,
            tangent[2] - nz * projection)
        length = math.sqrt(sum(component * component for component in tangent))
        if not math.isfinite(length) or length <= 1.0e-20:
            raise ValueError("authored tangent is parallel to its normal")
        packed += struct.pack("<4f", *(component / length for component in tangent),
            handedness * determinant_sign)
    return bytes(packed)
