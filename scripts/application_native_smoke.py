#!/usr/bin/env python3
"""Build and run the Elisa-authored application lifecycle smoke project."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    command = [
        sys.executable,
        str(ROOT / "scripts/elisa_build_run.py"),
        "run",
        "--project", str(ROOT),
        "--main", "test/application_native_main.elisa",
        "--output", "build/application-native-smoke",
    ]
    status = subprocess.run(command, check=False).returncode
    if status == 0:
        print("Elisa main application lifecycle passed through the generic build/run command.")
    return status


if __name__ == "__main__":
    raise SystemExit(main())
