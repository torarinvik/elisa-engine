#!/usr/bin/env python3
"""Build and run the sanitized offline MikkTSpace geometry regression."""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    dependency = ROOT / "dependencies/mikktspace"
    source = dependency / "mikktspace.c"
    header = dependency / "mikktspace.h"
    if not source.is_file() or not header.is_file():
        raise SystemExit("missing pinned MikkTSpace; run scripts/fetch_dependencies.py")
    cc = os.environ.get("CC", "cc")
    cxx = os.environ.get("CXX", "c++")
    with tempfile.TemporaryDirectory(prefix="elisa-mikktspace-test-") as temporary:
        directory = Path(temporary)
        mikktspace_object = directory / "mikktspace.o"
        executable = directory / "mikktspace-test"
        compile_c = subprocess.run([cc, "-std=c99", "-O1", "-g", "-fsanitize=address,undefined",
            "-fno-sanitize-recover=all", "-I", str(dependency), "-c", str(source),
            "-o", str(mikktspace_object)], check=False)
        if compile_c.returncode != 0:
            return compile_c.returncode
        compile_cpp = subprocess.run([cxx, "-std=c++17", "-O1", "-g",
            "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
            "-I", str(dependency), "-I", str(ROOT / "native"),
            str(ROOT / "native/mikktspace_geometry_test.cpp"), str(mikktspace_object),
            "-o", str(executable)], check=False)
        if compile_cpp.returncode != 0:
            return compile_cpp.returncode
        environment = dict(os.environ)
        environment["ASAN_OPTIONS"] = "halt_on_error=1"
        environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
        result = subprocess.run([str(executable)], check=False, env=environment)
        return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
