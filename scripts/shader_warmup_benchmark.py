#!/usr/bin/env python3
"""Measure the first Wicked render and a second launch with compiled shaders."""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path


ENGINE_ROOT = Path(__file__).resolve().parents[1]
WARMUP_RESULT = re.compile(
    r"shader warm-up: frames=(\d+) first_frame_us=(\d+) "
    r"warm_median_us=(\d+) warm_p95_us=(\d+)"
)


def parse_arguments(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--wicked-root", type=Path,
        default=Path(os.environ.get("WICKED_ROOT", ENGINE_ROOT.parent / "WickedEngine")),
        help="WickedEngine checkout (or WICKED_ROOT)")
    parser.add_argument("--probe", type=Path,
        default=ENGINE_ROOT / "build" / "wicked-native-probe",
        help="already-built native Wicked probe")
    return parser.parse_args(argv)


def run_probe(probe: Path, wicked_source: Path, manifest: Path,
    shader_root: Path) -> tuple[int, str, int]:
    environment = dict(os.environ)
    environment["ELISA_PROBE_SHADER_ROOT"] = str(shader_root)
    environment["ELISA_SHADER_WARMUP_PROBE"] = "1"
    screenshot = shader_root.parent / "shader-warmup.png"
    command = [str(probe), str(wicked_source), str(manifest), str(screenshot), "alwaysactive"]
    started = time.perf_counter_ns()
    result = subprocess.run(command, cwd=ENGINE_ROOT, env=environment,
        capture_output=True, text=True, check=False)
    elapsed_ms = (time.perf_counter_ns() - started) // 1_000_000
    output = result.stdout + result.stderr
    if result.returncode != 0:
        lines = output.splitlines()
        print("\n".join(lines[-60:]), file=sys.stderr)
        raise RuntimeError(f"Wicked warm-up probe exited with status {result.returncode}")
    return elapsed_ms, output, len(list(shader_root.rglob("*.cso")))


def main(argv: list[str] | None = None) -> int:
    args = parse_arguments(argv)
    if sys.platform != "darwin":
        print("The Wicked shader warm-up benchmark currently requires macOS/Metal.", file=sys.stderr)
        return 2
    wicked_root = args.wicked_root.expanduser().resolve()
    wicked_source = wicked_root / "WickedEngine"
    probe = args.probe.expanduser().resolve()
    manifest = ENGINE_ROOT / "backends" / "scene_manifest.txt"
    if not probe.is_file() or not os.access(probe, os.X_OK):
        print(f"Native Wicked probe is missing: {probe}", file=sys.stderr)
        print("Build it with: elisascript scripts/wicked_probe.elisascript build", file=sys.stderr)
        return 2
    if not (wicked_source / "shaders").is_dir() or not manifest.is_file():
        print("Wicked shader sources or the shared scene manifest are missing.", file=sys.stderr)
        return 2

    try:
        with tempfile.TemporaryDirectory(prefix="elisa-shader-warmup-") as temporary:
            shader_root = Path(temporary) / "shaders"
            (shader_root / "metal").mkdir(parents=True)
            # Root preflight is intentionally bypassed by this native-only probe.
            # The marker makes the output tree structurally valid while leaving
            # every shader requested by Wicked absent for the cold launch.
            (shader_root / "metal" / "elisa-warmup-anchor.cso").write_bytes(b"anchor")

            cold_ms, cold_output, cold_count = run_probe(
                probe, wicked_source, manifest, shader_root)
            cold_result = WARMUP_RESULT.search(cold_output)
            if cold_result is None:
                raise RuntimeError("cold run did not report its first and warmed frame timings")
            cold_compiled = cold_count - 1
            if cold_compiled <= 0:
                raise RuntimeError("cold run did not create any compiled shader binaries")

            warm_ms, warm_output, warm_count = run_probe(
                probe, wicked_source, manifest, shader_root)
            warm_result = WARMUP_RESULT.search(warm_output)
            if warm_result is None:
                raise RuntimeError("second run did not report its first and warmed frame timings")
            warm_new = warm_count - cold_count
            if warm_new != 0:
                raise RuntimeError(f"second launch compiled {warm_new} additional shader binaries")

            print(f"Cold launch: {cold_ms} ms; compiled {cold_compiled} shader binaries")
            print(f"  first frame: {cold_result[2]} us; later median/p95: "
                f"{cold_result[3]}/{cold_result[4]} us")
            print(f"Cached launch: {warm_ms} ms; new shader binaries: {warm_new}")
            print(f"  first frame: {warm_result[2]} us; later median/p95: "
                f"{warm_result[3]}/{warm_result[4]} us")
            print("Pipeline state caches are process-local in this measurement.")
    except (OSError, RuntimeError) as error:
        print(f"shader warm-up benchmark: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
