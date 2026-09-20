#!/usr/bin/env python3
"""Build and run the Elisa-authored application lifecycle smoke project."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="Elisa application smoke ") as temporary_directory:
        project = Path(temporary_directory)
        manifest = {
            "name": "application-native-smoke",
            "main": str(ROOT / "test/application_native_main.elisa"),
            "output": "build/application-native-smoke",
            "application": {
                "title": "Elisa Engine Smoke",
                "width": 320,
                "height": 200,
                "hidden": True,
            },
        }
        (project / "elisa.project.json").write_text(json.dumps(manifest), encoding="utf-8")
        command = [sys.executable, str(ROOT / "scripts/elisa_build_run.py"), "run", "--project", str(project)]
        status = subprocess.run(command, check=False).returncode
    if status == 0:
        print("Elisa main application lifecycle passed through the generic build/run command.")
    return status


if __name__ == "__main__":
    raise SystemExit(main())
