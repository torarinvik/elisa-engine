"""Transform authored glTF tangent frames through baked node transforms."""

from __future__ import annotations

import math
import struct

import cook_gltf_nodes


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
