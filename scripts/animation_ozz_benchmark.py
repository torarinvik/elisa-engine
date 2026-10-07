#!/usr/bin/env python3
"""Measure optimized keyed RenderScene animation CPU cost on SDL3/Metal."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SAMPLES = 600
RUNS = 3
INSTANCES = 8


def compiler_identity(env: dict[str, str]) -> dict:
    configured = env.get("ELISA_COMPILER_BIN", "elisac-stage1")
    located = shutil.which(configured)
    if located is None:
        raise ValueError(f"compiler is unavailable: {configured}")
    entry = Path(located).resolve()
    product = entry
    with entry.open("rb") as file:
        prefix = file.read(2048)
    if prefix.startswith(b"#!"):
        forwarded = re.search(r'exec bash "([^"]+)"', prefix.decode("utf-8"))
        driver = Path(forwarded.group(1)) if forwarded else entry
        product = driver.parent.parent / "bin/elisac-stage1"
    if env.get("ELISA_STAGE1_BIN"):
        candidate = Path(env["ELISA_STAGE1_BIN"]).resolve()
        with candidate.open("rb") as file:
            if not file.read(2).startswith(b"#!"):
                product = candidate
    provenance_path = product.with_suffix(".provenance.json")
    provenance = json.loads(provenance_path.read_text()) if provenance_path.is_file() else None
    return {"entry": str(entry), "product": str(product),
        "product_sha256": hashlib.sha256(product.read_bytes()).hexdigest(), "provenance": provenance}


def authored_capture_error(path: Path) -> str | None:
    """The eight neutral silhouettes must be upright and independently placed."""
    import compare_renders
    try:
        width, height, channels, pixels = compare_renders.read_png(path)
    except (OSError, ValueError) as error:
        return f"authored crowd capture unavailable: {error}"
    if width < 160 or height < 100 or channels < 3:
        return "authored crowd capture is too small or lacks RGB pixels"
    background = pixels[:3]
    for column in range(INSTANCES):
        rows = []
        count = 0
        for y in range(height):
            for x in range(column * width // INSTANCES, (column + 1) * width // INSTANCES):
                offset = (y * width + x) * channels
                if max(abs(pixels[offset + c] - background[c]) for c in range(3)) >= 24:
                    rows.append(y)
                    count += 1
        if count < 64 or not rows or max(rows) - min(rows) + 1 < height // 5:
            return f"authored figure {column + 1} is absent or not upright in its viewport column"
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--authored", action="store_true", help="cook and measure the attributed 19-joint Cesium Man benchmark variant")
    args = parser.parse_args()
    provenance = None
    env = dict(os.environ)
    if args.authored:
        import authored_animation_fixture
        provenance = authored_animation_fixture.prepare()
        env.update({"ELISA_ANIMATION_BENCHMARK_ASSET": provenance["runtime_asset"],
            "ELISA_ANIMATION_BENCHMARK_CLIP": provenance["clip_name"],
            "ELISA_ANIMATION_BENCHMARK_JOINTS": str(provenance["joints"]),
            "ELISA_ANIMATION_BENCHMARK_CAPTURE": str(ROOT / "build/authored-animation/crowd.png")})
    compiler = compiler_identity(env)
    env.update({
        "ELISA_RENDER_SCENE_RENDER_ONLY": "1",
        "ELISA_RENDER_SCENE_PROFILE_COST_ONLY": "1",
        "ELISA_RENDER_SCENE_OPTIMIZE": "1",
        "ELISA_RENDER_SCENE_NATIVE_MAIN": str(ROOT / "test/render_scene_ozz_benchmark_main.elisa"),
    })
    result = subprocess.run([sys.executable, str(ROOT / "scripts/render_scene_native_smoke.py")],
        cwd=ROOT, env=env, capture_output=True, text=True, check=False)
    log_path = ROOT / ("build/animation-authored-benchmark.log" if args.authored else "build/animation-ozz-benchmark.log")
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_path.write_text(result.stdout + result.stderr, encoding="utf-8")
    samples = [int(line[len("anim_ns="):]) for line in result.stdout.splitlines()
        if line.startswith("anim_ns=")]
    cpu_peaks = [int(line[len("cpu_sample_peak_bytes="):]) for line in result.stdout.splitlines()
        if line.startswith("cpu_sample_peak_bytes=")]
    gpu_peaks = [int(line[len("gpu_sample_peak_bytes="):]) for line in result.stdout.splitlines()
        if line.startswith("gpu_sample_peak_bytes=")]
    if result.returncode != 0 or len(samples) != SAMPLES * RUNS or len(cpu_peaks) != RUNS or len(gpu_peaks) != RUNS:
        sys.stderr.write(result.stdout[-3000:] + result.stderr[-3000:])
        print(f"keyed animation benchmark failed: exit {result.returncode}, {len(samples)} samples",
            file=sys.stderr)
        return result.returncode or 1
    runs = []
    for run in range(RUNS):
        run_samples = samples[run * SAMPLES:(run + 1) * SAMPLES]
        ordered = sorted(run_samples)
        p50 = ordered[(len(ordered) * 50 + 99) // 100 - 1]
        p95 = ordered[(len(ordered) * 95 + 99) // 100 - 1]
        p99 = ordered[(len(ordered) * 99 + 99) // 100 - 1]
        summary = {
            "p50_us": p50 / 1000,
            "p95_us": p95 / 1000,
            "p99_us": p99 / 1000,
            "max_us": ordered[-1] / 1000,
            "sampled_cpu_footprint_peak_bytes": cpu_peaks[run],
            "sampled_gpu_allocation_peak_bytes": gpu_peaks[run],
            "samples_ns": run_samples,
        }
        runs.append(summary)
        print(f"keyed animation run={run + 1} instances={INSTANCES} samples={SAMPLES} "
            f"p50_us={p50 / 1000:.3f} p95_us={p95 / 1000:.3f} p99_us={p99 / 1000:.3f} "
            f"max_us={ordered[-1] / 1000:.3f} cpu_sample_peak_bytes={cpu_peaks[run]} "
            f"gpu_sample_peak_bytes={gpu_peaks[run]}")
    executable = ROOT / "build/render-scene-native-smoke"
    archive = ROOT / "build/render-scene-native-smoke.a"
    capture_error = authored_capture_error(ROOT / "build/authored-animation/crowd.png") if args.authored else None
    report = {
        "passed": capture_error is None and (not args.authored or all(run["p99_us"] <= 1000 for run in runs)),
        "capture_error": capture_error,
        "platform": platform.platform(),
        "machine": platform.machine(),
        "optimization": {"elisa": "-O2", "native_cxx": "-O2"},
        "instances": INSTANCES,
        "joints_per_rig": provenance["joints"] if provenance else 4,
        "skin_joints_per_rig": 19 if provenance else 2,
        "compiler": compiler,
        "clips_per_library": provenance["clips"] if provenance else 1,
        "authored_provenance": provenance,
        "p99_budget_us": 1000 if args.authored else None,
        "warmup_group_updates_per_run": 300,
        "samples_per_run": SAMPLES,
        "allocations_per_run": 0,
        "timing_scope": "Eight public advance_animation calls; excludes frame submission and rendering.",
        "memory_sampling": "Before assets, after warm render frames, every tenth measured update and after final presentation; outside the timed region.",
        "artifacts": {
            "executable": str(executable.relative_to(ROOT)),
            "executable_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
            "elisa_archive": str(archive.relative_to(ROOT)),
            "elisa_archive_sha256": hashlib.sha256(archive.read_bytes()).hexdigest(),
        },
        "runs": runs,
    }
    report_path = ROOT / ("build/animation-authored-benchmark.json" if args.authored else "build/animation-ozz-benchmark.json")
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"benchmark report={report_path.relative_to(ROOT)}")
    print(f"measured calls per run={SAMPLES * INSTANCES}; warmup calls per run={300 * INSTANCES}; "
        "steady allocations=0; build=Elisa -O2 + native C++ -O2")
    if capture_error is not None:
        print(capture_error, file=sys.stderr)
        return 1
    if args.authored and any(run["p99_us"] > 1000 for run in runs):
        print("authored animation exceeded the 1 ms p99 budget", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
