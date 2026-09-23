"""Regression checks for fixed-rate glTF animation interpolation."""

from __future__ import annotations

import base64
from copy import deepcopy
import math
import struct
import sys

import cook_gltf_animation


def interpolation_self_test(document: dict, append, normalized) -> int:
    """Verify cubic TRS sampling, quaternion normalization, and linear SLERP."""
    cubic = deepcopy(document)
    cubic_buffer = bytearray(base64.b64decode(cubic["buffers"][0]["uri"].split(",", 1)[1]))
    cubic_values = (0.0, 0.0, 0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
        0.0, 2.0, 0.0, 0.0, 0.0, 0.0)
    cubic_accessor = append(cubic, cubic_buffer,
        struct.pack(f"<{len(cubic_values)}f", *cubic_values), 5126, "VEC3", 6)
    cubic["animations"][0]["samplers"][0].update({
        "output": cubic_accessor, "interpolation": "CUBICSPLINE"})
    cubic["buffers"][0]["byteLength"] = len(cubic_buffer)
    cubic["buffers"][0]["uri"] = "data:application/octet-stream;base64," + \
        base64.b64encode(cubic_buffer).decode("ascii")
    cubic_geometry = normalized(cubic)
    rig_index = cubic_geometry["skin"]["source_node_indices"][2]
    rig_index = rig_index[0] if isinstance(rig_index, list) else rig_index
    samples = cubic_geometry["animation_clips"][0]["samples"]
    midpoint_y = samples[(15 * len(cubic_geometry["skin"]["joints"]) + rig_index) * 10 + 1]
    if abs(midpoint_y - 1.5) > 1.0e-6:
        print("glTF animation self-test failed: cubic translation was not Hermite sampled",
            file=sys.stderr)
        return 1

    cubic_rotation = deepcopy(document)
    rotation_buffer = bytearray(base64.b64decode(
        cubic_rotation["buffers"][0]["uri"].split(",", 1)[1]))
    zero = (0.0, 0.0, 0.0, 0.0)
    quaternion_values = (*zero, 0.0, 0.0, 0.0, 1.0, *zero, *zero,
        0.0, 0.0, 2.0 ** -0.5, 2.0 ** -0.5, *zero)
    rotation_accessor = append(cubic_rotation, rotation_buffer,
        struct.pack(f"<{len(quaternion_values)}f", *quaternion_values), 5126, "VEC4", 6)
    cubic_rotation["animations"][0]["channels"][0]["target"]["path"] = "rotation"
    cubic_rotation["animations"][0]["samplers"][0].update({
        "output": rotation_accessor, "interpolation": "CUBICSPLINE"})
    cubic_rotation["buffers"][0]["byteLength"] = len(rotation_buffer)
    cubic_rotation["buffers"][0]["uri"] = "data:application/octet-stream;base64," + \
        base64.b64encode(rotation_buffer).decode("ascii")
    rotation_geometry = normalized(cubic_rotation)
    rotation_index = rotation_geometry["skin"]["source_node_indices"][2]
    rotation_index = rotation_index[0] if isinstance(rotation_index, list) else rotation_index
    rotation_samples = rotation_geometry["animation_clips"][0]["samples"]
    start = (15 * len(rotation_geometry["skin"]["joints"]) + rotation_index) * 10 + 3
    midpoint = rotation_samples[start:start + 4]
    if abs(sum(value * value for value in midpoint) - 1.0) > 1.0e-5:
        print("glTF animation self-test failed: cubic quaternion was not normalized",
            file=sys.stderr)
        return 1

    angle = 2.0 * math.pi / 3.0
    endpoint = (0.0, 0.0, math.sin(angle / 2.0), math.cos(angle / 2.0))
    track = ([0.0, 1.0], [(0.0, 0.0, 0.0, 1.0), endpoint], "LINEAR")
    quarter = cook_gltf_animation._sample(track, 0.25, "linear rotation", quaternion=True)
    if (abs(quarter[2] - math.sin(math.pi / 12.0)) > 1.0e-6 or
            abs(quarter[3] - math.cos(math.pi / 12.0)) > 1.0e-6):
        print("glTF animation self-test failed: LINEAR rotation did not use spherical interpolation",
            file=sys.stderr)
        return 1
    return 0
