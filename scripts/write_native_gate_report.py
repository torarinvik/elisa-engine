#!/usr/bin/env python3
"""Write the structured result for the ElisaScript native-first gate."""

import json
import os
import platform
import subprocess
import sys
import time
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 9:
        print("usage: write_native_gate_report.py OUTPUT MODE DEP SOURCE HYGIENE HEADLESS APPLICATION NATIVE", file=sys.stderr)
        return 2
    output, mode = Path(sys.argv[1]), sys.argv[2]
    names = ("dependency", "source_length", "module_hygiene", "headless", "application", "native")
    try:
        statuses = {name: int(value) for name, value in zip(names, sys.argv[3:])}
    except ValueError:
        print("native gate statuses must be integers", file=sys.stderr)
        return 2
    def stage(value: int) -> dict:
        return {"status": value, "state": "pass" if value == 0 else "skip" if value < 0 else "fail"}

    checkout = output.parent.parent
    revision = subprocess.run(
        ["git", "-C", str(checkout), "rev-parse", "HEAD"],
        capture_output=True, text=True, check=False,
    )
    report = {
        "schema": 2,
        "mode": mode,
        "outcome": "pass" if all(value <= 0 for value in statuses.values()) else "fail",
        "hardware_verification": "verified" if mode == "native" and statuses["application"] == 0 and statuses["native"] == 0 else "unverified",
        "recorded_at_unix": time.time(),
        "provenance": {
            "checkout": str(checkout),
            "git_revision": revision.stdout.strip() if revision.returncode == 0 else "unknown",
            "platform": platform.platform(),
            "python": platform.python_version(),
            "developer_dir": os.environ.get("DEVELOPER_DIR", ""),
        },
        "stages": {name: stage(value) for name, value in statuses.items()},
        # Keep flat status fields for existing consumers.
        **statuses,
    }
    output.write_text(json.dumps(report, sort_keys=True) + "\n", encoding="utf-8")
    print(f"native gate report: {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
