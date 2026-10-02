#!/usr/bin/env python3
"""R20 frame report for the authored render-graph scene (render group 240).

The native render smoke links the pinned Tracy client (when fetched), points
`ELISA_RENDER_FRAME_REPORT` at `build/render-frame-report.json`, and the Elisa
client fails group 240 when a frame leaves its committed cold/warm budget.
This module checks the written report's shape and labels, then prints the
cold frame and the warm median/max so repeated runs can be compared:

  python3 scripts/render_frame_report.py [--report build/render-frame-report.json]
"""

from __future__ import annotations

import argparse
import json
import statistics
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PASSES = ("update", "scene", "compose")
FIELDS = ("cpu_us", "draws", "upload_bytes", "resource_bytes")
REPORT_NAME = "render-frame-report.json"


def tracy_args(root: Path) -> list[str]:
    """Compiler arguments that link the pinned Tracy client, if it is fetched."""
    client = root / "dependencies/tracy/public/TracyClient.cpp"
    if not client.is_file():
        print("Tracy is not fetched; frame-report zones compile away "
            "(run python3 scripts/fetch_tracy.py).", flush=True)
        return []
    return ["-I", str(client.parent), "-DTRACY_ENABLE", str(client)]


def prepare(build: Path, environment: dict[str, str]) -> None:
    path = build / REPORT_NAME
    path.unlink(missing_ok=True)
    environment["ELISA_RENDER_FRAME_REPORT"] = str(path)


def validate(report: dict) -> list[str]:
    problems = []
    frames = report.get("frames")
    if not isinstance(frames, list) or len(frames) < 2:
        return ["report needs a cold frame and at least one warm frame"]
    for index, frame in enumerate(frames):
        if [row.get("pass") for row in frame] != list(PASSES):
            problems.append(f"frame {index}: passes are not {PASSES}")
            continue
        for row in frame:
            gpu = row.get("gpu_us")
            if gpu != "unavailable" and not (isinstance(gpu, int) and gpu >= 0):
                problems.append(f"frame {index} {row['pass']}: gpu_us {gpu!r} is neither "
                    "a measurement nor labelled unavailable")
            for field in FIELDS:
                if not isinstance(row.get(field), int) or row[field] < 0:
                    problems.append(f"frame {index} {row['pass']}: bad {field}")
    if not report.get("timestamps") and any(row["gpu_us"] != "unavailable"
            for frame in frames for row in frame if row.get("pass") != "update"):
        problems.append("GPU times reported without timestamp queries")
    return problems


def frame_total(frame: list[dict], field: str) -> int | str:
    if field == "gpu_us" and any(row["gpu_us"] == "unavailable" for row in frame):
        return "unavailable"
    return sum(row[field] for row in frame)


def describe(report: dict) -> list[str]:
    frames = report["frames"]
    lines = [f"frame report: backend={report.get('backend')} timestamps={report.get('timestamps')} "
        f"tracy={report.get('tracy')} frames={len(frames)}"]
    cold = frames[0]
    lines.append("cold: " + " ".join(f"{field}={frame_total(cold, field)}"
        for field in ("cpu_us", "gpu_us", "draws", "upload_bytes", "resource_bytes")))
    for name in PASSES:
        row = next(row for row in cold if row["pass"] == name)
        lines.append(f"  cold {name}: cpu_us={row['cpu_us']} gpu_us={row['gpu_us']} draws={row['draws']} "
            f"upload_bytes={row['upload_bytes']}")
    warm = frames[1:]
    for field in ("cpu_us", "gpu_us", "draws", "upload_bytes", "resource_bytes"):
        values = [frame_total(frame, field) for frame in warm]
        measured = [value for value in values if value != "unavailable"]
        unavailable = len(values) - len(measured)
        text = (f"median={int(statistics.median(measured))} max={max(measured)}" if measured else "unavailable")
        lines.append(f"warm {field}: {text}" + (f" unavailable_frames={unavailable}" if unavailable else ""))
    return lines


def check(build: Path) -> int:
    path = build / REPORT_NAME
    try:
        report = json.loads(path.read_text())
    except (OSError, ValueError) as error:
        print(f"render frame report: {error}", file=sys.stderr)
        return 2
    problems = validate(report)
    for problem in problems:
        print(f"render frame report: {problem}", file=sys.stderr)
    if problems:
        return 1
    for line in describe(report):
        print(line)
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path, default=ROOT / "build" / REPORT_NAME)
    args = parser.parse_args(argv)
    return check(args.report.parent) if args.report.name == REPORT_NAME else 2


if __name__ == "__main__":
    raise SystemExit(main())
