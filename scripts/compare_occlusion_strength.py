#!/usr/bin/env python3
"""Verify that glTF occlusion strength changes rendered indirect light."""

from __future__ import annotations

import sys

from compare_mirrored_normal import luminance
from compare_renders import read_png

LEFT_PATCH_X_PERMILLE = 350
RIGHT_PATCH_X_PERMILLE = 650
PATCH_Y_PERMILLE = 500
MINIMUM_LUMINANCE_CHANGE = 0.01


def main(arguments: list[str]) -> int:
    if len(arguments) != 2:
        print("usage: compare_occlusion_strength.py <occluded.png> <strength-zero.png>",
            file=sys.stderr)
        return 2
    occluded, unoccluded = (read_png(path) for path in arguments)
    if occluded[:3] != unoccluded[:3]:
        print("AO reference captures have different dimensions or formats", file=sys.stderr)
        return 1
    points = ((LEFT_PATCH_X_PERMILLE, PATCH_Y_PERMILLE),
        (RIGHT_PATCH_X_PERMILLE, PATCH_Y_PERMILLE))
    with_ao = sum(luminance(occluded, x, y) for x, y in points) / len(points)
    without_ao = sum(luminance(unoccluded, x, y) for x, y in points) / len(points)
    change = without_ao - with_ao
    print(f"occlusion strength: strength=0.65 luminance={with_ao:.4f}, "
        f"strength=0 luminance={without_ao:.4f}, darkening={change:.4f}")
    if change < MINIMUM_LUMINANCE_CHANGE:
        print(f"AO strength darkening is below {MINIMUM_LUMINANCE_CHANGE:.3f}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
