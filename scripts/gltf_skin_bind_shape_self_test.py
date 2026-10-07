"""Outcome checks for a common skin bind shape baked into vertex streams."""
import struct
import sys


def translation_preserves_streams(document, shifted, normalized):
    baseline_geometry, shifted_geometry = normalized(document), normalized(shifted)
    baseline_points = list(struct.iter_unpack("<3f", baseline_geometry["positions"]))
    shifted_points = list(struct.iter_unpack("<3f", shifted_geometry["positions"]))
    displacement = (5.0, -2.0, 2.75)
    if (len(baseline_points) != len(shifted_points) or
            any(abs(after[axis] - before[axis] - displacement[axis]) > 1e-5
                for before, after in zip(baseline_points, shifted_points) for axis in range(3)) or
            any(baseline_geometry[key] != shifted_geometry[key]
                for key in ("normals", "tangents", "morph_targets"))):
        print("glTF skin self-test failed: common bind-shape translation changed directions or lost positions",
            file=sys.stderr)
        return False
    return True
