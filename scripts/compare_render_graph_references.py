#!/usr/bin/env python3
"""Compare the authored render-graph scene captures with SDL3/Metal references.

Render group 239 (`test/render_graph_authored_scene_native.elisa`) saves the
copy -> saturation -> 4x clear -> resolve -> blend graph before resize, after
resize, after a real minimize/restore and after an injected pass failure has
recovered, plus the same graph with a dedicated (non-aliased) resolve target.
The asynchronous readback capture (R17) is taken one frame after the recovered
one. The aliased base capture must match the dedicated one, the asynchronous
capture must match the synchronous recovered one, and each capture must
match its committed reference. The comparison uses the lighting references' 160x100 reduction and
peak/mean tolerances.
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

from compare_lighting_references import (DEFAULT_MEAN_TOLERANCE,
    DEFAULT_PEAK_TOLERANCE, REFERENCE_HEIGHT, REFERENCE_WIDTH,
    compare_capture, update_reference)
from compare_renders import compare, read_png, resample_nearest
import render_frame_report


ROOT = Path(__file__).resolve().parents[1]
CAPTURES = (
    ("dedicated", "ELISA_RENDER_GRAPH_SCENE_DEDICATED_CAPTURE"),
    ("base", "ELISA_RENDER_GRAPH_SCENE_BASE_CAPTURE"),
    ("resized", "ELISA_RENDER_GRAPH_SCENE_RESIZED_CAPTURE"),
    ("restored", "ELISA_RENDER_GRAPH_SCENE_RESTORED_CAPTURE"),
    ("recovered", "ELISA_RENDER_GRAPH_SCENE_RECOVERED_CAPTURE"),
    # R17: the same recovered scene read back through the asynchronous queue.
    ("async", "ELISA_RENDER_GRAPH_SCENE_ASYNC_CAPTURE"),
)


def capture_path(build: Path, name: str) -> Path:
    return build / f"render-graph-scene-{name}.png"


def prepare(build: Path, environment: dict[str, str]) -> None:
    """Remove stale captures and point the native smoke at fresh paths."""
    for name, variable in CAPTURES:
        path = capture_path(build, name)
        path.unlink(missing_ok=True)
        environment[variable] = str(path)
    # R20: the same scene's per-pass frame report (render group 240).
    render_frame_report.prepare(build, environment)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture-dir", type=Path, default=ROOT / "build")
    parser.add_argument("--reference-dir", type=Path,
        default=ROOT / "docs/validation/references/render-graph")
    parser.add_argument("--update", action="store_true",
        help="replace the reference PNGs from current captures")
    args = parser.parse_args(argv)
    if os.environ.get("ELISA_UPDATE_RENDER_GRAPH_REFERENCES") == "1":
        args.update = True
    failed = False
    try:
        aliased, dedicated = (resample_nearest(read_png(capture_path(args.capture_dir, name)),
            REFERENCE_WIDTH, REFERENCE_HEIGHT) for name in ("base", "dedicated"))
        peak, mean, passed = compare(aliased, dedicated,
            DEFAULT_PEAK_TOLERANCE, DEFAULT_MEAN_TOLERANCE)
        print(f"{'PASS' if passed else 'FAIL'} render-graph aliased vs dedicated: "
            f"peak={peak:.4f} mean={mean:.4f}")
        failed = not passed
        asynchronous, synchronous = (resample_nearest(read_png(capture_path(args.capture_dir, name)),
            REFERENCE_WIDTH, REFERENCE_HEIGHT) for name in ("async", "recovered"))
        peak, mean, passed = compare(asynchronous, synchronous,
            DEFAULT_PEAK_TOLERANCE, DEFAULT_MEAN_TOLERANCE)
        print(f"{'PASS' if passed else 'FAIL'} render-graph async vs recovered: "
            f"peak={peak:.4f} mean={mean:.4f}")
        failed = failed or not passed
        for name, _ in CAPTURES:
            capture = capture_path(args.capture_dir, name)
            reference = args.reference_dir / f"{name}.png"
            if not capture.is_file():
                raise FileNotFoundError(f"render graph capture is missing: {capture}")
            if args.update:
                update_reference(capture, reference)
                print(f"Updated {reference}")
                continue
            if not reference.is_file():
                raise FileNotFoundError(f"render graph reference is missing: {reference}")
            peak, mean, passed = compare_capture(capture, reference,
                DEFAULT_PEAK_TOLERANCE, DEFAULT_MEAN_TOLERANCE)
            print(f"{'PASS' if passed else 'FAIL'} render-graph {name}: "
                f"peak={peak:.4f} mean={mean:.4f}")
            failed = failed or not passed
    except (OSError, ValueError) as error:
        print(f"render graph reference check: {error}", file=sys.stderr)
        return 2
    return 1 if failed else render_frame_report.check(args.capture_dir)


if __name__ == "__main__":
    raise SystemExit(main())
