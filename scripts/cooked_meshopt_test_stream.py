"""Decode one cooked vertex stream for offline cooker regression tests."""

from __future__ import annotations

import base64

from cook_gltf_meshopt_streams import VERTEX_STREAM, decode_streams


def decode_vertex_stream(fields: dict[str, str], name: str, vertex_count: int,
        vertex_stride: int) -> bytes:
    if type(vertex_count) is not int or type(vertex_stride) is not int or \
            vertex_count <= 0 or vertex_stride <= 0:
        raise ValueError("cooked vertex stream dimensions must be positive integers")
    raw = fields.get(f"{name}_b64")
    encoded = fields.get(f"{name}_meshopt_b64")
    if (raw is None) == (encoded is None):
        raise ValueError(f"cooked package has duplicate or missing {name} streams")
    if raw is not None:
        decoded = base64.b64decode(raw, validate=True)
    else:
        decoded = decode_streams([(name, VERTEX_STREAM, vertex_count, vertex_stride,
            base64.b64decode(encoded, validate=True))])[name]
    if len(decoded) != vertex_count * vertex_stride:
        raise ValueError(f"cooked vertex stream {name} has the wrong byte length")
    return decoded
