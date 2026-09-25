#!/usr/bin/env python3
"""Run focused native-facing unit tests that share the portable gate slot."""

from __future__ import annotations

import subprocess
import sys
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
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
