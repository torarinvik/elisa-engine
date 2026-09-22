#!/usr/bin/env python3
"""Measure the bounded Elisa world-storage workload across fresh processes."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import statistics
import subprocess
import sys
import time


ROOT = Path(__file__).resolve().parents[1]


def compiler_path() -> str:
    configured = os.environ.get("ELISA_COMPILER_BIN")
    if configured:
        return configured
    return shutil.which("elisac-stage1") or ""


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--runs", type=int, default=9)
    args = parser.parse_args()
    if not 3 <= args.runs <= 31:
        parser.error("--runs must be between 3 and 31")
    compiler = compiler_path()
    if not compiler or not Path(compiler).is_file():
        print("set ELISA_COMPILER_BIN or put elisac-stage1 on PATH", file=sys.stderr)
        return 2
    binary = ROOT / "build/world-storage-benchmark"
    binary.parent.mkdir(parents=True, exist_ok=True)
    compile_result = subprocess.run(
        [compiler, "-emit", "exe", "-o", str(binary), str(ROOT / "test/world_storage_benchmark.elisa")],
        cwd=ROOT, check=False,
    )
    if compile_result.returncode != 0:
        return compile_result.returncode
    samples_ms: list[float] = []
    for _ in range(args.runs):
        started = time.perf_counter_ns()
        result = subprocess.run([str(binary)], cwd=ROOT, check=False)
        elapsed_ms = (time.perf_counter_ns() - started) / 1_000_000.0
        if result.returncode != 0:
            print(f"world-storage benchmark workload failed with {result.returncode}", file=sys.stderr)
            return result.returncode
        samples_ms.append(elapsed_ms)
    ordered = sorted(samples_ms)
    p95 = ordered[min(len(ordered) - 1, int(len(ordered) * 0.95))]
    print("world-storage benchmark: "
        f"runs={len(samples_ms)} median_ms={statistics.median(samples_ms):.3f} "
        f"p95_ms={p95:.3f} min_ms={min(samples_ms):.3f} max_ms={max(samples_ms):.3f} "
        f"binary_bytes={binary.stat().st_size}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
