#!/usr/bin/env python3
"""Check the two lit sides of the mirrored-UV normal-map capture."""

from __future__ import annotations

import sys

from compare_renders import read_png

LEFT_PATCH_X_PERMILLE = 350
RIGHT_PATCH_X_PERMILLE = 650
PATCH_Y_PERMILLE = 500
MINIMUM_CAPTURE_CONTRAST = 0.04
PIXEL_PATCH_WIDTH_RATIO = 160


def luminance(frame, x_permille: int, y_permille: int) -> float:
    width, height, channels, pixels = frame
    radius = max(2, width // PIXEL_PATCH_WIDTH_RATIO)
    center_x = min(width - radius - 1, max(radius, (width - 1) * x_permille // 1000))
    center_y = min(height - radius - 1, max(radius, (height - 1) * y_permille // 1000))
    return sum(0.2126 * pixels[(y * width + x) * channels] / 255.0 +
        0.7152 * pixels[(y * width + x) * channels + 1] / 255.0 +
        0.0722 * pixels[(y * width + x) * channels + 2] / 255.0
        for y in range(center_y - radius, center_y + radius + 1)
        for x in range(center_x - radius, center_x + radius + 1)) / ((2 * radius + 1) ** 2)


def main(arguments: list[str]) -> int:
    if len(arguments) != 1:
        print("usage: compare_mirrored_normal.py <capture.png>", file=sys.stderr)
        return 2
    frame = read_png(arguments[0])
    left = luminance(frame, LEFT_PATCH_X_PERMILLE, PATCH_Y_PERMILLE)
    right = luminance(frame, RIGHT_PATCH_X_PERMILLE, PATCH_Y_PERMILLE)
    contrast = abs(left - right)
    print(f"mirrored normal map: left={left:.4f} right={right:.4f} contrast={contrast:.4f}")
    return 0 if contrast >= MINIMUM_CAPTURE_CONTRAST else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
