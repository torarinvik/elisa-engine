#!/usr/bin/env python3
"""Compare Godot Low/High quality captures and require a rendered change."""

from __future__ import annotations

import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys

from compare_renders import compare, read_png


ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build" / "godot-quality"
REFERENCES = ROOT / "docs" / "validation" / "references" / "godot-quality"
REFERENCE_PEAK = 0.12
REFERENCE_MEAN = 0.01
MIN_CHANGED_PIXELS = 1024
MIN_MEAN_DIFFERENCE = 0.02


def changed_pixels(first, second) -> int:
    if first[:3] != second[:3]:
        raise ValueError("Godot quality frames have mismatched dimensions or channels")
    channels = first[2]
    return sum(
        max(abs(first[3][offset + channel] - second[3][offset + channel]) for channel in range(3)) > 3
        for offset in range(0, len(first[3]), channels)
    )


def main() -> int:
    godot = os.environ.get("GODOT_BIN") or shutil.which("godot")
    if not godot:
        print("Install Godot or set GODOT_BIN to its executable path.", file=sys.stderr)
        return 2
    rendering_method = os.environ.get("GODOT_RENDERING_METHOD", "gl_compatibility")
    low_path = BUILD / f"low-{rendering_method}.png"
    high_path = BUILD / f"high-{rendering_method}.png"
    measurements_path = BUILD / f"measurements-{rendering_method}.json"
    pending_measurements_path = BUILD / f".measurements-{rendering_method}.pending.json"
    try:
        timeout_seconds = int(os.environ.get("GODOT_QUALITY_TIMEOUT_SECONDS", "240"))
        if timeout_seconds <= 0:
            raise ValueError("timeout must be positive")
    except ValueError as error:
        print(f"Invalid GODOT_QUALITY_TIMEOUT_SECONDS: {error}", file=sys.stderr)
        return 2
    BUILD.mkdir(parents=True, exist_ok=True)
    for capture in (low_path, high_path, pending_measurements_path):
        capture.unlink(missing_ok=True)
    command = [
        godot,
        "--disable-vsync",
        "--rendering-method", rendering_method,
        "--path", str(ROOT / "backends/godot"),
        "--script", "res://quality_capture.gd",
        "--", str(low_path), str(high_path), str(pending_measurements_path),
    ]
    try:
        result = subprocess.run(command, cwd=ROOT, check=False, timeout=timeout_seconds)
    except subprocess.TimeoutExpired:
        pending_measurements_path.unlink(missing_ok=True)
        print(
            f"Godot quality capture exceeded {timeout_seconds} seconds; "
            "set GODOT_QUALITY_TIMEOUT_SECONDS or reduce "
            "ELISA_GODOT_QUALITY_MEASURED_FRAMES.",
            file=sys.stderr,
        )
        return 1
    if result.returncode != 0:
        pending_measurements_path.unlink(missing_ok=True)
        return result.returncode
    try:
        low = read_png(low_path)
        high = read_png(high_path)
        measurements = json.loads(pending_measurements_path.read_text(encoding="utf-8"))
        if low[:3] != high[:3]:
            raise ValueError("Godot Low/High quality captures have mismatched dimensions")
        changed = changed_pixels(low, high)
        low_peak = low_mean = high_peak = high_mean = 0.0
        low_matches = high_matches = True
        if rendering_method == "gl_compatibility":
            reference_low = read_png(REFERENCES / "low.png")
            reference_high = read_png(REFERENCES / "high.png")
            if low[:3] != reference_low[:3] or high[:3] != reference_high[:3]:
                raise ValueError("Godot Compatibility captures differ in dimensions from their references")
            low_peak, low_mean, low_matches = compare(low, reference_low, REFERENCE_PEAK, REFERENCE_MEAN)
            high_peak, high_mean, high_matches = compare(high, reference_high, REFERENCE_PEAK, REFERENCE_MEAN)
    except (OSError, ValueError) as error:
        pending_measurements_path.unlink(missing_ok=True)
        print(f"Godot quality captures or references are invalid: {error}", file=sys.stderr)
        return 1
    if changed < MIN_CHANGED_PIXELS:
        print(f"Low/High profiles changed only {changed} rendered pixels.", file=sys.stderr)
        pending_measurements_path.unlink(missing_ok=True)
        return 1
    # Keep a separate pairwise check even when both individual captures match.
    mean_difference = compare(low, high, 1.0, 1.0)[1]
    if mean_difference < MIN_MEAN_DIFFERENCE:
        print(f"Low/High profiles have only {mean_difference:.4f} mean RGB difference.", file=sys.stderr)
        pending_measurements_path.unlink(missing_ok=True)
        return 1
    if not low_matches or not high_matches:
        print(
            "Godot quality references differ: "
            f"Low peak/mean={low_peak:.4f}/{low_mean:.4f}, "
            f"High peak/mean={high_peak:.4f}/{high_mean:.4f}.",
            file=sys.stderr,
        )
        pending_measurements_path.unlink(missing_ok=True)
        return 1
    try:
        profiles = measurements["profiles"]
        if measurements["schema"] != 1:
            raise ValueError("unknown measurements schema")
        if measurements["rendering_method"] != rendering_method:
            raise ValueError("measurement renderer does not match the requested renderer")
        for name in ("low", "high"):
            profile = profiles[name]
            if profile["cpu_ms"]["sample_count"] == 0:
                raise ValueError(f"{name} profile has no CPU timing samples")
            for percentile in ("p50", "p95", "p99"):
                value = profile["cpu_ms"][percentile]
                if not isinstance(value, (int, float)) or not math.isfinite(value):
                    raise ValueError(f"{name} profile has an invalid CPU {percentile} value")
            if profile["video_memory_bytes"] <= 0:
                raise ValueError(f"{name} profile has no renderer video-memory measurement")
        low_cpu = profiles["low"]["cpu_ms"]
        high_cpu = profiles["high"]["cpu_ms"]
        gpu_status = measurements["gpu_timing_status"]
        gpu_line = f"GPU timing unavailable ({gpu_status})"
        if gpu_status == "available":
            low_gpu = profiles["low"]["gpu_ms"]
            high_gpu = profiles["high"]["gpu_ms"]
            if low_gpu["sample_count"] == 0 or high_gpu["sample_count"] == 0:
                raise ValueError("renderer reports GPU timing support but returned no samples")
            for sample in (low_gpu, high_gpu):
                for percentile in ("p50", "p95", "p99"):
                    value = sample[percentile]
                    if not isinstance(value, (int, float)) or not math.isfinite(value):
                        raise ValueError(f"GPU {percentile} measurement is invalid")
            gpu_line = (
                f"GPU p50/p95 ms low={low_gpu['p50']:.3f}/{low_gpu['p95']:.3f}, "
                f"high={high_gpu['p50']:.3f}/{high_gpu['p95']:.3f}"
            )
        elif gpu_status not in (
            "unsupported_by_compatibility_renderer",
            "unavailable_no_samples",
            "disabled_by_configuration",
        ):
            raise ValueError(f"unexpected GPU timing status: {gpu_status}")
    except (KeyError, TypeError, ValueError) as error:
        pending_measurements_path.unlink(missing_ok=True)
        print(f"Godot profile measurements are invalid: {error}", file=sys.stderr)
        return 1
    pending_measurements_path.replace(measurements_path)
    reference_summary = (
        f"references low peak/mean={low_peak:.4f}/{low_mean:.4f}, "
        f"high={high_peak:.4f}/{high_mean:.4f}"
        if rendering_method == "gl_compatibility"
        else "checked-in image references apply to Compatibility only"
    )
    print(
        f"Godot {rendering_method} quality smoke passed at {low[0]}x{low[1]}: "
        f"changed_pixels={changed}; {reference_summary}.\n"
        f"{measurements['godot_version']} {measurements['rendering_method']}: "
        f"CPU p50/p95/p99 ms low={low_cpu['p50']:.3f}/{low_cpu['p95']:.3f}/{low_cpu['p99']:.3f}, "
        f"high={high_cpu['p50']:.3f}/{high_cpu['p95']:.3f}/{high_cpu['p99']:.3f}; "
        f"renderer video memory bytes low={profiles['low']['video_memory_bytes']}, "
        f"high={profiles['high']['video_memory_bytes']}; {gpu_line}. "
        f"Full report: {measurements_path}."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
