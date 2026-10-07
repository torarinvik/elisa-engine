#!/usr/bin/env python3
"""Measure optimized keyed RenderScene animation CPU cost on SDL3/Metal."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SAMPLES = 600
RUNS = 3
INSTANCES = 8


def main() -> int:
    env = dict(os.environ)
    env.update({
        "ELISA_RENDER_SCENE_RENDER_ONLY": "1",
        "ELISA_RENDER_SCENE_PROFILE_COST_ONLY": "1",
        "ELISA_RENDER_SCENE_OPTIMIZE": "1",
        "ELISA_RENDER_SCENE_NATIVE_MAIN": str(ROOT / "test/render_scene_ozz_benchmark_main.elisa"),
    })
    result = subprocess.run([sys.executable, str(ROOT / "scripts/render_scene_native_smoke.py")],
        cwd=ROOT, env=env, capture_output=True, text=True, check=False)
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
    report = {
        "platform": platform.platform(),
        "machine": platform.machine(),
        "optimization": {"elisa": "-O2", "native_cxx": "-O2"},
        "instances": INSTANCES,
        "joints_per_rig": 2,
        "warmup_group_updates_per_run": 300,
        "samples_per_run": SAMPLES,
        "allocations_per_run": 0,
        "timing_scope": "Eight public advance_animation calls; excludes frame submission and rendering.",
        "memory_sampling": "Before assets and every tenth measured update, outside the timed region.",
        "artifacts": {
            "executable": str(executable.relative_to(ROOT)),
            "executable_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
            "elisa_archive": str(archive.relative_to(ROOT)),
            "elisa_archive_sha256": hashlib.sha256(archive.read_bytes()).hexdigest(),
        },
        "runs": runs,
    }
    report_path = ROOT / "build/animation-ozz-benchmark.json"
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"benchmark report={report_path.relative_to(ROOT)}")
    print(f"measured calls per run={SAMPLES * INSTANCES}; warmup calls per run={300 * INSTANCES}; "
        "steady allocations=0; build=Elisa -O2 + native C++ -O2")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
