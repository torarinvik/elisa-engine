#!/usr/bin/env python3
"""Run the character course self-test and relaunch smokes under ASan+UBSan.

  PYTHONPATH=scripts python3 scripts/run_course_sanitized.py

Every native translation unit is built through `cxx_sanitize.py`. The prebuilt
Wicked library is not instrumented, so its inline `std::vector` code skips
ASan's container annotations; container-overflow detection is turned off for
that reason only. Every other ASan and UBSan report aborts the run.
"""

import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def main() -> int:
    environment = dict(os.environ)
    environment["CXX"] = str(ROOT / "scripts/cxx_sanitize.py")
    environment["ASAN_OPTIONS"] = "detect_container_overflow=0"
    environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
    environment["PYTHONPATH"] = str(ROOT / "scripts")
    return subprocess.call([sys.executable, str(ROOT / "scripts/application_native_smoke.py"),
        "--only", "character-course-smoke,character-course-relaunch-smoke"],
        cwd=ROOT, env=environment)


if __name__ == "__main__":
    raise SystemExit(main())
