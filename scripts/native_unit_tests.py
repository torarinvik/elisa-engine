#!/usr/bin/env python3
"""Run focused native-facing unit tests that share the portable gate slot."""

from __future__ import annotations

import subprocess
import sys
import os
import shlex
import shutil
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    tests = (
        (ROOT / "scripts/save_journal.py", "--self-test"),
        (ROOT / "scripts/test_jolt_shape_cache.py",),
    )
    for command in tests:
        result = subprocess.run([sys.executable, *(str(part) for part in command)], check=False)
        if result.returncode != 0:
            return result.returncode
    compiler = shlex.split(os.environ.get("CXX", "clang++"))
    if not compiler or shutil.which(compiler[0]) is None:
        print("A C++ compiler is required for the native gamepad mapping test.", file=sys.stderr)
        return 2
    include_flags: list[str] = []
    pkg_config = shutil.which("pkg-config")
    if pkg_config:
        sdl_flags = subprocess.run(
            [pkg_config, "--cflags", "sdl3"], capture_output=True, text=True, check=False)
        if sdl_flags.returncode == 0:
            include_flags = shlex.split(sdl_flags.stdout)
    with tempfile.TemporaryDirectory(prefix="elisa-gamepad-codes-") as temporary_directory:
        executable = Path(temporary_directory) / "application-gamepad-codes-test"
        compile_result = subprocess.run(
            [*compiler, "-std=c++17", "-O0", *include_flags,
             str(ROOT / "test/application_gamepad_codes.cpp"), "-o", str(executable)],
            check=False)
        if compile_result.returncode != 0:
            print("Native gamepad mapping test did not compile.", file=sys.stderr)
            return compile_result.returncode
        test_result = subprocess.run([str(executable)], check=False)
        if test_result.returncode != 0:
            print("Native gamepad mapping test failed.", file=sys.stderr)
            return test_result.returncode
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
