#!/usr/bin/env python3
"""Compile and run the native Jolt shape-cache envelope regression."""

from __future__ import annotations

import os
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    compiler = os.environ.get("CXX", "clang++")
    brew_include = Path(os.environ.get("WICKED_BREW_INCLUDE_DIR",
        Path(os.environ.get("HOMEBREW_PREFIX", "/opt/homebrew")) / "include"))
    with tempfile.TemporaryDirectory(prefix="elisa-jolt-cache-test-") as temporary:
        executable = Path(temporary) / "jolt-shape-cache-test"
        command = [compiler, "-std=c++17", "-O0", "-I", str(ROOT / "native"),
            "-I", str(brew_include), str(ROOT / "test/jolt_shape_cache_test.cpp"),
            "-o", str(executable)]
        built = subprocess.run(command, check=False)
        if built.returncode != 0:
            print("Jolt shape-cache regression did not compile.")
            return built.returncode
        tested = subprocess.run([str(executable)], check=False)
        if tested.returncode != 0:
            print(f"Jolt shape-cache regression failed with status {tested.returncode}.")
            return tested.returncode
    print("Versioned Jolt shape-cache envelope tests passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
