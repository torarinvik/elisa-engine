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
        (ROOT / "scripts/test_crash_report.py",),
        (ROOT / "scripts/test_input_codes.py",),
        (ROOT / "scripts/test_animation_contract.py",),
        (ROOT / "scripts/test_application_native_smoke.py",),
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
    probe_status = run_application_test_probe_fallback_test(compiler)
    if probe_status != 0:
        return probe_status
    return run_ozz_service_test(compiler)


def run_application_test_probe_fallback_test(compiler: list[str]) -> int:
    """Ordinary game entries retain dormant course stress helpers without enabling probes."""
    with tempfile.TemporaryDirectory(prefix="elisa-application-probe-fallback-") as temporary_directory:
        executable = Path(temporary_directory) / "application-test-probe-fallback-test"
        compile_result = subprocess.run(
            [*compiler, "-std=c++17", "-O0", "-I", str(ROOT / "native"),
             str(ROOT / "native/application_test_probe.cpp"),
             str(ROOT / "test/application_test_probe_fallback_test.cpp"),
             "-o", str(executable)],
            check=False)
        if compile_result.returncode != 0:
            print("Production application test-probe fallbacks did not compile.", file=sys.stderr)
            return compile_result.returncode
        test_result = subprocess.run([str(executable)], check=False)
        if test_result.returncode != 0:
            print("Production application test-probe fallbacks did not fail closed.", file=sys.stderr)
        return test_result.returncode


def run_ozz_service_test(compiler: list[str]) -> int:
    """C02: the ozz animation service loads the cooked rig and samples many
    characters without heap allocation per tick."""
    ozz = ROOT / "dependencies/ozz"
    libraries = [ozz / "build/src" / name for name in (
        "animation/offline/libozz_animation_offline_r.a",
        "animation/runtime/libozz_animation_r.a",
        "base/libozz_base_r.a")]
    if not all(library.is_file() for library in libraries):
        print("Missing ozz; run python3 scripts/fetch_ozz.py first.", file=sys.stderr)
        return 2
    with tempfile.TemporaryDirectory(prefix="elisa-ozz-service-") as temporary_directory:
        executable = Path(temporary_directory) / "ozz-animation-service-test"
        compile_result = subprocess.run(
            [*compiler, "-std=c++17", "-O1", "-I", str(ozz / "include"),
             str(ROOT / "test/ozz_animation_service_test.cpp"), *(str(library) for library in libraries),
             "-o", str(executable)],
            check=False)
        if compile_result.returncode != 0:
            print("Native ozz animation service test did not compile.", file=sys.stderr)
            return compile_result.returncode
        test_result = subprocess.run(
            [str(executable), str(ROOT / "examples/character_course/rigs/guide_rig.anim")], check=False)
        if test_result.returncode != 0:
            print("Native ozz animation service test failed.", file=sys.stderr)
        return test_result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
