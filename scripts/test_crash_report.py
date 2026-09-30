#!/usr/bin/env python3
"""Compile and run the native crash-report regression."""

from __future__ import annotations

import os
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    compiler = os.environ.get("CXX", "clang++")
    with tempfile.TemporaryDirectory(prefix="elisa-crash-report-test-") as temporary:
        executable = Path(temporary) / "crash-report-test"
        reports = Path(temporary) / "reports"
        reports.mkdir()
        # -rdynamic keeps local symbol names visible to backtrace_symbols.
        command = [compiler, "-std=c++17", "-O0", "-g", "-rdynamic", "-I", str(ROOT / "native"),
            str(ROOT / "test/crash_report_test.cpp"), "-o", str(executable)]
        built = subprocess.run(command, check=False)
        if built.returncode != 0:
            print("Crash-report regression did not compile.")
            return built.returncode
        tested = subprocess.run([str(executable), str(reports), "--probe-arg"], check=False)
        if tested.returncode != 0:
            print(f"Crash-report regression failed with status {tested.returncode}.")
            for report in sorted(reports.glob("crash-*.txt")):
                print(report.read_text(encoding="utf-8", errors="replace"))
            return tested.returncode
    print("Native crash-report tests passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
