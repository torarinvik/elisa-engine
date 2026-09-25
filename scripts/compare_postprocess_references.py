#!/usr/bin/env python3
"""Compare native High/Low post-process captures with SDL3/Metal references."""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

from compare_renders import compare, read_png, resample_nearest
from png_image import encode_png


ROOT = Path(__file__).resolve().parents[1]
CAPTURES = (
    ("high", "render-scene-postprocess-high.png"),
    ("low", "render-scene-postprocess-low.png"),
)
REFERENCE_WIDTH = 160
REFERENCE_HEIGHT = 100
PEAK_TOLERANCE = 0.35
MEAN_TOLERANCE = 0.025


def reference_image(frame: tuple[int, int, int, bytearray], path: Path) -> None:
    width, height = frame[:2]
    if width * REFERENCE_HEIGHT != height * REFERENCE_WIDTH:
        raise ValueError(f"{path} must have a 16:10 aspect ratio")
    reduced = resample_nearest(frame, REFERENCE_WIDTH, REFERENCE_HEIGHT)
    pixels = reduced[3]
    channels = reduced[2]
    rgba = bytearray(REFERENCE_WIDTH * REFERENCE_HEIGHT * 4)
    for index in range(REFERENCE_WIDTH * REFERENCE_HEIGHT):
        source = index * channels
        target = index * 4
        rgba[target:target + 3] = pixels[source:source + 3]
        rgba[target + 3] = pixels[source + 3] if channels == 4 else 255
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_bytes(encode_png(REFERENCE_WIDTH, REFERENCE_HEIGHT, bytes(rgba)))
    os.replace(temporary, path)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture-dir", type=Path, default=ROOT / "build")
    parser.add_argument("--reference-dir", type=Path,
        default=ROOT / "docs/validation/references/postprocess")
    parser.add_argument("--update", action="store_true",
        help="replace references from the current native captures")
    args = parser.parse_args()

    try:
        for name, filename in CAPTURES:
            capture_path = args.capture_dir / filename
            reference_path = args.reference_dir / f"{name}.png"
            if not capture_path.is_file():
                raise FileNotFoundError(f"post-process capture is missing: {capture_path}")
            capture = read_png(capture_path)
            if args.update:
                reference_image(capture, reference_path)
                print(f"Updated {reference_path}")
                continue
            if not reference_path.is_file():
                raise FileNotFoundError(f"post-process reference is missing: {reference_path}")
            reference = read_png(reference_path)
            if reference[:2] != (REFERENCE_WIDTH, REFERENCE_HEIGHT):
                raise ValueError(f"{reference_path} must be {REFERENCE_WIDTH}x{REFERENCE_HEIGHT}")
            reduced = resample_nearest(capture, REFERENCE_WIDTH, REFERENCE_HEIGHT)
            peak, mean, passed = compare(reduced, reference,
                PEAK_TOLERANCE, MEAN_TOLERANCE)
            print(f"{'PASS' if passed else 'FAIL'} postprocess-{name}: "
                f"peak={peak:.4f} mean={mean:.4f}")
            if not passed:
                return 1
    except (OSError, ValueError) as failure:
        print(f"post-process reference comparison failed: {failure}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
