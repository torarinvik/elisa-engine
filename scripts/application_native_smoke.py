#!/usr/bin/env python3
"""Build and run the Elisa-authored application lifecycle smoke project."""

from __future__ import annotations

import json
import math
import os
import struct
import subprocess
import sys
import tempfile
import wave
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="Elisa application smoke ") as temporary_directory:
        project = Path(temporary_directory)
        fixture = project / "test/fixtures/audio-smoke.wav"
        fixture.parent.mkdir(parents=True, exist_ok=True)
        samples = [int(6000 * math.sin(2.0 * math.pi * 440.0 * frame / 8000)) for frame in range(400)]
        with wave.open(str(fixture), "wb") as output:
            output.setnchannels(1)
            output.setsampwidth(2)
            output.setframerate(8000)
            output.writeframes(b"".join(struct.pack("<h", sample) for sample in samples))
        runner = ROOT / "scripts/elisa_build_run.py"
        settings = {
            "title": "Elisa Engine Smoke",
            "width": 320,
            "height": 200,
            "hidden": True,
        }
        projects = [
            ("application-native-smoke", ROOT / "test/application_native_main.elisa"),
            ("application-failure-cleanup-smoke", ROOT / "test/application_failure_native_main.elisa"),
        ]
        native_test = project / "user-data-native-test"
        native_command = [
            "clang++", "-std=c++17", "-O0",
            str(ROOT / "native/user_data_abi.cpp"),
            str(ROOT / "test/user_data_service_test.cpp"),
            "-o", str(native_test),
        ]
        compiled = subprocess.run(native_command, check=False)
        if compiled.returncode != 0:
            print("Native user-data service test did not compile.", file=sys.stderr)
            return compiled.returncode
        tested = subprocess.run([str(native_test)], check=False)
        if tested.returncode != 0:
            print("Native user-data service test failed.", file=sys.stderr)
            return tested.returncode

        for name, entry in projects:
            manifest = {
                "name": name,
                "main": str(entry),
                "output": f"build/{name}",
                "application": settings,
            }
            (project / "elisa.project.json").write_text(json.dumps(manifest), encoding="utf-8")
            command = [sys.executable, str(runner), "run", "--project", str(project),
                "--native-test-probes"]
            environment = dict(os.environ)
            environment["ELISA_USER_DATA_DIR"] = str(project / "user-data")
            status = subprocess.run(command, env=environment, check=False).returncode
            if status != 0:
                print(f"Native application smoke {name} failed with status {status}.", file=sys.stderr)
                return status
            print(f"Native application smoke {name} passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
