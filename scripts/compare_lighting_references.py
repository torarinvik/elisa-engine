#!/usr/bin/env python3
"""Compare native lighting captures with the SDL3/Metal visual references."""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

from compare_renders import compare, read_png, resample_nearest
from png_image import encode_png


ROOT = Path(__file__).resolve().parents[1]
CAPTURES = (
    ("point-light-left", "render-scene-point-light-left.png"),
    ("point-light-right", "render-scene-point-light-right.png"),
    ("shadows-disabled", "render-scene-shadows-disabled.png"),
    ("shadows-enabled", "render-scene-shadows-enabled.png"),
    ("outdoor", "render-scene-lighting-outdoor.png"),
    ("indoor", "render-scene-lighting-indoor.png"),
    ("translucent", "render-scene-lighting-transparent.png"),
    ("opaque", "render-scene-lighting-opaque.png"),
    ("receiver-bias-baseline", "render-scene-shadow-receiver-baseline.png"),
    ("receiver-bias-variant", "render-scene-shadow-receiver-variant.png"),
    ("rasterizer-bias-baseline", "render-scene-shadow-rasterizer-baseline.png"),
    ("rasterizer-bias-variant", "render-scene-shadow-rasterizer-variant.png"),
)
REFERENCE_WIDTH = 160
REFERENCE_HEIGHT = 100
DEFAULT_PEAK_TOLERANCE = 0.35
DEFAULT_MEAN_TOLERANCE = 0.025


def rgba_pixels(frame: tuple[int, int, int, bytearray]) -> bytes:
    channels = frame[2]
    pixels = frame[3]
    if channels == 4:
        return bytes(pixels)
    result = bytearray((len(pixels) // 3) * 4)
    for source in range(0, len(pixels), 3):
        target = source // 3 * 4
        result[target:target + 3] = pixels[source:source + 3]
        result[target + 3] = 255
    return bytes(result)


def require_reference_aspect(frame: tuple[int, int, int, bytearray], path: Path) -> None:
    width, height = frame[:2]
    if width * REFERENCE_HEIGHT != height * REFERENCE_WIDTH:
        raise ValueError(
            f"{path} has {width}x{height}; lighting references require a 16:10 capture"
        )


def update_reference(capture_path: Path, reference_path: Path) -> None:
    frame = read_png(capture_path)
    require_reference_aspect(frame, capture_path)
    reduced = resample_nearest(frame, REFERENCE_WIDTH, REFERENCE_HEIGHT)
    reference_path.parent.mkdir(parents=True, exist_ok=True)
    temporary = reference_path.with_name(reference_path.name + ".tmp")
    temporary.write_bytes(encode_png(
        REFERENCE_WIDTH, REFERENCE_HEIGHT, rgba_pixels(reduced)))
    os.replace(temporary, reference_path)


def compare_capture(capture_path: Path, reference_path: Path,
        peak_tolerance: float, mean_tolerance: float) -> tuple[float, float, bool]:
    capture = read_png(capture_path)
    require_reference_aspect(capture, capture_path)
    reference = read_png(reference_path)
    if reference[:2] != (REFERENCE_WIDTH, REFERENCE_HEIGHT):
        raise ValueError(
            f"{reference_path} must be {REFERENCE_WIDTH}x{REFERENCE_HEIGHT}, got "
            f"{reference[0]}x{reference[1]}"
        )
    reduced = resample_nearest(capture, REFERENCE_WIDTH, REFERENCE_HEIGHT)
    return compare(reduced, reference, peak_tolerance, mean_tolerance)


def parse_arguments(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture-dir", type=Path, default=ROOT / "build")
    parser.add_argument("--reference-dir", type=Path,
        default=ROOT / "docs/validation/references/lighting")
    parser.add_argument("--update", action="store_true",
        help="replace the small SDL3/Metal reference PNGs from current captures")
    parser.add_argument("--peak-tolerance", type=float, default=DEFAULT_PEAK_TOLERANCE,
        help="maximum per-channel difference on the 0..1 scale")
    parser.add_argument("--mean-tolerance", type=float, default=DEFAULT_MEAN_TOLERANCE,
        help="maximum mean per-pixel RGB difference on the 0..1 scale")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_arguments(argv)
    if not 0.0 <= args.peak_tolerance <= 1.0 or not 0.0 <= args.mean_tolerance <= 1.0:
        print("lighting tolerances must be between 0 and 1", file=sys.stderr)
        return 2

    failed = False
    try:
        for name, capture_name in CAPTURES:
            capture_path = args.capture_dir / capture_name
            reference_path = args.reference_dir / f"{name}.png"
            if args.update:
                update_reference(capture_path, reference_path)
                print(f"Updated {reference_path}")
                continue
            if not capture_path.is_file():
                raise FileNotFoundError(f"lighting capture is missing: {capture_path}")
            if not reference_path.is_file():
                raise FileNotFoundError(
                    f"lighting reference is missing: {reference_path}; run with --update after reviewing captures"
                )
            peak, mean, passed = compare_capture(capture_path, reference_path,
                args.peak_tolerance, args.mean_tolerance)
            status = "PASS" if passed else "FAIL"
            print(f"{status} {name}: peak={peak:.4f} mean={mean:.4f} "
                f"(limits {args.peak_tolerance:.4f}/{args.mean_tolerance:.4f})")
            failed = failed or not passed
    except (OSError, ValueError) as error:
        print(f"lighting reference check: {error}", file=sys.stderr)
        return 2
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
